// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "codec_adapter_h264.h"

#include "device_ctx.h"
#include "h264_decoder.h"
#include "pts_manager.h"
#include "vdec1.h"

#include <lib/fidl/cpp/clone.h>
#include <lib/zx/bti.h>

// TODO(dustingreen):
//   * Split InitializeStream() into two parts, one to get the format info from
//     the HW and send it to the Codec client, the other part to configure
//     output buffers once the client has configured Codec output config based
//     on the format info.  Wire up so that
//     onCoreCodecMidStreamOutputConstraintsChange() gets called and so that
//     CoreCodecBuildNewOutputConstraints() will pick up the correct current format
//     info (whether still mid-stream, or at the start of a new stream that's
//     starting before the mid-stream format change was processed for the old
//     stream).
//   * Allocate output video buffers contig by setting relevant buffer
//     constraints to indicate contig to BufferAllocator / BufferCollection.
//   * On EndOfStream at input, push all remaining data through the HW decoder
//     and detect when the EndOfStream is appropriate to generate at the output.
//   * Split video_->Parse() into start/complete and/or switch to feeding the
//     ring buffer directly, or whatever is wanted by multi-concurrent-stream
//     mode.
//   * Detect when there's sufficient space in the ring buffer, and feed in
//     partial input packets to permit large input packets with many AUs in
//     them.
//   * At least when promise_separate_access_units_on_input is set, propagate
//     timestamp_ish values from input AU to correct output video frame (using
//     PtsManager).
//   * Consider if there's a way to get AmlogicVideo to re-use buffers across
//     a stream switch without over-writing buffers that are still in-use
//     downstream.

namespace {

// avconv -f lavfi -i color=c=black:s=42x52 -c:v libx264 -profile:v baseline
// -vframes 1 new_stream.h264
//
// (The "baseline" part of the above isn't really needed, but neither is a
// higher profile really needed for this purpose.)
//
// bless new_stream.h264, and manually delete the big SEI NAL that has lots of
// text in it (the exact encoder settings don't really matter for this purpose),
// including its start code, up to just before the next start code, save.
//
// xxd -i new_stream.h264
//
// We push this through the decoder as our "EndOfStream" marker, and detect it
// at the output (for now) by its unusual 42x52 resolution during
// InitializeStream() _and_ the fact that we've queued this marker.  To force
// this frame to be handled by the decoder we queue kFlushThroughBytes of 0
// after this data.
//
// TODO(dustingreen): We don't currently detect the EndOfStream via its stream
// offset in PtsManager (for h264), but that would be marginally more robust
// than detecting the special resolution.  However, to detect via stream offset,
// we'd either need to avoid switching resolutions, or switch resolutions using
// the same output buffer set (including preserving the free/busy status of each
// buffer across the boundary), and delay notifying the client until we're sure
// a format change is real, not just the one immediately before a frame whose
// stream offset is >= the EndOfStream offset.
unsigned char new_stream_h264[] = {
    0x00, 0x00, 0x00, 0x01, 0x67, 0x42, 0xc0, 0x0a, 0xd9, 0x0c, 0x9e, 0x49,
    0xf0, 0x11, 0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x00, 0x03, 0x00, 0x32,
    0x0f, 0x12, 0x26, 0x48, 0x00, 0x00, 0x00, 0x01, 0x68, 0xcb, 0x83, 0xcb,
    0x20, 0x00, 0x00, 0x01, 0x65, 0x88, 0x84, 0x0a, 0xf2, 0x62, 0x80, 0x00,
    0xa7, 0xbc, 0x9c, 0x9d, 0x75, 0xd7, 0x5d, 0x75, 0xd7, 0x5d, 0x78};
unsigned int new_stream_h264_len = 59;

constexpr uint32_t kFlushThroughBytes = 1024;

constexpr uint32_t kEndOfStreamWidth = 42;
constexpr uint32_t kEndOfStreamHeight = 52;

static inline constexpr uint32_t make_fourcc(uint8_t a, uint8_t b, uint8_t c,
                                             uint8_t d) {
  return (static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16) |
         (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a);
}

// A client using the min shouldn't necessarily expect performance to be
// acceptable when running higher bit-rates.
//
// TODO(MTWN-249): Set this to ~8k or so.  For now, we have to boost the
// per-packet buffer size up to fit the largest AUs we expect to decode, until
// MTWN-249 is fixed, in case avcC format is used.
constexpr uint32_t kInputPerPacketBufferBytesMin = 512 * 1024;
// This is an arbitrary cap for now.
constexpr uint32_t kInputPerPacketBufferBytesMax = 4 * 1024 * 1024;

}  // namespace

CodecAdapterH264::CodecAdapterH264(std::mutex& lock,
                                   CodecAdapterEvents* codec_adapter_events,
                                   DeviceCtx* device)
    : CodecAdapter(lock, codec_adapter_events),
      device_(device),
      video_(device_->video()),
      input_processing_loop_(&kAsyncLoopConfigNoAttachToThread) {
  ZX_DEBUG_ASSERT(device_);
  ZX_DEBUG_ASSERT(video_);
}

CodecAdapterH264::~CodecAdapterH264() {
  // TODO(dustingreen): Remove the printfs or switch them to VLOG.
  input_processing_loop_.Quit();
  input_processing_loop_.JoinThreads();
  input_processing_loop_.Shutdown();

  // nothing else to do here, at least not until we aren't calling PowerOff() in
  // CoreCodecStopStream().
}

bool CodecAdapterH264::IsCoreCodecRequiringOutputConfigForFormatDetection() {
  return false;
}

bool CodecAdapterH264::IsCoreCodecMappedBufferNeeded(CodecPort port) {
  // If protected buffers, then only in-band AnnexB is supported, because in
  // that case we can't implement conversion of OOB AnnexB-like oob_bytes to
  // in-band AnnexB (at least not yet, since that would require one buffer to be
  // in normal non-protected RAM), and we can't implement avcC format conversion
  // to AnnexB, because that would require the CPU reading from input buffers.
  //
  // TODO(dustingreen): Make the previous paragraph true.  For now we report
  // true here to ensure we don't get secure buffers since the rest of this
  // class doesn't yet constrain what it can do based on is_secure or not.
  return true;
}

bool CodecAdapterH264::IsCoreCodecHwBased() {
  return true;
}

void CodecAdapterH264::CoreCodecInit(
    const fuchsia::media::FormatDetails& initial_input_format_details) {
  zx_status_t result = input_processing_loop_.StartThread(
      "CodecAdapterH264::input_processing_thread_", &input_processing_thread_);
  if (result != ZX_OK) {
    events_->onCoreCodecFailCodec(
        "In CodecAdapterH264::CoreCodecInit(), StartThread() failed (input)");
    return;
  }

  initial_input_format_details_ = fidl::Clone(initial_input_format_details);
  latest_input_format_details_ = fidl::Clone(initial_input_format_details);

  // TODO(dustingreen): We do most of the setup in CoreCodecStartStream()
  // currently, but we should do more here and less there.
}

// TODO(dustingreen): A lot of the stuff created in this method should be able
// to get re-used from stream to stream. We'll probably want to factor out
// create/init from stream init further down.
void CodecAdapterH264::CoreCodecStartStream() {
  zx_status_t status;

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    parsed_video_size_ = 0;
    is_input_format_details_pending_ = true;
    // At least until proven otherwise.
    is_avcc_ = false;
    is_input_end_of_stream_queued_ = false;
    is_stream_failed_ = false;
  }  // ~lock

  auto decoder = std::make_unique<H264Decoder>(video_);
  decoder->SetFrameReadyNotifier([this](std::shared_ptr<VideoFrame> frame) {
    // The Codec interface requires that emitted frames are cache clean
    // at least for now.  We invalidate without skipping over stride-width
    // per line, at least partly because stride - width is small (possibly
    // always 0) for this decoder.  But we do invalidate the UV section
    // separately in case uv_plane_offset happens to leave significant
    // space after the Y section (regardless of whether there's actually
    // ever much padding there).
    //
    // TODO(dustingreen): Probably there's not ever any significant
    // padding between Y and UV for this decoder, so probably can make one
    // invalidate call here instead of two with no downsides.
    //
    // TODO(dustingreen): Skip this when the buffer isn't map-able.
    io_buffer_cache_flush_invalidate(&frame->buffer, 0,
                                     frame->stride * frame->height);
    io_buffer_cache_flush_invalidate(&frame->buffer, frame->uv_plane_offset,
                                     frame->stride * frame->height / 2);

    const CodecBuffer* buffer = frame->codec_buffer;
    ZX_DEBUG_ASSERT(buffer);

    // We intentionally _don't_ use the packet with same index as the buffer (in
    // general - it's fine that they sometimes match), to avoid clients building
    // up inappropriate dependency on buffer index being the same as packet
    // index (as nice as that would be, VP9, and maybe others, don't get along
    // with that in general, so ... force clients to treat packet index and
    // buffer index as separate things).
    //
    // TODO(dustingreen): Make the previous paragraph fully true.  For the
    // moment for h264 we let the packet_index and buffer_index be equal, just
    // to split up the separation into smaller steps.
    CodecPacket* packet = GetFreePacket(buffer->buffer_index());
    // With h.264, we know that an emitted buffer implies an available output
    // packet, because h.264 doesn't put the same output buffer in flight more
    // than once concurrently, and we have as many output packets as buffers.
    // This contrasts with VP9 which has unbounded show_existing_frame.
    ZX_DEBUG_ASSERT(packet);

    // Associate the packet with the buffer while the packet is in-flight.
    packet->SetBuffer(buffer);

    packet->SetStartOffset(0);
    uint64_t total_size_bytes = frame->stride * frame->height * 3 / 2;
    packet->SetValidLengthBytes(total_size_bytes);

    if (frame->has_pts) {
      packet->SetTimstampIsh(frame->pts);
    } else {
      packet->ClearTimestampIsh();
    }

    events_->onCoreCodecOutputPacket(packet, false, false);
  });
  decoder->SetInitializeFramesHandler(
      fit::bind_member(this, &CodecAdapterH264::InitializeFramesHandler));
  decoder->SetErrorHandler([this] { OnCoreCodecFailStream(); });

  {  // scope lock
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    video_->SetDefaultInstance(std::move(decoder), false);
    status = video_->InitializeStreamBuffer(true, PAGE_SIZE);
    if (status != ZX_OK) {
      events_->onCoreCodecFailCodec("InitializeStreamBuffer() failed");
      return;
    }
    status = video_->video_decoder()->Initialize();
    if (status != ZX_OK) {
      events_->onCoreCodecFailCodec(
          "video_->video_decoder_->Initialize() failed");
      return;
    }
  }  // ~lock

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    status = video_->InitializeEsParser();
    if (status != ZX_OK) {
      events_->onCoreCodecFailCodec("InitializeEsParser() failed");
      return;
    }
  }  // ~lock
}

void CodecAdapterH264::CoreCodecQueueInputFormatDetails(
    const fuchsia::media::FormatDetails& per_stream_override_format_details) {
  // TODO(dustingreen): Consider letting the client specify profile/level info
  // in the FormatDetails at least optionally, and possibly sizing input
  // buffer constraints and/or other buffers based on that.

  QueueInputItem(
      CodecInputItem::FormatDetails(per_stream_override_format_details));
}

void CodecAdapterH264::CoreCodecQueueInputPacket(CodecPacket* packet) {
  QueueInputItem(CodecInputItem::Packet(packet));
}

void CodecAdapterH264::CoreCodecQueueInputEndOfStream() {
  // This queues a marker, but doesn't force the HW to necessarily decode all
  // the way up to the marker, depending on whether the client closes the stream
  // or switches to a different stream first - in those cases it's fine for the
  // marker to never show up as output EndOfStream.

  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_input_end_of_stream_queued_ = true;
  }  // ~lock

  QueueInputItem(CodecInputItem::EndOfStream());
}

// TODO(dustingreen): See comment on CoreCodecStartStream() re. not deleting
// creating as much stuff for each stream.
void CodecAdapterH264::CoreCodecStopStream() {
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);

    // This helps any previously-queued ProcessInput() calls return faster, and
    // is checked before calling WaitForParsingCompleted() in case
    // TryStartCancelParsing() does nothing.
    is_cancelling_input_processing_ = true;
  }

  // Try to cause WaitForParsingCompleted() to return early.  This only cancels
  // up to one WaitForParsingCompleted() (not queued, not sticky), so it's
  // relevant that is_cancelling_input_processing_ == true set above is
  // preventing us from starting another wait.  Or if we didn't set
  // is_cancelling_input_processing_ = true soon enough, then this call does
  // make WaitForParsingCompleted() return faster.
  video_->TryStartCancelParsing();

  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    std::condition_variable stop_input_processing_condition;
    // We know there won't be any new queuing of input, so once this posted work
    // runs, we know all previously-queued ProcessInput() calls have returned.
    PostToInputProcessingThread([this, &stop_input_processing_condition] {
      std::list<CodecInputItem> leftover_input_items;
      {  // scope lock
        std::lock_guard<std::mutex> lock(lock_);
        ZX_DEBUG_ASSERT(is_cancelling_input_processing_);
        leftover_input_items = std::move(input_queue_);
        is_cancelling_input_processing_ = false;
      }  // ~lock
      for (auto& input_item : leftover_input_items) {
        if (input_item.is_packet()) {
          events_->onCoreCodecInputPacketDone(std::move(input_item.packet()));
        }
      }
      stop_input_processing_condition.notify_all();
    });
    while (is_cancelling_input_processing_) {
      stop_input_processing_condition.wait(lock);
    }
    ZX_DEBUG_ASSERT(!is_cancelling_input_processing_);
  }  // ~lock

  // Stop processing queued frames.
  if (video_->core()) {
    video_->core()->StopDecoding();
    video_->core()->WaitForIdle();
  }

  // TODO(dustingreen): Currently, we have to tear down a few pieces of video_,
  // to make it possible to run all the AmlogicVideo + DecoderCore +
  // VideoDecoder code that seems necessary to run to ensure that a new stream
  // will be entirely separate from an old stream, without deleting/creating
  // AmlogicVideo itself.  Probably we can tackle this layer-by-layer, fixing up
  // AmlogicVideo to be more re-usable without the stuff in this method, then
  // DecoderCore, then VideoDecoder.

  video_->ClearDecoderInstance();
}

void CodecAdapterH264::CoreCodecAddBuffer(CodecPort port,
                                          const CodecBuffer* buffer) {
  if (port != kOutputPort) {
    return;
  }
  all_output_buffers_.push_back(buffer);
}

void CodecAdapterH264::CoreCodecConfigureBuffers(
    CodecPort port, const std::vector<std::unique_ptr<CodecPacket>>& packets) {
  if (port != kOutputPort) {
    return;
  }
  ZX_DEBUG_ASSERT(all_output_packets_.empty());
  ZX_DEBUG_ASSERT(!all_output_buffers_.empty());
  // TODO(dustingreen): Remove this assert - this CodecAdapter needs to stop
  // forcing this to be true.  Or, set packet count based on buffer collection
  // buffer_count, or enforce that packet count is >= buffer_count.
  ZX_DEBUG_ASSERT(all_output_buffers_.size() == packets.size());
  for (auto& packet : packets) {
    all_output_packets_.push_back(packet.get());
  }
}

void CodecAdapterH264::CoreCodecRecycleOutputPacket(CodecPacket* packet) {
  if (packet->is_new()) {
    packet->SetIsNew(false);
    return;
  }
  ZX_DEBUG_ASSERT(!packet->is_new());

  // A recycled packet will have a buffer set because the packet is in-flight
  // until put on the free list, and has a buffer associated while in-flight.
  const CodecBuffer* buffer = packet->buffer();
  ZX_DEBUG_ASSERT(buffer);

  // Getting the buffer is all we needed the packet for.  The packet won't get
  // re-used until after ReturnFrame() below.
  packet->SetBuffer(nullptr);

  // TODO(dustingreen): Use scrambled free_packet_list_ like for VP9, and put
  // packet back on the free list here (and update comment above re. when re-use
  // of the packet can happen).  For now for this codec we let packet_index ==
  // buffer_index to split the separation of buffer_index from packet_index into
  // smaller changes.

  {  // scope lock
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    std::shared_ptr<VideoFrame> frame = buffer->video_frame().lock();
    if (!frame) {
      // EndOfStream seen at the output, or a new InitializeFrames(), can cause
      // !frame, which is fine.  In that case, any new stream will request
      // allocation of new frames.
      return;
    }
    // Recycle can happen while stopped, but this CodecAdapater has no way yet
    // to return frames while stopped, or to re-use buffers/frames across a
    // stream switch.  Any new stream will request allocation of new frames.
    if (!video_->video_decoder()) {
      return;
    }
    video_->video_decoder()->ReturnFrame(frame);
  }  // ~lock
}

void CodecAdapterH264::CoreCodecEnsureBuffersNotConfigured(CodecPort port) {
  std::lock_guard<std::mutex> lock(lock_);

  // This adapter should ensure that zero old CodecPacket* or CodecBuffer*
  // remain in this adapter (or below).

  if (port == kInputPort) {
    // There shouldn't be any queued input at this point, but if there is any,
    // fail here even in a release build.
    ZX_ASSERT(input_queue_.empty());
  } else {
    ZX_DEBUG_ASSERT(port == kOutputPort);

    // The old all_output_buffers_ are no longer valid.
    all_output_buffers_.clear();
    all_output_packets_.clear();
  }
}

std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
CodecAdapterH264::CoreCodecBuildNewOutputConstraints(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_buffer_constraints_version_ordinal,
    bool buffer_constraints_action_required) {
  // bear.h264 decodes into 320x192 YUV buffers, but the video display
  // dimensions are 320x180.  A the bottom of the buffer only .25 of the last
  // 16 height macroblock row is meant to be displayed.
  //
  // TODO(dustingreen): Need to plumb video size separately from buffer size so
  // we can display (for example) a video at 320x180 instead of the buffer's
  // 320x192.  The extra pixels look like don't-care pixels that just let
  // themselves float essentially (re. past-the-boundary behavior of those
  // pixels).  Such pixels aren't meant to be displayed and look strange.
  // Presumably the difference is the buffer needing to be a whole macroblock in
  // width/height (%16==0) vs. the video dimensions being allowed to not use all
  // of the last macroblock.
  //
  // This decoder produces NV12.

  // For the moment, this codec splits 24 into 22 for the codec and 2 for the
  // client.
  //
  // TODO(dustingreen): Plumb actual frame counts.
  constexpr uint32_t kPacketCountForClientForced = 2;
  // Fairly arbitrary.  The client should set a higher value if the client needs
  // to camp on more frames than this.
  constexpr uint32_t kDefaultPacketCountForClient = kPacketCountForClientForced;

  uint32_t per_packet_buffer_bytes = min_stride_ * height_ * 3 / 2;

  std::unique_ptr<fuchsia::media::StreamOutputConstraints> config =
      std::make_unique<fuchsia::media::StreamOutputConstraints>();

  config->set_stream_lifetime_ordinal(stream_lifetime_ordinal);

  auto* constraints = config->mutable_buffer_constraints();
  auto* default_settings = constraints->mutable_default_settings();

  // For the moment, there will be only one StreamOutputConstraints, and it'll need
  // output buffers configured for it.
  ZX_DEBUG_ASSERT(buffer_constraints_action_required);
  config->set_buffer_constraints_action_required(
      buffer_constraints_action_required);
  constraints->set_buffer_constraints_version_ordinal(
      new_output_buffer_constraints_version_ordinal);

  // 0 is intentionally invalid - the client must fill out this field.
  default_settings->set_buffer_lifetime_ordinal(0);
  default_settings->set_buffer_constraints_version_ordinal(
      new_output_buffer_constraints_version_ordinal);
  default_settings->set_packet_count_for_server(packet_count_total_ -
                                                kPacketCountForClientForced);
  default_settings->set_packet_count_for_client(kDefaultPacketCountForClient);
  // Packed NV12 (no extra padding, min UV offset, min stride).
  default_settings->set_per_packet_buffer_bytes(per_packet_buffer_bytes);
  default_settings->set_single_buffer_mode(false);

  // For the moment, let's just force the client to allocate this exact size.
  constraints->set_per_packet_buffer_bytes_min(per_packet_buffer_bytes);
  constraints->set_per_packet_buffer_bytes_recommended(per_packet_buffer_bytes);
  constraints->set_per_packet_buffer_bytes_max(per_packet_buffer_bytes);

  // For the moment, let's just force the client to set this exact number of
  // frames for the codec.
  constraints->set_packet_count_for_server_min(packet_count_total_ -
                                               kPacketCountForClientForced);
  constraints->set_packet_count_for_server_recommended(
      packet_count_total_ - kPacketCountForClientForced);
  constraints->set_packet_count_for_server_recommended_max(
      packet_count_total_ - kPacketCountForClientForced);
  constraints->set_packet_count_for_server_max(packet_count_total_ -
                                               kPacketCountForClientForced);

  constraints->set_packet_count_for_client_min(kPacketCountForClientForced);
  constraints->set_packet_count_for_client_max(kPacketCountForClientForced);

  // False because it's not required and not encouraged for a video decoder
  // output to allow single buffer mode.
  constraints->set_single_buffer_mode_allowed(false);

  constraints->set_is_physically_contiguous_required(true);
  ::zx::bti very_temp_kludge_bti;
  zx_status_t dup_status =
      ::zx::unowned<::zx::bti>(video_->bti())
          ->duplicate(ZX_RIGHT_SAME_RIGHTS, &very_temp_kludge_bti);
  if (dup_status != ZX_OK) {
    events_->onCoreCodecFailCodec("BTI duplicate failed - status: %d",
                                  dup_status);
    return nullptr;
  }
  // This is very temporary.  The BufferAllocator should handle this directly,
  // not the client.
  constraints->set_very_temp_kludge_bti_handle(std::move(very_temp_kludge_bti));

  return config;
}

fuchsia::sysmem::BufferCollectionConstraints
CodecAdapterH264::CoreCodecGetBufferCollectionConstraints(
    CodecPort port,
    const fuchsia::media::StreamBufferConstraints& stream_buffer_constraints,
    const fuchsia::media::StreamBufferPartialSettings& partial_settings) {
  fuchsia::sysmem::BufferCollectionConstraints result;

  // For now, we didn't report support for single_buffer_mode, and CodecImpl
  // will have failed the codec already by this point if the client tried to
  // use single_buffer_mode.
  //
  // TODO(dustingreen): Support single_buffer_mode on input (only).
  ZX_DEBUG_ASSERT(!partial_settings.has_single_buffer_mode() || !partial_settings.single_buffer_mode());
  // The CodecImpl won't hand us the sysmem token, so we shouldn't expect to
  // have the token here.
  ZX_DEBUG_ASSERT(!partial_settings.has_sysmem_token());

  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_server());
  ZX_DEBUG_ASSERT(partial_settings.has_packet_count_for_client());
  uint32_t packet_count =
      partial_settings.packet_count_for_server() +
      partial_settings.packet_count_for_client();

  // For now this is true - when we plumb more flexible buffer count range this
  // will change to account for a range.
  ZX_DEBUG_ASSERT(port != kOutputPort || packet_count == packet_count_total_);

  // TODO(MTWN-250): plumb/permit range of buffer count from further down,
  // instead of single number frame_count, and set this to the actual
  // stream-required # of reference frames + # that can concurrently decode.
  // For the moment we demand that buffer_count equals packet_count equals
  // packet_count_for_server() + packet_count_for_client(), which is too
  // inflexible.  Also, we rely on the server setting exactly and only
  // min_buffer_count_for_camping to packet_count_for_server() and the client
  // setting exactly and only min_buffer_count_for_camping to
  // packet_count_for_client().
  result.min_buffer_count_for_camping =
      partial_settings.packet_count_for_server();
  // Some slack is nice overall, but avoid having each participant ask for
  // dedicated slack.  Using sysmem the client will ask for it's own buffers for
  // camping and any slack, so the codec doesn't need to ask for any extra on
  // behalf of the client.
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_dedicated_slack == 0);
  ZX_DEBUG_ASSERT(result.min_buffer_count_for_shared_slack == 0);
  result.max_buffer_count = packet_count;

  uint32_t per_packet_buffer_bytes_min;
  uint32_t per_packet_buffer_bytes_max;
  if (port == kInputPort) {
    per_packet_buffer_bytes_min = kInputPerPacketBufferBytesMin;
    per_packet_buffer_bytes_max = kInputPerPacketBufferBytesMax;
  } else {
    ZX_DEBUG_ASSERT(port == kOutputPort);
    // NV12, based on min stride.
    per_packet_buffer_bytes_min = min_stride_ * height_ * 3 / 2;
    // At least for now, don't cap the per-packet buffer size for output.  The
    // HW only cares about the portion we set up for output anyway, and the
    // client has no way to force output to occur into portions of the output
    // buffer beyond what's implied by the max supported image dimensions.
    per_packet_buffer_bytes_max = 0xFFFFFFFF;
  }

  result.has_buffer_memory_constraints = true;
  result.buffer_memory_constraints.min_size_bytes = per_packet_buffer_bytes_min;
  result.buffer_memory_constraints.max_size_bytes = per_packet_buffer_bytes_max;
  // amlogic requires physically contiguous on both input and output
  result.buffer_memory_constraints.physically_contiguous_required = true;
  result.buffer_memory_constraints.secure_required = false;
  // This isn't expected to fully work at first, but allow getting as far as we
  // can.
  result.buffer_memory_constraints.secure_permitted = true;

  if (port == kOutputPort) {
    result.image_format_constraints_count = 1;
    fuchsia::sysmem::ImageFormatConstraints& image_constraints =
        result.image_format_constraints[0];
    image_constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
    // TODO(MTWN-251): confirm that REC709 is always what we want here, or plumb
    // actual YUV color space if it can ever be REC601_*.  Since 2020 and 2100
    // are minimum 10 bits per Y sample and we're outputting NV12, 601 is the
    // only other potential possibility here.
    image_constraints.color_spaces_count = 1;
    image_constraints.color_space[0].type =
        fuchsia::sysmem::ColorSpaceType::REC709;

    // The non-"required_" fields indicate the decoder's ability to potentially
    // output frames at various dimensions as coded in the stream.  Aside from
    // the current stream being somewhere in these bounds, these have nothing to
    // do with the current stream in particular.
    image_constraints.min_coded_width = 16;
    image_constraints.max_coded_width = 3840;
    image_constraints.min_coded_height = 16;
    // This intentionally isn't the height of a 4k frame.  See
    // max_coded_width_times_coded_height.  We intentionally constrain the max
    // dimension in width or height to the width of a 4k frame.  While the HW
    // might be able to go bigger than that as long as the other dimension is
    // smaller to compensate, we don't really need to enable any larger than
    // 4k's width in either dimension, so we don't.
    image_constraints.max_coded_height = 3840;
    image_constraints.min_bytes_per_row = 16;
    // no hard-coded max stride, at least for now
    image_constraints.max_bytes_per_row = 0xFFFFFFFF;
    image_constraints.max_coded_width_times_coded_height = 3840 * 2160;
    image_constraints.layers = 1;
    image_constraints.coded_width_divisor = 16;
    image_constraints.coded_height_divisor = 16;
    image_constraints.bytes_per_row_divisor = 16;
    // TODO(dustingreen): Since this is a producer that will always produce at
    // offset 0 of a physical page, we don't really care if this field is
    // consistent with any constraints re. what the HW can do.
    image_constraints.start_offset_divisor = 1;
    // Odd display dimensions are permitted, but these don't imply odd NV12
    // dimensions - those are constrainted by coded_width_divisor and
    // coded_height_divisor which are both 16.
    image_constraints.display_width_divisor = 1;
    image_constraints.display_height_divisor = 1;

    // The decoder is producing frames and the decoder has no choice but to
    // produce frames at their coded size.  The decoder wants to potentially be
    // able to support a stream with dynamic resolution, potentially including
    // dimensions both less than and greater than the dimensions that led to the
    // current need to allocate a BufferCollection.  For this reason, the
    // required_ fields are set to the exact current dimensions, and the
    // permitted (non-required_) fields is set to the full potential range that
    // the decoder could potentially output.  If an initiator wants to require a
    // larger range of dimensions that includes the required range indicated
    // here (via a-priori knowledge of the potential stream dimensions), an
    // initiator is free to do so.
    image_constraints.required_min_coded_width = width_;
    image_constraints.required_max_coded_width = width_;
    image_constraints.required_min_coded_height = height_;
    image_constraints.required_max_coded_height = height_;
  } else {
    ZX_DEBUG_ASSERT(result.image_format_constraints_count == 0);
  }

  // We don't have to fill out usage - CodecImpl takes care of that.
  ZX_DEBUG_ASSERT(!result.usage.cpu);
  ZX_DEBUG_ASSERT(!result.usage.display);
  ZX_DEBUG_ASSERT(!result.usage.vulkan);
  ZX_DEBUG_ASSERT(!result.usage.video);

  return result;
}

void CodecAdapterH264::CoreCodecSetBufferCollectionInfo(
    CodecPort port,
    const fuchsia::sysmem::BufferCollectionInfo_2& buffer_collection_info) {
  ZX_DEBUG_ASSERT(buffer_collection_info.settings.buffer_settings.is_physically_contiguous);
  ZX_DEBUG_ASSERT(buffer_collection_info.settings.buffer_settings.coherency_domain == fuchsia::sysmem::CoherencyDomain::Cpu);
  if (port == kOutputPort) {
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.has_image_format_constraints);
    ZX_DEBUG_ASSERT(buffer_collection_info.settings.image_format_constraints.pixel_format.type == fuchsia::sysmem::PixelFormatType::NV12);
  }
}

fuchsia::media::StreamOutputFormat CodecAdapterH264::CoreCodecGetOutputFormat(
    uint64_t stream_lifetime_ordinal,
    uint64_t new_output_format_details_version_ordinal) {
  fuchsia::media::StreamOutputFormat result;
  result.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
  result.mutable_format_details()->set_format_details_version_ordinal(
      new_output_format_details_version_ordinal);

  result.mutable_format_details()->set_mime_type("video/raw");

  // For the moment, we'll memcpy to NV12 without any extra padding.
  fuchsia::media::VideoUncompressedFormat video_uncompressed;
  video_uncompressed.fourcc = make_fourcc('N', 'V', '1', '2');
  video_uncompressed.primary_width_pixels = width_;
  video_uncompressed.primary_height_pixels = height_;
  video_uncompressed.secondary_width_pixels = width_ / 2;
  video_uncompressed.secondary_height_pixels = height_ / 2;
  // TODO(dustingreen): remove this field from the VideoUncompressedFormat or
  // specify separately for primary / secondary.
  video_uncompressed.planar = true;
  video_uncompressed.swizzled = false;
  video_uncompressed.primary_line_stride_bytes = min_stride_;
  video_uncompressed.secondary_line_stride_bytes = min_stride_;
  video_uncompressed.primary_start_offset = 0;
  video_uncompressed.secondary_start_offset = min_stride_ * height_;
  video_uncompressed.tertiary_start_offset = min_stride_ * height_ + 1;
  video_uncompressed.primary_pixel_stride = 1;
  video_uncompressed.secondary_pixel_stride = 2;
  video_uncompressed.primary_display_width_pixels = display_width_;
  video_uncompressed.primary_display_height_pixels = display_height_;
  video_uncompressed.has_pixel_aspect_ratio = has_sar_;
  video_uncompressed.pixel_aspect_ratio_width = sar_width_;
  video_uncompressed.pixel_aspect_ratio_height = sar_height_;

  fuchsia::media::VideoFormat video_format;
  video_format.set_uncompressed(std::move(video_uncompressed));

  result.mutable_format_details()->mutable_domain()->set_video(
      std::move(video_format));

  return result;
}

void CodecAdapterH264::CoreCodecMidStreamOutputBufferReConfigPrepare() {
  // For this adapter, the core codec just needs us to get new frame buffers
  // set up, so nothing to do here.
  //
  // CoreCodecEnsureBuffersNotConfigured() will run soon.
}

void CodecAdapterH264::CoreCodecMidStreamOutputBufferReConfigFinish() {
  // Now that the client has configured output buffers, we need to hand those
  // back to the core codec via InitializedFrames.

  std::vector<CodecFrame> frames;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // Now we need to populate the frames_out vector.
    for (uint32_t i = 0; i < all_output_buffers_.size(); i++) {
      ZX_DEBUG_ASSERT(all_output_buffers_[i]->buffer_index() == i);
      ZX_DEBUG_ASSERT(all_output_buffers_[i]->codec_buffer().buffer_index() ==
                      i);
      frames.emplace_back(CodecFrame{
          .codec_buffer_spec =
              fidl::Clone(all_output_buffers_[i]->codec_buffer()),
          .codec_buffer_ptr = all_output_buffers_[i],
      });
    }
    width = width_;
    height = height_;
    stride = min_stride_;
  }  // ~lock
  {  // scope lock
    std::lock_guard<std::mutex> lock(*video_->video_decoder_lock());
    video_->video_decoder()->InitializedFrames(std::move(frames), width, height,
                                               stride);
  }  // ~lock
}

void CodecAdapterH264::PostSerial(async_dispatcher_t* dispatcher,
                                  fit::closure to_run) {
  zx_status_t post_result = async::PostTask(dispatcher, std::move(to_run));
  ZX_ASSERT_MSG(post_result == ZX_OK, "async::PostTask() failed - result: %d\n",
                post_result);
}

void CodecAdapterH264::PostToInputProcessingThread(fit::closure to_run) {
  PostSerial(input_processing_loop_.dispatcher(), std::move(to_run));
}

void CodecAdapterH264::QueueInputItem(CodecInputItem input_item) {
  bool is_trigger_needed = false;
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    // For now we don't worry about avoiding a trigger if we happen to queue
    // when ProcessInput() has removed the last item but ProcessInput() is still
    // running.
    if (!is_process_input_queued_) {
      is_trigger_needed = input_queue_.empty();
      is_process_input_queued_ = is_trigger_needed;
    }
    input_queue_.emplace_back(std::move(input_item));
  }  // ~lock
  if (is_trigger_needed) {
    PostToInputProcessingThread(
        fit::bind_member(this, &CodecAdapterH264::ProcessInput));
  }
}

CodecInputItem CodecAdapterH264::DequeueInputItem() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    if (is_stream_failed_ || is_cancelling_input_processing_ ||
        input_queue_.empty()) {
      return CodecInputItem::Invalid();
    }
    CodecInputItem to_ret = std::move(input_queue_.front());
    input_queue_.pop_front();
    return to_ret;
  }  // ~lock
}

void CodecAdapterH264::ProcessInput() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_process_input_queued_ = false;
  }  // ~lock
  while (true) {
    CodecInputItem item = DequeueInputItem();
    if (!item.is_valid()) {
      return;
    }

    if (item.is_format_details()) {
      // TODO(dustingreen): Be more strict about what the input format actually
      // is, and less strict about it matching the initial format.
      ZX_ASSERT(item.format_details() == initial_input_format_details_);

      latest_input_format_details_ = fidl::Clone(item.format_details());

      // Even if the new item.format_details() are the same as
      // initial_input_format_details_, this CodecAdapter doesn't notice any
      // in-band SPS/PPS info, so the new oob_bytes still need to be
      // (converted and) re-delivered to the core codec in case any in-band
      // SPS/PPS changes have been seen by the core codec since the previous
      // time.
      //
      // Or maybe we have no oob_bytes in which case this is irrelevant
      // but harmless.
      //
      // Or maybe the oob_bytes changed.  Either way, the core codec will
      // want that info, but in-band.  We delay sending the info to the core
      // codec until we see the first input data, to more consistently handle
      // the oob_bytes that we get initially during Codec creation.
      is_input_format_details_pending_ = true;
      continue;
    }

    if (item.is_end_of_stream()) {
      video_->pts_manager()->SetEndOfStreamOffset(parsed_video_size_);
      if (!ParseVideoAnnexB(&new_stream_h264[0], new_stream_h264_len)) {
        return;
      }
      auto bytes = std::make_unique<uint8_t[]>(kFlushThroughBytes);
      memset(bytes.get(), 0, kFlushThroughBytes);
      if (!ParseVideoAnnexB(bytes.get(), kFlushThroughBytes)) {
        return;
      }
      continue;
    }

    ZX_DEBUG_ASSERT(item.is_packet());

    if (is_input_format_details_pending_) {
      is_input_format_details_pending_ = false;
      if (!ParseAndDeliverCodecOobBytes()) {
        return;
      }
    }

    uint8_t* data =
        item.packet()->buffer()->buffer_base() + item.packet()->start_offset();
    uint32_t len = item.packet()->valid_length_bytes();

    video_->pts_manager()->InsertPts(parsed_video_size_,
                                     item.packet()->has_timestamp_ish(),
                                     item.packet()->timestamp_ish());

    // This call is the main reason the current thread exists, as this call can
    // wait synchronously until there are empty output frames available to
    // decode into, which can require the shared_fidl_thread() to get those free
    // frames to the Codec server.
    //
    // TODO(dustingreen): This call could be split into a start and complete.
    //
    // TODO(dustingreen): The current wait duration within ParseVideo() assumes
    // that free output frames will become free on an ongoing basis, which isn't
    // really what'll happen when video output is paused.
    if (!ParseVideo(data, len)) {
      return;
    }

    events_->onCoreCodecInputPacketDone(item.packet());
    // At this point CodecInputItem is holding a packet pointer which may get
    // re-used in a new CodecInputItem, but that's ok since CodecInputItem is
    // going away here.
    //
    // ~item
  }
}

bool CodecAdapterH264::ParseAndDeliverCodecOobBytes() {
  // Our latest oob_bytes may contain SPS/PPS info.  If we have any
  // such info, the core codec needs it (possibly converted first).

  // If there's no OOB info, then there's nothing to do, as all such info will
  // be in-band in normal packet-based AnnexB NALs (including start codes and
  // start code emulation prevention bytes).
  if (!latest_input_format_details_.has_oob_bytes() || latest_input_format_details_.oob_bytes().empty()) {
    // success
    return true;
  }

  const std::vector<uint8_t>* oob = &latest_input_format_details_.oob_bytes();

  // We need to deliver Annex B style SPS/PPS to this core codec, regardless of
  // what format the oob_bytes is in.

  // The oob_bytes can be in two different forms, which can be detected by
  // the value of the first byte:
  //
  // 0 - Annex B form already.  The 0 is the first byte of a start code.
  // 1 - AVCC form, which we'll convert to Annex B form.  AVCC version 1.  There
  //   is no AVCC version 0.
  // anything else - fail.
  //
  // In addition, we need to know if AVCC or not since we need to know whether
  // to add start code emulation prevention bytes or not.  And if it's AVCC,
  // how many bytes long the pseudo_nal_length field is - that field is before
  // each input NAL.

  // We already checked empty() above.
  ZX_DEBUG_ASSERT(oob->size() >= 1);
  switch ((*oob)[0]) {
    case 0:
      is_avcc_ = false;
      // This ParseVideo() consumes AnnexB oob data directly.  We don't
      // presently check if the oob data has only SPS/PPS.  This data is just
      // logically pre-pended to the stream.
      if (!ParseVideo(oob->data(), oob->size())) {
        return false;
      }
      return true;
    case 1: {
      // This applies to both the oob data and the input packet payload data.
      // Both are AVCC, or both are AnnexB.
      is_avcc_ = true;

      /*
        AVCC OOB data layout (bits):
        [0] (8) - version 1
        [1] (8) - h264 profile #
        [2] (8) - compatible profile bits
        [3] (8) - h264 level (eg. 31 == "3.1")
        [4] (6) - reserved, can be set to all 1s
            (2) - pseudo_nal_length_field_bytes_ - 1
        [5] (3) - reserved, can be set to all 1s
            (5) - sps_count
              (16) - sps_bytes
              (8*sps_bytes) - SPS nal_unit_type (that byte) + SPS data as RBSP.
            (8) - pps_count
              (16) - pps_bytes
              (8*pps_bytes) - PPS nal_unit_type (that byte) + PPS data as RBSP.
      */

      // We accept 0 SPS and/or 0 PPS, but typically there's one of each.  At
      // minimum the oob buffer needs to be large enough to contain both the
      // sps_count and pps_count fields, which is a min of 7 bytes.
      if (oob->size() < 7) {
        OnCoreCodecFailStream();
        return false;
      }
      uint32_t stashed_pseudo_nal_length_bytes = ((*oob)[4] & 0x3) + 1;
      // Temporarily, the pseudo_nal_length_field_bytes_ is 2 so we can
      // ParseVideo() directly out of "oob".
      pseudo_nal_length_field_bytes_ = 2;
      uint32_t sps_count = (*oob)[5] & 0x1F;
      uint32_t offset = 6;
      for (uint32_t i = 0; i < sps_count; ++i) {
        if (offset + 2 > oob->size()) {
          OnCoreCodecFailStream();
          return false;
        }
        uint32_t sps_length = (*oob)[offset] * 256 + (*oob)[offset + 1];
        if (offset + 2 + sps_length > oob->size()) {
          OnCoreCodecFailStream();
          return false;
        }
        if (!ParseVideo(&oob->data()[offset], 2 + sps_length)) {
          return false;
        }
        offset += 2 + sps_length;
      }
      if (offset + 1 > oob->size()) {
        OnCoreCodecFailStream();
        return false;
      }
      uint32_t pps_count = (*oob)[offset++];
      for (uint32_t i = 0; i < pps_count; ++i) {
        if (offset + 2 > oob->size()) {
          OnCoreCodecFailStream();
          return false;
        }
        uint32_t pps_length = (*oob)[offset] * 256 + (*oob)[offset + 1];
        if (offset + 2 + pps_length > oob->size()) {
          OnCoreCodecFailStream();
          return false;
        }
        if (!ParseVideo(&oob->data()[offset], 2 + pps_length)) {
          return false;
        }
        offset += 2 + pps_length;
      }
      // All pseudo-NALs in input packet payloads will use the
      // parsed count of bytes of the length field.
      pseudo_nal_length_field_bytes_ = stashed_pseudo_nal_length_bytes;
      return true;
    }
    default:
      OnCoreCodecFailStream();
      return false;
  }
}

bool CodecAdapterH264::ParseVideo(const uint8_t* data, uint32_t length) {
  return is_avcc_ ? ParseVideoAvcc(data, length)
                  : ParseVideoAnnexB(data, length);
}

bool CodecAdapterH264::ParseVideoAvcc(const uint8_t* data, uint32_t length) {
  // We don't necessarily know that is_avcc_ is true on entry to this method.
  // We use this method to send the decoder a bunch of 0x00 sometimes, which
  // will call this method regardless of is_avcc_ or not.

  // So far, the "avcC"/"AVCC" we've seen has emulation prevention bytes on it
  // already.  So we don't add those here.  But if we did need to add them, we'd
  // add them here.

  // For now we assume the heap is pretty fast and doesn't mind the size thrash,
  // but maybe we'll want to keep a buffer around (we'll optimize only if/when
  // we determine this is actually a problem).  We only actually use this buffer
  // if is_avcc_ (which is not uncommon).

  // We do parse more than one pseudo_nal per input packet.
  //
  // No splitting NALs across input packets, for now.
  //
  // TODO(dustingreen): Allow splitting NALs across input packets (not a small
  // change).  Probably also move into a source_set for sharing with other
  // CodecAdapter(s).

  // Count the input pseudo_nal(s)
  uint32_t pseudo_nal_count = 0;
  uint32_t i = 0;
  while (i < length) {
    if (i + pseudo_nal_length_field_bytes_ > length) {
      OnCoreCodecFailStream();
      return false;
    }
    // Read pseudo_nal_length field, which is a field which can be 1-4 bytes
    // long because AVCC/avcC.
    uint32_t pseudo_nal_length = 0;
    for (uint32_t length_byte = 0; length_byte < pseudo_nal_length_field_bytes_;
         ++length_byte) {
      pseudo_nal_length = pseudo_nal_length * 256 + data[i + length_byte];
    }
    i += pseudo_nal_length_field_bytes_;
    if (i + pseudo_nal_length > length) {
      OnCoreCodecFailStream();
      return false;
    }
    i += pseudo_nal_length;
    ++pseudo_nal_count;
  }

  static constexpr uint32_t kStartCodeBytes = 4;
  uint32_t local_length = length -
                          pseudo_nal_count * pseudo_nal_length_field_bytes_ +
                          pseudo_nal_count * kStartCodeBytes;
  std::unique_ptr<uint8_t[]> local_buffer =
      std::make_unique<uint8_t[]>(local_length);
  uint8_t* local_data = local_buffer.get();

  i = 0;
  uint32_t o = 0;
  while (i < length) {
    if (i + pseudo_nal_length_field_bytes_ > length) {
      OnCoreCodecFailStream();
      return false;
    }
    uint32_t pseudo_nal_length = 0;
    for (uint32_t length_byte = 0; length_byte < pseudo_nal_length_field_bytes_;
         ++length_byte) {
      pseudo_nal_length = pseudo_nal_length * 256 + data[i + length_byte];
    }
    i += pseudo_nal_length_field_bytes_;
    if (i + pseudo_nal_length > length) {
      OnCoreCodecFailStream();
      return false;
    }

    local_data[o++] = 0;
    local_data[o++] = 0;
    local_data[o++] = 0;
    local_data[o++] = 1;

    memcpy(&local_data[o], &data[i], pseudo_nal_length);
    o += pseudo_nal_length;
    i += pseudo_nal_length;
  }
  ZX_DEBUG_ASSERT(o == local_length);
  ZX_DEBUG_ASSERT(i == length);

  return ParseVideoAnnexB(local_data, local_length);
}

bool CodecAdapterH264::ParseVideoAnnexB(const uint8_t* data, uint32_t length) {
  // We don't need to check is_cancelling_input_processing_ here, because we
  // check further down before waiting (see comment there re. why the check
  // there after video_->ParseVideo() is important), and because returning false
  // from this method for the first time will prevent further calls to this
  // method thanks to propagation of false returns under ProcessInput() and a
  // check of is_cancelling_input_processing_ in DequeueInputItem() relevant to
  // any subsequent ProcessInput() while we're still stopping. So checking here
  // would only be redundant.

  // Parse AnnexB data, with start codes and start code emulation prevention
  // bytes present.
  //
  // The data won't be modified by ParseVideo().
  if (ZX_OK != video_->ParseVideo(
                   static_cast<void*>(const_cast<uint8_t*>(data)), length)) {
    OnCoreCodecFailStream();
    return false;
  }
  parsed_video_size_ += length;

  // Once we're cancelling, we're cancelling until we're done stopping.  This
  // snap of is_cancelling_input_processing_ either notices the transition to
  // cancelling or doesn't, but doesn't have to worry about
  // is_cancelling_input_processing_ becoming false again too soon because that
  // doesn't happen until after this method has returned.
  //
  // If is_cancelling does notice is_cancelling_input_processing_ true:
  //
  // It's important that we snap after calling video_->ParseVideo() above so
  // that this check occurs after parser_running_ becomes true, in case
  // is_cancelling_input_processing_ became true and TryStartCancelParsing() ran
  // before parser_running_ became true.  In that case TryStartCancelParsing()
  // did nothing - this cancelation check avoids calling
  // WaitForParsingCompleted() at all in that case, which avoids waiting for 10
  // seconds.
  //
  // If is_cancelling doesn't notice is_cancelling_input_processing_ true:
  //
  // If on the other hand we miss is_cancelling_input_processing_ changing to
  // true, then that means TryStartCancelParsing() will take care of canceling
  // WaitForParsingCompleted(), which avoids waiting for 10 seconds.
  bool is_cancelling;
  {  // scope lock
    std::unique_lock<std::mutex> lock(lock_);
    is_cancelling = is_cancelling_input_processing_;
  }

  if (is_cancelling || ZX_OK != video_->WaitForParsingCompleted(ZX_SEC(10))) {
    video_->CancelParsing();
    OnCoreCodecFailStream();
    return false;
  }
  return true;
}

zx_status_t CodecAdapterH264::InitializeFramesHandler(
    ::zx::bti bti, uint32_t frame_count, uint32_t width, uint32_t height,
    uint32_t stride, uint32_t display_width, uint32_t display_height,
    bool has_sar, uint32_t sar_width, uint32_t sar_height) {
  // First handle the special case of EndOfStream marker showing up at the
  // output.
  if (display_width == kEndOfStreamWidth &&
      display_height == kEndOfStreamHeight) {
    bool is_output_end_of_stream = false;
    {  // scope lock
      std::lock_guard<std::mutex> lock(lock_);
      if (is_input_end_of_stream_queued_) {
        is_output_end_of_stream = true;
      }
    }  // ~lock
    if (is_output_end_of_stream) {
      events_->onCoreCodecOutputEndOfStream(false);
      return ZX_ERR_STOP;
    }
  }

  // This is called on a core codec thread, ordered with respect to emitted
  // output frames.  This method needs to block until either:
  //   * Format details have been delivered to the Codec client and the Codec
  //     client has configured corresponding output buffers.
  //   * The client has moved on by closing the current stream, in which case
  //     this method needs to fail quickly so the core codec can be stopped.
  //
  // The video_decoder_lock_ is held during this method.  We don't release the
  // video_decoder_lock_ while waiting for the client, because we want close of
  // the current stream to wait for this method to return before starting the
  // portion of stream close protected by video_decoder_lock_.
  //
  // The signalling to un-block this thread uses lock_.
  //
  // TODO(dustingreen): It can happen that the current set of buffers is already
  // suitable for use under the new buffer constraints.  However, some of the
  // buffers can still be populated with data and used by other parts of the
  // system, so to re-use buffers, we'll need a way to communicate which buffers
  // are not presently available to decode into, even for what h264_decoder.cc
  // sees as a totally new set of buffers.  The h264_decoder.cc doesn't seem to
  // separate configuration of a buffer from marking that buffer ready to fill.
  // It seems like "new" buffers are immediately ready to fill.  At the moment,
  // the AmlogicVideo code doesn't appear to show any way to tell the HW which
  // frames are presently still in use (not yet available to decode into),
  // during InitializeStream().  Maybe delaying configuring of a canvas would
  // work, but in that case would the delayed configuring adversely impact
  // decoding performance consistency?  If we can do this, detect when we can,
  // and call onCoreCodecMidStreamOutputConstraintsChange() but pass false instead of
  // true, and don't expect a response or block in here.  Still have to return
  // the vector of buffers, and will need to indicate which are actually
  // available to decode into.  The rest will get indicated via
  // CoreCodecRecycleOutputPacket(), despite not necessarily getting signalled
  // to the HW by H264Decoder::ReturnFrame further down.  For now, we always
  // re-allocate buffers.  Old buffers still active elsewhere in the system can
  // continue to be referenced by those parts of the system - the important
  // thing for now is we avoid overwriting the content of those buffers by using
  // an entirely new set of buffers for each stream for now.

  // First stash some format and buffer count info needed to initialize frames
  // before triggering mid-stream format change.  Later, frames satisfying these
  // stashed parameters will be handed to the decoder via InitializedFrames(),
  // unless CoreCodecStopStream() happens first.
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);

    // For the moment, force this exact number of frames.
    //
    // TODO(dustingreen): plumb actual frame counts.
    packet_count_total_ = frame_count;
    width_ = width;
    height_ = height;
    min_stride_ = stride;
    display_width_ = display_width;
    display_height_ = display_height;
    has_sar_ = has_sar;
    sar_width_ = sar_width;
    sar_height_ = sar_height;
  }  // ~lock

  // This will snap the current stream_lifetime_ordinal_, and call
  // CoreCodecMidStreamOutputBufferReConfigPrepare() and
  // CoreCodecMidStreamOutputBufferReConfigFinish() from the StreamControl
  // thread, _iff_ the client hasn't already moved on to a new stream by then.
  events_->onCoreCodecMidStreamOutputConstraintsChange(true);

  return ZX_OK;
}

void CodecAdapterH264::OnCoreCodecFailStream() {
  {  // scope lock
    std::lock_guard<std::mutex> lock(lock_);
    is_stream_failed_ = true;
  }
  events_->onCoreCodecFailStream();
}

CodecPacket* CodecAdapterH264::GetFreePacket(uint32_t buffer_index) {
  // TODO(dustingreen): Intentionally don't always have packet_index ==
  // buffer_index.  However, for the moment, for h.264, always have packet_index
  // == buffer_index, as a temporary measure to do the separation in smaller
  // changes.
  return all_output_packets_[buffer_index];
}
