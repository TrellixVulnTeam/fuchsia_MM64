// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIA_CODECS_SW_CODEC_ADAPTER_SW_H_
#define GARNET_BIN_MEDIA_CODECS_SW_CODEC_ADAPTER_SW_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/async/cpp/task.h>
#include <lib/media/codec_impl/codec_adapter.h>
#include <lib/media/codec_impl/codec_buffer.h>
#include <lib/media/codec_impl/codec_input_item.h>
#include <lib/media/codec_impl/codec_packet.h>
#include <src/lib/fxl/macros.h>
#include <src/lib/fxl/synchronization/thread_annotations.h>
#include <threads.h>

#include <optional>
#include <queue>

#include "buffer_pool.h"
#include "mpsc_queue.h"

// TODO(turnage): Allow a range of packet count for the client instead of
// forcing a particular number.
static constexpr uint32_t kPacketCountForClientForced = 5;
static constexpr uint32_t kDefaultPacketCountForClient =
    kPacketCountForClientForced;

// We want at least 16 packets codec side because that's the worst case scenario
// for h264 keeping frames around (if the media has set its reference frame
// option to 16).
//
// TODO(turnage): Dynamically detect how many reference frames are needed by a
// given stream, to allow fewer buffers to be allocated.
static constexpr uint32_t kPacketCount = kPacketCountForClientForced + 16;

template <typename LocalOutput>
class CodecAdapterSW : public CodecAdapter {
 public:
  CodecAdapterSW(std::mutex& lock, CodecAdapterEvents* codec_adapter_events)
      : CodecAdapter(lock, codec_adapter_events),
        input_queue_(),
        free_output_packets_(),
        input_processing_loop_(&kAsyncLoopConfigNoAttachToThread) {
    ZX_DEBUG_ASSERT(events_);
  }

  ~CodecAdapterSW() = default;

  bool IsCoreCodecRequiringOutputConfigForFormatDetection() override {
    return false;
  }

  bool IsCoreCodecMappedBufferNeeded(CodecPort port) override { return true; }

  bool IsCoreCodecHwBased() override { return false; }

  void CoreCodecInit(const fuchsia::media::FormatDetails&
                         initial_input_format_details) override {
    if (!initial_input_format_details.has_format_details_version_ordinal()) {
      events_->onCoreCodecFailCodec(
          "CoreCodecInit(): Initial input format details missing version "
          "ordinal.");
      return;
    }
    // Will always be 0 for now.
    input_format_details_version_ordinal_ =
        initial_input_format_details.format_details_version_ordinal();
    zx_status_t result = input_processing_loop_.StartThread(
        "input_processing_thread_", &input_processing_thread_);
    if (result != ZX_OK) {
      events_->onCoreCodecFailCodec(
          "CodecCodecInit(): Failed to start input processing thread with "
          "zx_status_t: %d",
          result);
      return;
    }
  }

  void CoreCodecAddBuffer(CodecPort port, const CodecBuffer* buffer) override {
    if (port != kOutputPort) {
      return;
    }

    staged_output_buffers_.push_back(buffer);
  }

  void CoreCodecConfigureBuffers(
      CodecPort port,
      const std::vector<std::unique_ptr<CodecPacket>>& packets) override {
    // Nothing to do here.
  }

  void CoreCodecStartStream() override {
    // It's ok for RecycleInputPacket to make a packet free anywhere in this
    // sequence. Nothing else ought to be happening during CoreCodecStartStream
    // (in this or any other thread).
    input_queue_.Reset();
    free_output_packets_.Reset(/*keep_data=*/true);
    output_buffer_pool_.Reset(/*keep_data=*/true);
    LoadStagedOutputBuffers();

    zx_status_t post_result = async::PostTask(
        input_processing_loop_.dispatcher(), [this] { ProcessInputLoop(); });
    ZX_ASSERT_MSG(
        post_result == ZX_OK,
        "async::PostTask() failed to post input processing loop - result: %d\n",
        post_result);
  }

  void CoreCodecQueueInputFormatDetails(
      const fuchsia::media::FormatDetails& per_stream_override_format_details)
      override {
    // TODO(turnage): Accept midstream and interstream input format changes.
    // For now these should always be 0, so assert to notice if anything
    // changes.
    ZX_ASSERT(
        per_stream_override_format_details
            .has_format_details_version_ordinal() &&
        per_stream_override_format_details.format_details_version_ordinal() ==
            input_format_details_version_ordinal_);
    input_queue_.Push(
        CodecInputItem::FormatDetails(per_stream_override_format_details));
  }

  void CoreCodecQueueInputPacket(CodecPacket* packet) override {
    input_queue_.Push(CodecInputItem::Packet(packet));
  }

  void CoreCodecQueueInputEndOfStream() override {
    input_queue_.Push(CodecInputItem::EndOfStream());
  }

  void CoreCodecStopStream() override {
    input_queue_.StopAllWaits();
    free_output_packets_.StopAllWaits();
    output_buffer_pool_.StopAllWaits();

    WaitForInputProcessingLoopToEnd();
    CleanUpAfterStream();

    auto queued_input_items =
        BlockingMpscQueue<CodecInputItem>::Extract(std::move(input_queue_));
    while (!queued_input_items.empty()) {
      CodecInputItem input_item = std::move(queued_input_items.front());
      queued_input_items.pop();
      if (input_item.is_packet()) {
        events_->onCoreCodecInputPacketDone(input_item.packet());
      }
    }
  }

  void CoreCodecRecycleOutputPacket(CodecPacket* packet) override {
    if (packet->buffer()) {
      LocalOutput local_output;
      {
        std::lock_guard<std::mutex> lock(lock_);
        local_output = std::move(in_use_by_client_[packet]);
        in_use_by_client_.erase(packet);
      }

      // ~ local_output, which may trigger a buffer free callback.
    }
    free_output_packets_.Push(std::move(packet));
  }

  void CoreCodecEnsureBuffersNotConfigured(CodecPort port) override {
    if (port != kOutputPort) {
      // We don't do anything with input buffers.
      return;
    }

    output_buffer_pool_.Reset();
    staged_output_buffers_.clear();

    std::map<CodecPacket*, LocalOutput> to_drop;
    {
      std::lock_guard<std::mutex> lock(lock_);
      std::swap(to_drop, in_use_by_client_);
    }
    // ~ to_drop

    // Given that we currently fail the codec on mid-stream output format
    // change (elsewhere), the decoder won't have buffers referenced here.
    ZX_DEBUG_ASSERT(!output_buffer_pool_.has_buffers_in_use());

    free_output_packets_.Reset();
  }

  void CoreCodecMidStreamOutputBufferReConfigPrepare() override {
    // Nothing to do here.
  }

  void CoreCodecMidStreamOutputBufferReConfigFinish() override {
    LoadStagedOutputBuffers();
  }

  std::unique_ptr<const fuchsia::media::StreamOutputConstraints>
  CoreCodecBuildNewOutputConstraints(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_buffer_constraints_version_ordinal,
      bool buffer_constraints_action_required) override {
    auto [format_details, per_packet_buffer_bytes] = OutputFormatDetails();

    auto config = std::make_unique<fuchsia::media::StreamOutputConstraints>();

    config->set_stream_lifetime_ordinal(stream_lifetime_ordinal);

    // For the moment, there will be only one StreamOutputConstraints, and it'll
    // need output buffers configured for it.
    ZX_DEBUG_ASSERT(buffer_constraints_action_required);
    config->set_buffer_constraints_action_required(
        buffer_constraints_action_required);
    auto* constraints = config->mutable_buffer_constraints();
    constraints->set_buffer_constraints_version_ordinal(
        new_output_buffer_constraints_version_ordinal);
    // For the moment, let's just force the client to allocate this exact size.
    constraints->set_per_packet_buffer_bytes_min(per_packet_buffer_bytes);
    constraints->set_per_packet_buffer_bytes_recommended(
        per_packet_buffer_bytes);
    constraints->set_per_packet_buffer_bytes_max(per_packet_buffer_bytes);

    // For the moment, let's just force the client to set this exact number of
    // frames for the codec.
    constraints->set_packet_count_for_server_min(kPacketCount -
                                                 kPacketCountForClientForced);
    constraints->set_packet_count_for_server_recommended(
        kPacketCount - kPacketCountForClientForced);
    constraints->set_packet_count_for_server_recommended_max(
        kPacketCount - kPacketCountForClientForced);
    constraints->set_packet_count_for_server_max(kPacketCount -
                                                 kPacketCountForClientForced);

    constraints->set_packet_count_for_client_min(kPacketCountForClientForced);
    constraints->set_packet_count_for_client_max(kPacketCountForClientForced);

    constraints->set_single_buffer_mode_allowed(false);
    constraints->set_is_physically_contiguous_required(false);

    // 0 is intentionally invalid - the client must fill out this field.
    auto* default_settings = constraints->mutable_default_settings();
    default_settings->set_buffer_lifetime_ordinal(0);
    default_settings->set_buffer_constraints_version_ordinal(
        new_output_buffer_constraints_version_ordinal);
    default_settings->set_packet_count_for_server(kPacketCount -
                                                  kPacketCountForClientForced);
    default_settings->set_packet_count_for_client(kDefaultPacketCountForClient);
    default_settings->set_per_packet_buffer_bytes(per_packet_buffer_bytes);
    default_settings->set_single_buffer_mode(false);

    return config;
  }

  fuchsia::media::StreamOutputFormat CoreCodecGetOutputFormat(
      uint64_t stream_lifetime_ordinal,
      uint64_t new_output_format_details_version_ordinal) override {
    fuchsia::media::StreamOutputFormat result;
    // format_details is fuchsia::media::FormatDetails
    auto [format_details, per_packet_buffer_bytes] = OutputFormatDetails();
    result.set_stream_lifetime_ordinal(stream_lifetime_ordinal);
    result.set_format_details(std::move(format_details));
    result.mutable_format_details()->set_format_details_version_ordinal(
        new_output_format_details_version_ordinal);
    return result;
  }

 protected:
  void WaitForInputProcessingLoopToEnd() {
    ZX_DEBUG_ASSERT(thrd_current() != input_processing_thread_);

    std::condition_variable stream_stopped_condition;
    bool stream_stopped = false;
    zx_status_t post_result =
        async::PostTask(input_processing_loop_.dispatcher(),
                        [this, &stream_stopped, &stream_stopped_condition] {
                          {
                            std::lock_guard<std::mutex> lock(lock_);
                            stream_stopped = true;
                          }
                          stream_stopped_condition.notify_all();
                        });
    ZX_ASSERT_MSG(
        post_result == ZX_OK,
        "async::PostTask() failed to post input processing loop - result: %d\n",
        post_result);

    std::unique_lock<std::mutex> lock(lock_);
    stream_stopped_condition.wait(lock,
                                  [&stream_stopped] { return stream_stopped; });
  }

  // We don't give the codec any buffers in its output pool until
  // configuration is finished or a stream starts. Until finishing
  // configuration we stage all the buffers. Here we load all the staged
  // buffers so the codec can make output.
  void LoadStagedOutputBuffers() {
    std::vector<const CodecBuffer*> to_add = std::move(staged_output_buffers_);
    for (auto buffer : to_add) {
      output_buffer_pool_.AddBuffer(buffer);
    }
  }

  // Processes input in a loop. Should only execute on input_processing_thread_.
  // Loops for the lifetime of a stream.
  virtual void ProcessInputLoop() = 0;

  // Releases any resources from the just-ended stream.
  virtual void CleanUpAfterStream() = 0;

  // Returns the format details of the output and the bytes needed to store each
  // output packet.
  virtual std::pair<fuchsia::media::FormatDetails, size_t>
  OutputFormatDetails() = 0;

  BlockingMpscQueue<CodecInputItem> input_queue_;
  BlockingMpscQueue<CodecPacket*> free_output_packets_;

  std::map<CodecPacket*, LocalOutput> in_use_by_client_ FXL_GUARDED_BY(lock_);
  BufferPool output_buffer_pool_;

  // Buffers the client has added but that we cannot use until configuration is
  // complete.
  std::vector<const CodecBuffer*> staged_output_buffers_;

  uint64_t input_format_details_version_ordinal_;

  async::Loop input_processing_loop_;
  thrd_t input_processing_thread_;
};

#endif  // GARNET_BIN_MEDIA_CODECS_SW_CODEC_ADAPTER_SW_H_
