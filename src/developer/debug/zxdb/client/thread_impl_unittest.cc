// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/zxdb/client/thread_impl.h"
#include "gtest/gtest.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/zxdb/client/frame.h"
#include "src/developer/debug/zxdb/client/remote_api_test.h"
#include "src/developer/debug/zxdb/client/thread_controller.h"
#include "src/developer/debug/zxdb/client/thread_impl_test_support.h"
#include "src/developer/debug/zxdb/common/err.h"
#include "src/developer/debug/zxdb/symbols/input_location.h"

namespace zxdb {

namespace {

// This ThreadController always responds with "continue".
class ContinueThreadController : public ThreadController {
 public:
  // The parameter is a variable to set when the OnThreadStop() function is
  // called. It must outlive this class.
  explicit ContinueThreadController(bool* got_stop) : got_stop_(got_stop) {}
  ~ContinueThreadController() override = default;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override {
    set_thread(thread);
    cb(Err());
  }
  ContinueOp GetContinueOp() override { return ContinueOp::Continue(); }
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override {
    *got_stop_ = true;
    return kContinue;
  }
  const char* GetName() const override { return "Continue"; }

 private:
  bool* got_stop_;
};

// This ThreadController always reports "kUnexpected" for stops.
class UnexpectedThreadController : public ThreadController {
 public:
  explicit UnexpectedThreadController() = default;
  ~UnexpectedThreadController() override = default;

  // ThreadController implementation.
  void InitWithThread(Thread* thread,
                      std::function<void(const Err&)> cb) override {
    set_thread(thread);
    cb(Err());
  }
  ContinueOp GetContinueOp() override { return ContinueOp::Continue(); }
  StopOp OnThreadStop(
      debug_ipc::NotifyException::Type stop_type,
      const std::vector<fxl::WeakPtr<Breakpoint>>& hit_breakpoints) override {
    return kUnexpected;
  }
  const char* GetName() const override { return "Unexpected"; }
};

}  // namespace

TEST_F(ThreadImplTest, Frames) {
  // Make a process and thread for notifying about.
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  // The thread should be in a running state with no frames.
  EXPECT_TRUE(thread->GetStack().empty());
  EXPECT_FALSE(thread->GetStack().has_all_frames());

  // Notify of thread stop.
  constexpr uint64_t kAddress1 = 0x12345678;
  constexpr uint64_t kStack1 = 0x7890;
  debug_ipc::NotifyException break_notification;
  break_notification.type = debug_ipc::NotifyException::Type::kSoftware;
  break_notification.thread.process_koid = kProcessKoid;
  break_notification.thread.thread_koid = kThreadKoid;
  break_notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  break_notification.thread.frames.resize(1);
  break_notification.thread.frames[0].ip = kAddress1;
  break_notification.thread.frames[0].sp = kStack1;
  InjectException(break_notification);

  // There should be one frame with the address of the stop.
  EXPECT_FALSE(thread->GetStack().has_all_frames());
  const Stack& stack = thread->GetStack();
  ASSERT_EQ(1u, stack.size());
  EXPECT_EQ(kAddress1, stack[0]->GetAddress());
  EXPECT_EQ(kStack1, stack[0]->GetStackPointer());

  // Construct what the full stack will be returned to the thread. The top
  // element should match the one already there.
  constexpr uint64_t kAddress2 = 0x34567890;
  constexpr uint64_t kStack2 = 0x7800;
  debug_ipc::ThreadStatusReply expected_reply;
  expected_reply.record = break_notification.thread;  // Copies existing frame.
  expected_reply.record.stack_amount =
      debug_ipc::ThreadRecord::StackAmount::kFull;
  expected_reply.record.frames.emplace_back(kAddress2, 0, kStack2);
  mock_remote_api().set_thread_status_reply(expected_reply);

  // Asynchronously request the frames.
  thread->GetStack().SyncFrames(
      [](const Err&) { debug_ipc::MessageLoop::Current()->QuitNow(); });
  loop().Run();

  // The thread should have the new stack we provided.
  EXPECT_TRUE(thread->GetStack().has_all_frames());
  ASSERT_EQ(2u, stack.size());
  EXPECT_EQ(kAddress1, stack[0]->GetAddress());
  EXPECT_EQ(kStack1, stack[0]->GetStackPointer());
  EXPECT_EQ(kAddress2, stack[1]->GetAddress());
  EXPECT_EQ(kStack2, stack[1]->GetStackPointer());

  // Resuming the thread should clear the frames.
  mock_remote_api().set_resume_quits_loop(true);
  thread->Continue();
  EXPECT_EQ(0u, thread->GetStack().size());
  EXPECT_FALSE(thread->GetStack().has_all_frames());
  loop().Run();

  // After resuming we don't actually know what state the thread is in so
  // nothing should change. If we have better thread state notifications in
  // the future, it would be nice if the thread reported itself as running
  // and the stack was cleared at this point.
}

// Tests that general exceptions still run thread controllers. If the exception
// is at an address where the thread controller says "stop", that thread
// controller should be notified so it can be deleted. It doesn't matter what
// caused the stop if the thread controller thinks its done.
//
// For this exception case, the thread should always stop, even if the
// controllers say "continue."
TEST_F(ThreadImplTest, ControllersWithGeneralException) {
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  // Notify of thread stop.
  constexpr uint64_t kAddress1 = 0x12345678;
  constexpr uint64_t kStack1 = 0x7890;
  debug_ipc::NotifyException notification;
  notification.type = debug_ipc::NotifyException::Type::kSoftware;
  notification.thread.process_koid = kProcessKoid;
  notification.thread.thread_koid = kThreadKoid;
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notification.thread.frames.resize(1);
  notification.thread.frames[0].ip = kAddress1;
  notification.thread.frames[0].sp = kStack1;
  InjectException(notification);

  // Set a controller that always says to continue.
  bool got_stop = false;
  thread->ContinueWith(std::make_unique<ContinueThreadController>(&got_stop),
                       [](const Err& err) {});
  EXPECT_FALSE(got_stop);  // Should not have been called.

  // Start watching for thread events starting now.
  TestThreadObserver thread_observer(thread);

  // Notify on thread stop again (this is the same address as above but it
  // doesn't matter).
  notification.type = debug_ipc::NotifyException::Type::kGeneral;
  InjectException(notification);

  // The controller should have been notified and the thread should have issued
  // a stop notification even though the controller said to continue.
  EXPECT_TRUE(got_stop);
  EXPECT_TRUE(thread_observer.got_stopped());
}

// Tests conditions where thread controllers report unexpected stop types.
TEST_F(ThreadImplTest, ControllersUnexpected) {
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  TestThreadObserver thread_observer(thread);

  // Notify of thread stop.
  constexpr uint64_t kAddress1 = 0x12345678;
  constexpr uint64_t kStack1 = 0x7890;
  debug_ipc::NotifyException notification;
  notification.type = debug_ipc::NotifyException::Type::kSoftware;
  notification.thread.process_koid = kProcessKoid;
  notification.thread.thread_koid = kThreadKoid;
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notification.thread.frames.resize(1);
  notification.thread.frames[0].ip = kAddress1;
  notification.thread.frames[0].sp = kStack1;
  InjectException(notification);

  // No controllers means the thread should report "stopped".
  EXPECT_TRUE(thread_observer.got_stopped());
  thread_observer.set_got_stopped(false);

  // Add the controller that always reports unexpected.
  thread->ContinueWith(std::make_unique<UnexpectedThreadController>(),
                       [](const Err& err) {});

  // Notify on thread stop again (this is the same address as above but it
  // doesn't matter).
  notification.type = debug_ipc::NotifyException::Type::kSingleStep;
  InjectException(notification);

  // When all controllers report unexpected, the thread should stop.
  EXPECT_TRUE(thread_observer.got_stopped());
  thread_observer.set_got_stopped(false);

  // Add a continue controller and throw the exception. There should be one
  // controller voting "continue" and one voting "unexpected" which should
  // continue the thread.
  bool continue_got_stop = false;
  thread->ContinueWith(
      std::make_unique<ContinueThreadController>(&continue_got_stop),
      [](const Err& err) {});
  InjectException(notification);
  EXPECT_FALSE(thread_observer.got_stopped());
}

TEST_F(ThreadImplTest, JumpTo) {
  constexpr uint64_t kProcessKoid = 1234;
  InjectProcess(kProcessKoid);
  constexpr uint64_t kThreadKoid = 5678;
  Thread* thread = InjectThread(kProcessKoid, kThreadKoid);

  // Notify of thread stop.
  constexpr uint64_t kAddress1 = 0x12345678;
  constexpr uint64_t kStack = 0x7890;
  debug_ipc::NotifyException notification;
  notification.type = debug_ipc::NotifyException::Type::kSoftware;
  notification.thread.process_koid = kProcessKoid;
  notification.thread.thread_koid = kThreadKoid;
  notification.thread.state = debug_ipc::ThreadRecord::State::kBlocked;
  notification.thread.frames.resize(1);
  notification.thread.frames[0].ip = kAddress1;
  notification.thread.frames[0].sp = kStack;
  InjectException(notification);

  // Canned response for thread status.
  constexpr uint64_t kDestAddress = 0x7828374510a;
  debug_ipc::ThreadStatusReply status;
  status.record = notification.thread;  // Copies existing frame.
  status.record.stack_amount = debug_ipc::ThreadRecord::StackAmount::kFull;
  status.record.frames[0].ip = kDestAddress;
  mock_remote_api().set_thread_status_reply(status);

  // Do jump.
  bool called = false;
  Err out_err;
  thread->JumpTo(kDestAddress, [&called, &out_err](const Err& err) {
    called = true;
    out_err = err;
    debug_ipc::MessageLoop::Current()->QuitNow();
  });
  EXPECT_FALSE(called);

  // The command should have sent a request to write to the IP. There should
  // be a single register with a value of the new address.
  const auto& written = mock_remote_api().last_write_registers().registers;
  ASSERT_EQ(1u, written.size());
  ASSERT_EQ(sizeof(uint64_t), written[0].data.size());
  EXPECT_EQ(0, memcmp(&kDestAddress, &written[0].data[0], sizeof(uint64_t)));

  // The callback should be asynchronously received.
  loop().Run();
  EXPECT_TRUE(called);
  EXPECT_FALSE(out_err.has_error());

  // The thread should be in the new location.
  EXPECT_EQ(kDestAddress, thread->GetStack()[0]->GetAddress());
}

}  // namespace zxdb
