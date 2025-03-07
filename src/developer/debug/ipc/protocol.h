// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_DEBUG_IPC_PROTOCOL_H_
#define GARNET_LIB_DEBUG_IPC_PROTOCOL_H_

#include "src/developer/debug/ipc/records.h"

namespace debug_ipc {

// As defined in zircon/types.h
using zx_status_t = int32_t;

constexpr uint32_t kProtocolVersion = 7;

enum class Arch : uint32_t { kUnknown = 0, kX64, kArm64 };

#pragma pack(push, 8)

// A message consists of a MsgHeader followed by a serialized version of
// whatever struct is associated with that message type. Use the MessageWriter
// class to build this up, which will reserve room for the header and allows
// the structs to be appended, possibly dynamically.
struct MsgHeader {
  enum class Type : uint32_t {
    kNone = 0,
    kHello,

    kAddOrChangeBreakpoint,
    kAddressSpace,
    kConfigAgent,
    kAttach,
    kDetach,
    kJobFilter,
    kKill,
    kLaunch,
    kModules,
    kPause,
    kProcessTree,
    kQuitAgent,
    kReadMemory,
    kReadRegisters,
    kWriteRegisters,
    kRemoveBreakpoint,
    kResume,
    kSysInfo, kThreadStatus,
    kThreads,
    kWriteMemory,

    // The "notify" messages are sent unrequested from the agent to the client.
    kNotifyProcessExiting,
    kNotifyProcessStarting,
    kNotifyThreadStarting,
    kNotifyThreadExiting,
    kNotifyException,
    kNotifyModules,
    kNotifyIO,

    kNumMessages
  };
  static const char* TypeToString(Type);

  MsgHeader() = default;
  explicit MsgHeader(Type t) : type(t) {}

  uint32_t size = 0;  // Size includes this header.
  Type type = Type::kNone;

  // The transaction ID is assigned by the sender of a request, and is echoed
  // in the reply so the transaction can be easily correlated.
  //
  // Notification messages (sent unsolicited from the agent to the client) have
  // a 0 transaction ID.
  uint32_t transaction_id = 0;

  static constexpr uint32_t kSerializedHeaderSize = sizeof(uint32_t) * 3;
};

struct HelloRequest {};
struct HelloReply {
  // Stream signature to make sure we're talking to the right service.
  // This number is ASCII for "zxdbIPC>".
  static constexpr uint64_t kStreamSignature = 0x7a7864624950433e;

  static constexpr uint32_t kCurrentVersion = 1;

  uint64_t signature = kStreamSignature;
  uint32_t version = kCurrentVersion;
  Arch arch = Arch::kUnknown;
};

enum class InferiorType : uint32_t {
  kBinary,
  kComponent,
  kLast,
};
const char* InferiorTypeToString(InferiorType);

struct LaunchRequest {
  // TODO(DX-953): zxdb should be able to recognize when something is a binary
  //               or a component. Replying with the type is probably OK as it
  //               makes the client handling a bit easier.
  InferiorType inferior_type = InferiorType::kLast;

  // argv[0] is the app to launch.
  std::vector<std::string> argv;
};
struct LaunchReply {
  // The client needs to react differently depending on whether we started a
  // process or a component.
  InferiorType inferior_type;

  // zx_status_t value from launch, ZX_OK on success.
  zx_status_t status = 0;

  // These fields are mutually exclusive. If InferiorType is process, then
  // process_id != 0 and component_id == 0. If it's component, it's the other
  // way around.
  uint64_t process_id = 0;
  uint64_t component_id = 0;

  std::string process_name;
};

struct KillRequest {
  uint64_t process_koid = 0;
};
struct KillReply {
  zx_status_t status = 0;
};

enum class TaskType : uint32_t { kProcess = 0, kJob, kComponentRoot, kLast };
const char* TaskTypeToString(TaskType);

// The debug agent will follow a successful AttachReply with notifications for
// all threads currently existing in the attached process.
struct AttachRequest {
  TaskType type = TaskType::kProcess;
  uint64_t koid = 0;  // Unused for ComponentRoot.
};

struct AttachReply {
  uint64_t koid = 0;
  zx_status_t status =
      0;  // zx_status_t value from attaching. ZX_OK on success.
  std::string name;
};

struct DetachRequest {
  TaskType type = TaskType::kProcess;
  uint64_t koid = 0;
};
struct DetachReply {
  zx_status_t status = 0;
};

struct PauseRequest {
  // If 0, all threads of all debugged processes will be paused.
  uint64_t process_koid = 0;

  // If 0, all threads in the given process will be paused.
  uint64_t thread_koid = 0;
};
// The backend should make a best effort to ensure the requested threads are
// actually stopped before sending the reply.
struct PauseReply {
  // The updated thead state for all affected threads.
  std::vector<ThreadRecord> threads;
};

struct QuitAgentRequest {};
struct QuitAgentReply {};

struct ResumeRequest {
  enum class How : uint32_t {
    kContinue = 0,     // Continue execution without stopping.
    kStepInstruction,  // Step one machine instruction.
    kStepInRange,      // Step until control exits an address range.

    kLast  // Not a real state, used for validation.
  };

  // If 0, all threads of all debugged processes will be continued.
  uint64_t process_koid = 0;

  // If empty, all threads in the given process will be continued. If nonempty,
  // the threads with listed koids will be resumed. kStepInRange may only be
  // used with a single thread.
  std::vector<uint64_t> thread_koids;

  How how = How::kContinue;

  // When how == kStepInRange, these variables define the address range to
  // step in. As long as the instruction pointer is inside
  // [range_begin, range_end), execution will continue.
  uint64_t range_begin = 0;
  uint64_t range_end = 0;
};
struct ResumeReply {};

struct ProcessTreeRequest {};
struct ProcessTreeReply {
  ProcessTreeRecord root;
};

struct ThreadsRequest {
  uint64_t process_koid = 0;
};
struct ThreadsReply {
  // If there is no such process, the threads array will be empty.
  std::vector<ThreadRecord> threads;
};

struct ReadMemoryRequest {
  uint64_t process_koid = 0;
  uint64_t address = 0;
  uint32_t size = 0;
};
struct ReadMemoryReply {
  std::vector<MemoryBlock> blocks;
};

struct AddOrChangeBreakpointRequest {
  BreakpointSettings breakpoint;
};
struct AddOrChangeBreakpointReply {
  // A variety of race conditions could cause a breakpoint modification or
  // set to fail. For example, updating or setting a breakpoint could race
  // with the library containing that code unloading.
  //
  // The update or set will always apply the breakpoint to any contexts that
  // it can apply to (if there are multiple locations, we don't want to
  // remove them all just because one failed). Therefore, you can't
  // definitively say the breakpoint is invalid just because it has a failure
  // code here. If necessary, we can add more information in the failure.
  zx_status_t status = 0;
};

struct RemoveBreakpointRequest {
  uint32_t breakpoint_id = 0;
};
struct RemoveBreakpointReply {};

struct SysInfoRequest {};
struct SysInfoReply {
  std::string version;
  uint32_t num_cpus;
  uint32_t memory_mb;
  uint32_t hw_breakpoint_count;
  uint32_t hw_watchpoint_count;
};

// The thread state request asks for the current thread status with a full
// backtrace if it is suspended. If the thread with the given KOID doesn't
// exist, the ThreadRecord will report a "kDead" status.
struct ThreadStatusRequest {
  uint64_t process_koid = 0;
  uint32_t thread_koid = 0;
};
struct ThreadStatusReply {
  ThreadRecord record;
};

struct AddressSpaceRequest {
  uint64_t process_koid = 0;
  // if non-zero |address| indicates to return only the regions
  // that contain it.
  uint64_t address = 0;
};

struct AddressSpaceReply {
  std::vector<AddressRegion> map;
};

struct ModulesRequest {
  uint64_t process_koid = 0;
};
struct ModulesReply {
  std::vector<Module> modules;
};

// Request to set filter.
struct JobFilterRequest {
  uint64_t job_koid = 0;
  std::vector<std::string> filters;
};

struct JobFilterReply {
  zx_status_t status = 0;  // zx_status for filter request
};

struct WriteMemoryRequest {
  uint64_t process_koid = 0;
  uint64_t address = 0;
  std::vector<uint8_t> data;
};

struct WriteMemoryReply {
  zx_status_t status = 0;
};

// ReadRegisters ---------------------------------------------------------------

struct ReadRegistersRequest {
  uint64_t process_koid = 0;
  uint64_t thread_koid = 0;
  // What categories do we want to receive data from.
  std::vector<RegisterCategory::Type> categories;
};

struct ReadRegistersReply {
  std::vector<RegisterCategory> categories;
};

// WriteRegisters --------------------------------------------------------------

struct WriteRegistersRequest {
  uint64_t process_koid = 0;
  uint64_t thread_koid = 0;
  std::vector<Register> registers;
};

struct WriteRegistersReply {
  zx_status_t status = 0;
};

// Agent Config ----------------------------------------------------------------
//
// The client sends a list of configurations and will receive a status result
// for each of them in order.

struct ConfigAgentRequest {
  std::vector<ConfigAction> actions;
};

struct ConfigAgentReply {
  std::vector<zx_status_t> results;
};

// Notifications ---------------------------------------------------------------

// Notify that a new process was created in debugged job.
struct NotifyProcessStarting {
  uint64_t koid = 0;
  // When components are launched from the debugger, they look like normal
  // processes starting. The debug agent sets an id to them so the debugger can
  // detect them and start them as they would normal processes.
  //
  // 0 means non set.
  uint32_t component_id = 0;
  std::string name = "";
};

// Data for process destroyed messages (process created messages are in
// response to launch commands so is just the reply to that message).
struct NotifyProcessExiting {
  uint64_t process_koid = 0;
  int64_t return_code = 0;
};

// Data for thread created and destroyed messages.
struct NotifyThread {
  ThreadRecord record;
};

// Data passed for exceptions.
struct NotifyException {
  enum class Type : uint32_t {
    // No current exception, used as placeholder or to indicate not set.
    kNone = 0,

    kGeneral,

    // Hardware breakpoints are issues by the CPU via debug registers.
    kHardware,

    // Single-step completion issued by the CPU.
    kSingleStep,

    // Software breakpoint. This will be issued when a breakpoint is hit and
    // when the debugged program manually issues a breakpoint instruction.
    kSoftware,

    // Indicates this exception is not a real CPU exception but was generated
    // internally for the purposes of sending a stop notification. The frontend
    // uses this value when the thread didn't actually do anything, but the
    // should be updated as if it hit an exception.
    kSynthetic,

    kLast  // Not an actual exception type, for range checking.
  };
  static const char* TypeToString(Type);

  // Holds the state and a minimal stack (up to 2 frames) of the thread at the
  // moment of notification.
  ThreadRecord thread;

  Type type = Type::kNone;

  // When the stop was caused by hitting a breakpoint, this vector will contain
  // the post-hit stats of every hit breakpoint (since there can be more than
  // one breakpoint at any given address).
  //
  // Be sure to check should_delete on each of these and update local state as
  // necessary.
  std::vector<BreakpointStats> hit_breakpoints;
};

// Indicates the loaded modules may have changed. The entire list of current
// modules is sent every time.
struct NotifyModules {
  uint64_t process_koid = 0;
  std::vector<Module> modules;

  // The list of threads in the process stopped automatically as a result of
  // the module load. The client will want to resume these threads once it has
  // processed the load.
  std::vector<uint64_t> stopped_thread_koids;
};

struct NotifyIO {
  static constexpr size_t kMaxDataSize = 64 * 1024;  // 64k.

  enum class Type : uint32_t {
    kStderr,
    kStdout,
    kLast,    // Not a real type.
  };
  static const char* TypeToString(Type);

  uint64_t process_koid = 0;
  Type type = Type::kLast;

  // Data will be up most kMaxDataSize bytes.
  std::string data;

  // Whether this is a piece of bigger message.
  // This information can be used by the consumer to change how it will handle
  // this data.
  bool more_data_available = false;
};

#pragma pack(pop)

}  // namespace debug_ipc

#endif  // GARNET_LIB_DEBUG_IPC_PROTOCOL_H_
