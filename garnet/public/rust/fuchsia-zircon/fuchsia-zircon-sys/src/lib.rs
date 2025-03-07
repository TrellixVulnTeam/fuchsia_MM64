// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(non_camel_case_types)]
#![deny(warnings)]

pub type zx_addr_t = usize;
pub type zx_clock_t = u32;
pub type zx_duration_t = i64;
pub type zx_futex_t = i32;
pub type zx_handle_t = u32;
pub type zx_off_t = u64;
pub type zx_paddr_t = usize;
pub type zx_rights_t = u32;
pub type zx_signals_t = u32;
pub type zx_ssize_t = isize;
pub type zx_status_t = i32;
pub type zx_ticks_t = u64;
pub type zx_time_t = i64;
pub type zx_vaddr_t = usize;
pub type zx_gpaddr_t = usize;
pub type zx_vm_option_t = u32;
pub type zx_obj_type_t = i32;
pub type zx_obj_props_t = u32;
pub type zx_koid_t = u64;
pub type zx_object_info_topic_t = u32;
pub type zx_guest_trap_t = u32;

pub const ZX_MAX_NAME_LEN: usize = 32;

// TODO: combine these macros with the bitflags and assoc consts macros below
// so that we only have to do one macro invocation.
// The result would look something like:
// multiconst!(bitflags, zx_rights_t, Rights, [RIGHT_NONE => ZX_RIGHT_NONE = 0; ...]);
// multiconst!(assoc_consts, zx_status_t, Status, [OK => ZX_OK = 0; ...]);
// Note that the actual name of the inner macro (e.g. `bitflags`) can't be a variable.
// It'll just have to be matched on manually
macro_rules! multiconst {
    ($typename:ident, [$($(#[$attr:meta])* $rawname:ident = $value:expr;)*]) => {
        $(
            $(#[$attr])*
            pub const $rawname: $typename = $value;
        )*
    }
}

multiconst!(zx_handle_t, [
    ZX_HANDLE_INVALID = 0;
]);

multiconst!(zx_time_t, [
    ZX_TIME_INFINITE = ::std::i64::MAX;
    ZX_TIME_INFINITE_PAST = ::std::i64::MIN;
]);

multiconst!(zx_rights_t, [
    ZX_RIGHT_NONE           = 0;
    ZX_RIGHT_DUPLICATE      = 1 << 0;
    ZX_RIGHT_TRANSFER       = 1 << 1;
    ZX_RIGHT_READ           = 1 << 2;
    ZX_RIGHT_WRITE          = 1 << 3;
    ZX_RIGHT_EXECUTE        = 1 << 4;
    ZX_RIGHT_MAP            = 1 << 5;
    ZX_RIGHT_GET_PROPERTY   = 1 << 6;
    ZX_RIGHT_SET_PROPERTY   = 1 << 7;
    ZX_RIGHT_ENUMERATE      = 1 << 8;
    ZX_RIGHT_DESTROY        = 1 << 9;
    ZX_RIGHT_SET_POLICY     = 1 << 10;
    ZX_RIGHT_GET_POLICY     = 1 << 11;
    ZX_RIGHT_SIGNAL         = 1 << 12;
    ZX_RIGHT_SIGNAL_PEER    = 1 << 13;
    ZX_RIGHT_WAIT           = 1 << 14;
    ZX_RIGHT_INSPECT        = 1 << 15;
    ZX_RIGHT_MANAGE_JOB     = 1 << 16;
    ZX_RIGHT_MANAGE_PROCESS = 1 << 17;
    ZX_RIGHT_MANAGE_THREAD  = 1 << 18;
    ZX_RIGHT_APPLY_PROFILE  = 1 << 19;
    ZX_RIGHT_SAME_RIGHTS    = 1 << 31;
]);

multiconst!(u32, [
    ZX_VMO_NON_RESIZABLE = 1;
]);

// TODO: add an alias for this type in the C headers.
multiconst!(u32, [
    ZX_VMO_OP_COMMIT = 1;
    ZX_VMO_OP_DECOMMIT = 2;
    ZX_VMO_OP_LOCK = 3;
    ZX_VMO_OP_UNLOCK = 4;
    ZX_VMO_OP_CACHE_SYNC = 6;
    ZX_VMO_OP_CACHE_INVALIDATE = 7;
    ZX_VMO_OP_CACHE_CLEAN = 8;
    ZX_VMO_OP_CACHE_CLEAN_INVALIDATE = 9;
]);

// TODO: add an alias for this type in the C headers.
multiconst!(zx_vm_option_t, [
    ZX_VM_PERM_READ             = 1 << 0;
    ZX_VM_PERM_WRITE            = 1 << 1;
    ZX_VM_PERM_EXECUTE          = 1 << 2;
    ZX_VM_COMPACT               = 1 << 3;
    ZX_VM_SPECIFIC              = 1 << 4;
    ZX_VM_SPECIFIC_OVERWRITE    = 1 << 5;
    ZX_VM_CAN_MAP_SPECIFIC      = 1 << 6;
    ZX_VM_CAN_MAP_READ          = 1 << 7;
    ZX_VM_CAN_MAP_WRITE         = 1 << 8;
    ZX_VM_CAN_MAP_EXECUTE       = 1 << 9;
    ZX_VM_MAP_RANGE             = 1 << 10;
    ZX_VM_REQUIRE_NON_RESIZABLE = 1 << 11;
]);

// matches ///zircon/system/public/zircon/errors.h
multiconst!(zx_status_t, [
    ZX_OK                         = 0;
    ZX_ERR_INTERNAL               = -1;
    ZX_ERR_NOT_SUPPORTED          = -2;
    ZX_ERR_NO_RESOURCES           = -3;
    ZX_ERR_NO_MEMORY              = -4;
    ZX_ERR_INTERRUPTED_RETRY      = -6;
    ZX_ERR_INVALID_ARGS           = -10;
    ZX_ERR_BAD_HANDLE             = -11;
    ZX_ERR_WRONG_TYPE             = -12;
    ZX_ERR_BAD_SYSCALL            = -13;
    ZX_ERR_OUT_OF_RANGE           = -14;
    ZX_ERR_BUFFER_TOO_SMALL       = -15;
    ZX_ERR_BAD_STATE              = -20;
    ZX_ERR_TIMED_OUT              = -21;
    ZX_ERR_SHOULD_WAIT            = -22;
    ZX_ERR_CANCELED               = -23;
    ZX_ERR_PEER_CLOSED            = -24;
    ZX_ERR_NOT_FOUND              = -25;
    ZX_ERR_ALREADY_EXISTS         = -26;
    ZX_ERR_ALREADY_BOUND          = -27;
    ZX_ERR_UNAVAILABLE            = -28;
    ZX_ERR_ACCESS_DENIED          = -30;
    ZX_ERR_IO                     = -40;
    ZX_ERR_IO_REFUSED             = -41;
    ZX_ERR_IO_DATA_INTEGRITY      = -42;
    ZX_ERR_IO_DATA_LOSS           = -43;
    ZX_ERR_IO_OVERRUN             = -45;
    ZX_ERR_IO_MISSED_DEADLINE     = -46;
    ZX_ERR_IO_INVALID             = -47;
    ZX_ERR_BAD_PATH               = -50;
    ZX_ERR_NOT_DIR                = -51;
    ZX_ERR_NOT_FILE               = -52;
    ZX_ERR_FILE_BIG               = -53;
    ZX_ERR_NO_SPACE               = -54;
    ZX_ERR_NOT_EMPTY              = -55;
    ZX_ERR_STOP                   = -60;
    ZX_ERR_NEXT                   = -61;
    ZX_ERR_ASYNC                  = -62;
    ZX_ERR_PROTOCOL_NOT_SUPPORTED = -70;
    ZX_ERR_ADDRESS_UNREACHABLE    = -71;
    ZX_ERR_ADDRESS_IN_USE         = -72;
    ZX_ERR_NOT_CONNECTED          = -73;
    ZX_ERR_CONNECTION_REFUSED     = -74;
    ZX_ERR_CONNECTION_RESET       = -75;
    ZX_ERR_CONNECTION_ABORTED     = -76;
]);

multiconst!(zx_signals_t, [
    ZX_SIGNAL_NONE              = 0;
    ZX_OBJECT_SIGNAL_ALL        = 0x00ffffff;
    ZX_USER_SIGNAL_ALL          = 0xff000000;
    ZX_OBJECT_SIGNAL_0          = 1 << 0;
    ZX_OBJECT_SIGNAL_1          = 1 << 1;
    ZX_OBJECT_SIGNAL_2          = 1 << 2;
    ZX_OBJECT_SIGNAL_3          = 1 << 3;
    ZX_OBJECT_SIGNAL_4          = 1 << 4;
    ZX_OBJECT_SIGNAL_5          = 1 << 5;
    ZX_OBJECT_SIGNAL_6          = 1 << 6;
    ZX_OBJECT_SIGNAL_7          = 1 << 7;
    ZX_OBJECT_SIGNAL_8          = 1 << 8;
    ZX_OBJECT_SIGNAL_9          = 1 << 9;
    ZX_OBJECT_SIGNAL_10         = 1 << 10;
    ZX_OBJECT_SIGNAL_11         = 1 << 11;
    ZX_OBJECT_SIGNAL_12         = 1 << 12;
    ZX_OBJECT_SIGNAL_13         = 1 << 13;
    ZX_OBJECT_SIGNAL_14         = 1 << 14;
    ZX_OBJECT_SIGNAL_15         = 1 << 15;
    ZX_OBJECT_SIGNAL_16         = 1 << 16;
    ZX_OBJECT_SIGNAL_17         = 1 << 17;
    ZX_OBJECT_SIGNAL_18         = 1 << 18;
    ZX_OBJECT_SIGNAL_19         = 1 << 19;
    ZX_OBJECT_SIGNAL_20         = 1 << 20;
    ZX_OBJECT_SIGNAL_21         = 1 << 21;
    ZX_OBJECT_SIGNAL_22         = 1 << 22;
    ZX_OBJECT_HANDLE_CLOSED     = 1 << 23;
    ZX_USER_SIGNAL_0            = 1 << 24;
    ZX_USER_SIGNAL_1            = 1 << 25;
    ZX_USER_SIGNAL_2            = 1 << 26;
    ZX_USER_SIGNAL_3            = 1 << 27;
    ZX_USER_SIGNAL_4            = 1 << 28;
    ZX_USER_SIGNAL_5            = 1 << 29;
    ZX_USER_SIGNAL_6            = 1 << 30;
    ZX_USER_SIGNAL_7            = 1 << 31;

    ZX_OBJECT_READABLE          = ZX_OBJECT_SIGNAL_0;
    ZX_OBJECT_WRITABLE          = ZX_OBJECT_SIGNAL_1;
    ZX_OBJECT_PEER_CLOSED       = ZX_OBJECT_SIGNAL_2;

    // Cancelation (handle was closed while waiting with it)
    ZX_SIGNAL_HANDLE_CLOSED     = ZX_OBJECT_HANDLE_CLOSED;

    // Event
    ZX_EVENT_SIGNALED           = ZX_OBJECT_SIGNAL_3;

    // EventPair
    ZX_EVENTPAIR_SIGNALED       = ZX_OBJECT_SIGNAL_3;
    ZX_EVENTPAIR_CLOSED         = ZX_OBJECT_SIGNAL_2;

    // Task signals (process, thread, job)
    ZX_TASK_TERMINATED          = ZX_OBJECT_SIGNAL_3;

    // Channel
    ZX_CHANNEL_READABLE         = ZX_OBJECT_SIGNAL_0;
    ZX_CHANNEL_WRITABLE         = ZX_OBJECT_SIGNAL_1;
    ZX_CHANNEL_PEER_CLOSED      = ZX_OBJECT_SIGNAL_2;

    // Socket
    ZX_SOCKET_READABLE            = ZX_OBJECT_READABLE;
    ZX_SOCKET_WRITABLE            = ZX_OBJECT_WRITABLE;
    ZX_SOCKET_PEER_CLOSED         = ZX_OBJECT_PEER_CLOSED;
    ZX_SOCKET_PEER_WRITE_DISABLED = ZX_OBJECT_SIGNAL_4;
    ZX_SOCKET_WRITE_DISABLED      = ZX_OBJECT_SIGNAL_5;
    ZX_SOCKET_CONTROL_READABLE    = ZX_OBJECT_SIGNAL_6;
    ZX_SOCKET_CONTROL_WRITABLE    = ZX_OBJECT_SIGNAL_7;
    ZX_SOCKET_ACCEPT              = ZX_OBJECT_SIGNAL_8;
    ZX_SOCKET_SHARE               = ZX_OBJECT_SIGNAL_9;
    ZX_SOCKET_READ_THRESHOLD      = ZX_OBJECT_SIGNAL_10;
    ZX_SOCKET_WRITE_THRESHOLD     = ZX_OBJECT_SIGNAL_11;

    // Resource
    ZX_RESOURCE_DESTROYED       = ZX_OBJECT_SIGNAL_3;
    ZX_RESOURCE_READABLE        = ZX_OBJECT_READABLE;
    ZX_RESOURCE_WRITABLE        = ZX_OBJECT_WRITABLE;
    ZX_RESOURCE_CHILD_ADDED     = ZX_OBJECT_SIGNAL_4;

    // Fifo
    ZX_FIFO_READABLE            = ZX_OBJECT_READABLE;
    ZX_FIFO_WRITABLE            = ZX_OBJECT_WRITABLE;
    ZX_FIFO_PEER_CLOSED         = ZX_OBJECT_PEER_CLOSED;

    // Job
    ZX_JOB_NO_JOBS              = ZX_OBJECT_SIGNAL_4;
    ZX_JOB_NO_PROCESSES         = ZX_OBJECT_SIGNAL_5;

    // Process
    ZX_PROCESS_TERMINATED       = ZX_OBJECT_SIGNAL_3;

    // Thread
    ZX_THREAD_TERMINATED        = ZX_OBJECT_SIGNAL_3;

    // Log
    ZX_LOG_READABLE             = ZX_OBJECT_READABLE;
    ZX_LOG_WRITABLE             = ZX_OBJECT_WRITABLE;

    // Timer
    ZX_TIMER_SIGNALED           = ZX_OBJECT_SIGNAL_3;
]);

multiconst!(zx_obj_type_t, [
    ZX_OBJ_TYPE_NONE                = 0;
    ZX_OBJ_TYPE_PROCESS             = 1;
    ZX_OBJ_TYPE_THREAD              = 2;
    ZX_OBJ_TYPE_VMO                 = 3;
    ZX_OBJ_TYPE_CHANNEL             = 4;
    ZX_OBJ_TYPE_EVENT               = 5;
    ZX_OBJ_TYPE_PORT                = 6;
    ZX_OBJ_TYPE_INTERRUPT           = 9;
    ZX_OBJ_TYPE_PCI_DEVICE          = 11;
    ZX_OBJ_TYPE_LOG                 = 12;
    ZX_OBJ_TYPE_SOCKET              = 14;
    ZX_OBJ_TYPE_RESOURCE            = 15;
    ZX_OBJ_TYPE_EVENTPAIR           = 16;
    ZX_OBJ_TYPE_JOB                 = 17;
    ZX_OBJ_TYPE_VMAR                = 18;
    ZX_OBJ_TYPE_FIFO                = 19;
    ZX_OBJ_TYPE_GUEST               = 20;
    ZX_OBJ_TYPE_VCPU                = 21;
    ZX_OBJ_TYPE_TIMER               = 22;
    ZX_OBJ_TYPE_IOMMU               = 23;
    ZX_OBJ_TYPE_BTI                 = 24;
    ZX_OBJ_TYPE_PROFILE             = 25;
    ZX_OBJ_TYPE_PMT                 = 26;
    ZX_OBJ_TYPE_SUSPEND_TOKEN       = 27;
    ZX_OBJ_TYPE_PAGER               = 28;
]);

multiconst!(zx_obj_props_t, [
    ZX_OBJ_PROP_NONE                  = 0;
    ZX_OBJ_PROP_WAITABLE              = 1;

    // Argument is a char[ZX_MAX_NAME_LEN].
    ZX_PROP_NAME                      = 3;

    // Argument is a uintptr_t.
    #[cfg(target_arch = "x86_64")]
    ZX_PROP_REGISTER_GS               = 2;
    #[cfg(target_arch = "x86_64")]
    ZX_PROP_REGISTER_FS               = 4;

    // Argument is the value of ld.so's _dl_debug_addr, a uintptr_t. If the
    // property is set to the magic value of ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET
    // on process startup, ld.so will trigger a debug breakpoint immediately after
    // setting the property to the correct value.
    ZX_PROP_PROCESS_DEBUG_ADDR        = 5;

    // Argument is the base address of the vDSO mapping (or zero), a uintptr_t.
    ZX_PROP_PROCESS_VDSO_BASE_ADDRESS = 6;

    // Argument is a size_t.
    ZX_PROP_SOCKET_RX_THRESHOLD       = 12;
    ZX_PROP_SOCKET_TX_THRESHOLD       = 13;

    // Argument is a size_t, describing the number of packets a channel
    // endpoint can have pending in its tx direction.
    ZX_PROP_CHANNEL_TX_MSG_MAX        = 14;

    // Terminate this job if the system is low on memory.
    ZX_PROP_JOB_KILL_ON_OOM           = 15;
]);

pub const ZX_PROCESS_DEBUG_ADDR_BREAK_ON_SET: usize = 1;

// clock ids
multiconst!(zx_clock_t, [
    ZX_CLOCK_MONOTONIC    = 0;
    ZX_CLOCK_UTC          = 1;
    ZX_CLOCK_THREAD       = 2;
]);

// Buffer size limits on the cprng syscalls
pub const ZX_CPRNG_DRAW_MAX_LEN: usize = 256;
pub const ZX_CPRNG_ADD_ENTROPY_MAX_LEN: usize = 256;

// Socket flags and limits.
pub const ZX_SOCKET_SHUTDOWN_WRITE: u32 = 1;
pub const ZX_SOCKET_SHUTDOWN_READ: u32 = 1 << 1;

// VM Object clone flags
pub const ZX_VMO_CHILD_COPY_ON_WRITE: u32 = 1;
pub const ZX_VMO_CHILD_NON_RESIZEABLE: u32 = 1 << 1;

// channel write size constants
pub const ZX_CHANNEL_MAX_MSG_HANDLES: u32 = 64;
pub const ZX_CHANNEL_MAX_MSG_BYTES: u32 = 65536;

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_cache_policy_t {
    ZX_CACHE_POLICY_CACHED = 0,
    ZX_CACHE_POLICY_UNCACHED = 1,
    ZX_CACHE_POLICY_UNCACHED_DEVICE = 2,
    ZX_CACHE_POLICY_WRITE_COMBINING = 3,
}

// Flag bits for zx_cache_flush.
multiconst!(u32, [
    ZX_CACHE_FLUSH_INSN         = 1 << 0;
    ZX_CACHE_FLUSH_DATA         = 1 << 1;
    ZX_CACHE_FLUSH_INVALIDATE   = 1 << 2;
]);

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_wait_item_t {
    pub handle: zx_handle_t,
    pub waitfor: zx_signals_t,
    pub pending: zx_signals_t,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_waitset_result_t {
    pub cookie: u64,
    pub status: zx_status_t,
    pub observed: zx_signals_t,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_channel_call_args_t {
    pub wr_bytes: *const u8,
    pub wr_handles: *const zx_handle_t,
    pub rd_bytes: *mut u8,
    pub rd_handles: *mut zx_handle_t,
    pub wr_num_bytes: u32,
    pub wr_num_handles: u32,
    pub rd_num_bytes: u32,
    pub rd_num_handles: u32,
}

pub type zx_pci_irq_swizzle_lut_t = [[[u32; 4]; 8]; 32];

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_pci_init_arg_t {
    pub dev_pin_to_global_irq: zx_pci_irq_swizzle_lut_t,
    pub num_irqs: u32,
    pub irqs: [zx_irq_t; 32],
    pub ecam_window_count: u32,
    // Note: the ecam_windows field is actually a variable size array.
    // We use a fixed size array to match the C repr.
    pub ecam_windows: [zx_ecam_window_t; 1],
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_irq_t {
    pub global_irq: u32,
    pub level_triggered: bool,
    pub active_high: bool,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_ecam_window_t {
    pub base: u64,
    pub size: usize,
    pub bus_start: u8,
    pub bus_end: u8,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_pcie_device_info_t {
    pub vendor_id: u16,
    pub device_id: u16,
    pub base_class: u8,
    pub sub_class: u8,
    pub program_interface: u8,
    pub revision_id: u8,
    pub bus_id: u8,
    pub dev_id: u8,
    pub func_id: u8,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_pci_resource_t {
    pub type_: u32,
    pub size: usize,
    // TODO: Actually a union
    pub pio_addr: usize,
}

// TODO: Actually a union
pub type zx_rrec_t = [u8; 64];

// Ports V2
#[repr(u32)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub enum zx_packet_type_t {
    ZX_PKT_TYPE_USER = 0,
    ZX_PKT_TYPE_SIGNAL_ONE = 1,
    ZX_PKT_TYPE_SIGNAL_REP = 2,
    ZX_PKT_TYPE_GUEST_BELL = 3,
    ZX_PKT_TYPE_GUEST_MEM = 4,
    ZX_PKT_TYPE_GUEST_IO = 5,
    #[doc(hidden)]
    __Nonexhaustive,
}

impl Default for zx_packet_type_t {
    fn default() -> Self {
        zx_packet_type_t::ZX_PKT_TYPE_USER
    }
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct zx_packet_signal_t {
    pub trigger: zx_signals_t,
    pub observed: zx_signals_t,
    pub count: u64,
}

pub const ZX_WAIT_ASYNC_ONCE: u32 = 0;

// Actually a union of different integer types, but this should be good enough.
pub type zx_packet_user_t = [u8; 32];

#[repr(C)]
#[derive(Debug, Default, Copy, Clone, Eq, PartialEq)]
pub struct zx_port_packet_t {
    pub key: u64,
    pub packet_type: zx_packet_type_t,
    pub status: i32,
    pub union: [u8; 32],
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_bell_t {
    pub addr: zx_gpaddr_t,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_io_t {
    pub port: u16,
    pub access_size: u8,
    pub input: bool,
    pub data: [u8; 4],
}

#[cfg(target_arch = "aarch64")]
#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_mem_t {
    pub addr: zx_gpaddr_t,
    pub access_size: u8,
    pub sign_extend: bool,
    pub xt: u8,
    pub read: bool,
    pub data: u64,
}

pub const X86_MAX_INST_LEN: usize = 15;

#[cfg(target_arch = "x86_64")]
#[repr(C)]
#[derive(Debug, Copy, Clone, Eq, PartialEq)]
pub struct zx_packet_guest_mem_t {
    pub addr: zx_gpaddr_t,
    pub inst_len: u8,
    pub inst_buf: [u8; X86_MAX_INST_LEN],
    pub default_operand_size: u8,
}

multiconst!(zx_object_info_topic_t, [
    ZX_INFO_NONE                       = 0;
    ZX_INFO_HANDLE_VALID               = 1;
    ZX_INFO_HANDLE_BASIC               = 2;  // zx_info_handle_basic_t[1]
    ZX_INFO_PROCESS                    = 3;  // zx_info_process_t[1]
    ZX_INFO_PROCESS_THREADS            = 4;  // zx_koid_t[n]
    ZX_INFO_VMAR                       = 7;  // zx_info_vmar_t[1]
    ZX_INFO_JOB_CHILDREN               = 8;  // zx_koid_t[n]
    ZX_INFO_JOB_PROCESSES              = 9;  // zx_koid_t[n]
    ZX_INFO_THREAD                     = 10; // zx_info_thread_t[1]
    ZX_INFO_THREAD_EXCEPTION_REPORT    = 11; // zx_exception_report_t[1]
    ZX_INFO_TASK_STATS                 = 12; // zx_info_task_stats_t[1]
    ZX_INFO_PROCESS_MAPS               = 13; // zx_info_maps_t[n]
    ZX_INFO_PROCESS_VMOS               = 14; // zx_info_vmo_t[n]
    ZX_INFO_THREAD_STATS               = 15; // zx_info_thread_stats_t[1]
    ZX_INFO_CPU_STATS                  = 16; // zx_info_cpu_stats_t[n]
    ZX_INFO_KMEM_STATS                 = 17; // zx_info_kmem_stats_t[1]
    ZX_INFO_RESOURCE                   = 18; // zx_info_resource_t[1]
    ZX_INFO_HANDLE_COUNT               = 19; // zx_info_handle_count_t[1]
    ZX_INFO_BTI                        = 20; // zx_info_bti_t[1]
    ZX_INFO_PROCESS_HANDLE_STATS       = 21; // zx_info_process_handle_stats_t[1]
    ZX_INFO_SOCKET                     = 22; // zx_info_socket_t[1]
]);

#[repr(C)]
#[derive(Copy, Clone, Default)]
pub struct zx_info_handle_basic_t {
    pub koid: zx_koid_t,
    pub rights: zx_rights_t,
    pub type_: u32,
    pub related_koid: zx_koid_t,
    pub props: u32,
}

#[repr(C)]
#[derive(Debug, Copy, Clone, Default, Eq, PartialEq)]
pub struct zx_info_socket_t {
    pub options: u32,
    pub rx_buf_max: usize,
    pub rx_buf_size: usize,
    pub rx_buf_available: usize,
    pub tx_buf_max: usize,
    pub tx_buf_size: usize,
}

multiconst!(zx_guest_trap_t, [
    ZX_GUEST_TRAP_BELL = 0;
    ZX_GUEST_TRAP_MEM  = 1;
    ZX_GUEST_TRAP_IO   = 2;
]);

include!("definitions.rs");
