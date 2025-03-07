// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/stack.h"

#include <map>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/macros.h"
#include "src/developer/debug/ipc/records.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/frame_fingerprint.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/expr/expr_eval_context.h"
#include "src/developer/debug/zxdb/symbols/function.h"

namespace zxdb {

namespace {

// Implementation of Frame for inlined frames. Inlined frames have a different
// location in the source code, but refer to the underlying physical frame for
// most data.
class InlineFrame final : public Frame {
 public:
  // The physical_frame must outlive this class. Normally both are owned by the
  // Stack and have the same lifetime.
  InlineFrame(Frame* physical_frame, Location loc)
      : Frame(physical_frame->session()),
        physical_frame_(physical_frame),
        location_(loc) {}
  ~InlineFrame() override = default;

  // Frame implementation.
  Thread* GetThread() const override { return physical_frame_->GetThread(); }
  bool IsInline() const override { return true; }
  const Frame* GetPhysicalFrame() const override { return physical_frame_; }
  const Location& GetLocation() const override { return location_; }
  uint64_t GetAddress() const override { return location_.address(); }
  uint64_t GetBasePointerRegister() const override {
    return physical_frame_->GetBasePointerRegister();
  }
  std::optional<uint64_t> GetBasePointer() const override {
    return physical_frame_->GetBasePointer();
  }
  void GetBasePointerAsync(std::function<void(uint64_t bp)> cb) override {
    return physical_frame_->GetBasePointerAsync(std::move(cb));
  }
  uint64_t GetStackPointer() const override {
    return physical_frame_->GetStackPointer();
  }
  fxl::RefPtr<SymbolDataProvider> GetSymbolDataProvider() const override {
    return physical_frame_->GetSymbolDataProvider();
  }
  fxl::RefPtr<ExprEvalContext> GetExprEvalContext() const override {
    return physical_frame_->GetExprEvalContext();
  }
  bool IsAmbiguousInlineLocation() const override {
    const Location& loc = GetLocation();

    // Extract the inline function.
    if (!loc.symbol())
      return false;
    const Function* function = loc.symbol().Get()->AsFunction();
    if (!function)
      return false;
    if (!function->is_inline())
      return false;

    // There could be multiple code ranges for the inlined function, consider
    // any of them as being a candidate.
    for (const auto& cur :
         function->GetAbsoluteCodeRanges(loc.symbol_context())) {
      if (loc.address() == cur.begin())
        return true;
    }
    return false;
  }

 private:
  Frame* physical_frame_;  // Non-owning.
  Location location_;

  FXL_DISALLOW_COPY_AND_ASSIGN(InlineFrame);
};

// Returns a fixed-up location referring to an indexed element in an inlined
// function call chain. This also handles the case where there are no inline
// calls and the function is the only one (this returns the same location).
//
// The main_location is the location returned by symbol lookup for the
// current address.
Location LocationForInlineFrameChain(
    const std::vector<const Function*>& inline_chain, size_t chain_index,
    const Location& main_location) {
  // The file/line is the call location of the next (into the future) inlined
  // function. Fall back on the file/line from the main lookup.
  const FileLine* new_line = &main_location.file_line();
  int new_column = main_location.column();
  if (chain_index > 0) {
    const Function* next_call = inline_chain[chain_index - 1];
    if (next_call->call_line().is_valid()) {
      new_line = &next_call->call_line();
      new_column = 0;  // DWARF doesn't contain inline call column.
    }
  }

  return Location(main_location.address(), *new_line, new_column,
                  main_location.symbol_context(),
                  LazySymbol(inline_chain[chain_index]));
}

}  // namespace

Stack::Stack(Delegate* delegate) : delegate_(delegate), weak_factory_(this) {}

Stack::~Stack() = default;

fxl::WeakPtr<Stack> Stack::GetWeakPtr() { return weak_factory_.GetWeakPtr(); }

std::optional<size_t> Stack::IndexForFrame(const Frame* frame) const {
  for (size_t i = 0; i < frames_.size(); i++) {
    if (frames_[i].get() == frame)
      return i;
  }
  return std::nullopt;
}

size_t Stack::InlineDepthForIndex(size_t index) const {
  FXL_DCHECK(index < frames_.size());
  for (size_t depth = 0; index + depth < frames_.size(); depth++) {
    if (!frames_[index + depth]->IsInline())
      return depth;
  }

  FXL_NOTREACHED();  // Should have found a physical frame that generated it.
  return 0;
}

std::optional<FrameFingerprint> Stack::GetFrameFingerprint(
    size_t virtual_frame_index) const {
  size_t frame_index = virtual_frame_index + hide_ambiguous_inline_frame_count_;

  // Should reference a valid index in the array.
  if (frame_index >= frames_.size()) {
    FXL_NOTREACHED();
    return FrameFingerprint();
  }

  // The inline frame count is the number of steps from the requested frame
  // index to the current physical frame.
  size_t inline_count = InlineDepthForIndex(frame_index);

  // The stack pointer we want is the one from right before the current
  // physical frame (see frame_fingerprint.h).
  size_t before_physical_frame_index = frame_index + inline_count + 1;
  if (before_physical_frame_index == frames_.size()) {
    if (!has_all_frames())
      return std::nullopt;  // Not synchronously available.

    // For the bottom frame, this returns the frame base pointer instead which
    // will at least identify the frame in some ways, and can be used to see if
    // future frames are younger.
    return FrameFingerprint(frames_[frame_index]->GetStackPointer(), 0);
  }

  return FrameFingerprint(
      frames_[before_physical_frame_index]->GetStackPointer(), inline_count);
}

void Stack::GetFrameFingerprint(
    size_t virtual_frame_index,
    std::function<void(const Err&, FrameFingerprint)> cb) {
  size_t frame_index = virtual_frame_index + hide_ambiguous_inline_frame_count_;
  FXL_DCHECK(frame_index < frames_.size());

  // Identify the frame in question across the async call by its combination of
  // IP, SP, and inline nesting count. If anything changes we don't want to
  // issue the callback.
  uint64_t ip = frames_[frame_index]->GetAddress();
  uint64_t sp = frames_[frame_index]->GetStackPointer();
  size_t inline_count = InlineDepthForIndex(frame_index);

  // This callback is issued when the full stack is available.
  auto on_full_stack = [weak_stack = GetWeakPtr(), frame_index, ip, sp,
                        inline_count, cb = std::move(cb)](const Err& err) {
    if (err.has_error()) {
      cb(err, FrameFingerprint());
      return;
    }
    if (!weak_stack) {
      cb(Err("Thread destroyed."), FrameFingerprint());
      return;
    }
    const auto& frames = weak_stack->frames_;

    if (frame_index >= frames.size() ||
        frames[frame_index]->GetAddress() != ip ||
        frames[frame_index]->GetStackPointer() != sp ||
        weak_stack->InlineDepthForIndex(frame_index) != inline_count) {
      // Something changed about this stack item since the original call.
      // Count the request as invalid.
      cb(Err("Stack changed across queries."), FrameFingerprint());
      return;
    }

    // Should always have a fingerprint after syncing the stack.
    auto found_fingerprint = weak_stack->GetFrameFingerprint(frame_index);
    FXL_DCHECK(found_fingerprint);
    cb(Err(), *found_fingerprint);
  };

  if (has_all_frames()) {
    // All frames are available, don't force a recomputation of the stack. But
    // the caller still expects an async response. Calling the full callback
    // is important for the checking in case the frames changed while the
    // async task is pending.
    debug_ipc::MessageLoop::Current()->PostTask(
        FROM_HERE,
        [on_full_stack = std::move(on_full_stack)]() { on_full_stack(Err()); });
  } else {
    SyncFrames(std::move(on_full_stack));
  }
}

size_t Stack::GetAmbiguousInlineFrameCount() const {
  // This can't be InlineDepthForIndex() because that takes an index relative
  // to the hide_ambiguous_inline_frame_count_ and this function always wants
  // to return the same thing regardless of the hide count.
  for (size_t i = 0; i < frames_.size(); i++) {
    if (!frames_[i]->IsAmbiguousInlineLocation())
      return i;
  }

  // Should always have a non-inline frame if there are any.
  FXL_DCHECK(frames_.empty());
  return 0;
}

void Stack::SetHideAmbiguousInlineFrameCount(size_t hide_count) {
  FXL_DCHECK(hide_count <= GetAmbiguousInlineFrameCount());
  hide_ambiguous_inline_frame_count_ = hide_count;
}

void Stack::SyncFrames(std::function<void(const Err&)> callback) {
  delegate_->SyncFramesForStack(std::move(callback));
}

void Stack::SetFrames(debug_ipc::ThreadRecord::StackAmount amount,
                      const std::vector<debug_ipc::StackFrame>& new_frames) {
  // See if the new frames are an extension of the existing frames or are a
  // replacement.
  size_t appending_from = 0;  // First index in new_frames to append.
  for (size_t i = 0; i < frames_.size(); i++) {
    // The input will not contain any inline frames so skip over those when
    // doing the checking.
    if (frames_[i]->IsInline())
      continue;

    if (appending_from >= new_frames.size() ||
        frames_[i]->GetAddress() != new_frames[appending_from].ip ||
        frames_[i]->GetStackPointer() != new_frames[appending_from].sp ||
        frames_[i]->GetBasePointerRegister() != new_frames[appending_from].bp) {
      // New frames are not a superset of our existing stack, replace
      // everything.
      hide_ambiguous_inline_frame_count_ = 0;
      frames_.clear();
      appending_from = 0;
      break;
    }

    appending_from++;
  }

  for (size_t i = appending_from; i < new_frames.size(); i++)
    AppendFrame(new_frames[i]);

  has_all_frames_ = amount == debug_ipc::ThreadRecord::StackAmount::kFull;
}

void Stack::SetFramesForTest(std::vector<std::unique_ptr<Frame>> frames,
                             bool has_all) {
  frames_ = std::move(frames);
  has_all_frames_ = has_all;
  hide_ambiguous_inline_frame_count_ = 0;
}

bool Stack::ClearFrames() {
  has_all_frames_ = false;
  hide_ambiguous_inline_frame_count_ = 0;

  if (frames_.empty())
    return false;  // Nothing to do.

  frames_.clear();
  return true;
}

void Stack::AppendFrame(const debug_ipc::StackFrame& record) {
  // This symbolizes all stack frames since the expansion of inline frames
  // depends on the symbols. Its possible some stack objects will never have
  // their frames queried which makes this duplicate work. A possible addition
  // is to just save the debug_ipc::StackFrames and only expand the inline
  // frames when the frame list is accessed.

  // Indicates we're adding the newest physical frame and its inlines to the
  // frame list.
  bool is_top_physical_frame = frames_.empty();

  // The symbols will provide the location for the innermost inlined function.
  Location inner_loc = delegate_->GetSymbolizedLocationForStackFrame(record);

  const Function* cur_func = inner_loc.symbol().Get()->AsFunction();
  if (!cur_func) {
    // No function associated with this location.
    frames_.push_back(delegate_->MakeFrameForStack(record, inner_loc));
    return;
  }

  // The Location object will reference the most-specific inline function but
  // we need the whole chain.
  std::vector<const Function*> inline_chain = cur_func->GetInlineChain();
  if (inline_chain.back()->is_inline()) {
    // A non-inline frame was not found. The symbols are corrupt so give up
    // on inline processing and add the physical frame only.
    frames_.push_back(delegate_->MakeFrameForStack(record, inner_loc));
    return;
  }

  // Need to make the base "physical" frame first because all of the inline
  // frames refer to it.
  auto physical_frame = delegate_->MakeFrameForStack(
      record, LocationForInlineFrameChain(inline_chain, inline_chain.size() - 1,
                                          inner_loc));

  // Add inline functions (skipping the last which is the physical frame
  // made above).
  for (size_t i = 0; i < inline_chain.size() - 1; i++) {
    auto inline_frame = std::make_unique<InlineFrame>(
        physical_frame.get(),
        LocationForInlineFrameChain(inline_chain, i, inner_loc));

    // Only add ambiguous inline frames when they correspond to the top
    // physical frame of the stack. The reason is that the instruction pointer
    // of non-topmost stack frames represents the return address. An ambiguous
    // inline frame means the return address is the beginning of an inlined
    // function. This implies that the function call itself isn't actually in
    // that inlined function.
    //
    // TODO(brettw) we may want to consider checking the address immediately
    // before the IP for these frames and using that for inline frame
    // computation. This may make the stack make more sense when a function
    // call is the last part of an inline frame, but it also may make the
    // line numbers for these frames inconsistent with how they're displayed
    // for non-inlined frames.
    if (is_top_physical_frame || !inline_frame->IsAmbiguousInlineLocation())
      frames_.push_back(std::move(inline_frame));
  }

  // Physical frame goes last (back in time).
  frames_.push_back(std::move(physical_frame));
}

}  // namespace zxdb
