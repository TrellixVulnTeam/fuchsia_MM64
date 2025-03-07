// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// These definitions are used for communication between the cpu-trace
// device driver and the kernel only.

#pragma once

#include <lib/zircon-internal/device/cpu-trace/perf-mon.h>
#include <lib/zircon-internal/device/cpu-trace/common-pm.h>

// MSRs

#define IPM_MSR_MASK(len, shift) (((1ULL << (len)) - 1) << (shift))

// Bits in the IA32_PERF_CAPABILITIES MSR.

#define IA32_PERF_CAPABILITIES_LBR_FORMAT_SHIFT (0)
#define IA32_PERF_CAPABILITIES_LBR_FORMAT_LEN   (6)
#define IA32_PERF_CAPABILITIES_LBR_FORMAT_MASK \
  IPM_MSR_MASK(IA32_PERF_CAPABILITIES_LBR_FORMAT_LEN, \
               IA32_PERF_CAPABILITIES_LBR_FORMAT_SHIFT)

#define IA32_PERF_CAPABILITIES_PEBS_TRAP_SHIFT (6)
#define IA32_PERF_CAPABILITIES_PEBS_TRAP_LEN   (1)
#define IA32_PERF_CAPABILITIES_PEBS_TRAP_MASK \
  IPM_MSR_MASK(IA32_PERF_CAPABILITIES_PEBS_TRAP_LEN, \
               IA32_PERF_CAPABILITIES_PEBS_TRAP_SHIFT)

#define IA32_PERF_CAPABILITIES_PEBS_SAVE_ARCH_REGS_SHIFT (7)
#define IA32_PERF_CAPABILITIES_PEBS_SAVE_ARCH_REGS_LEN   (1)
#define IA32_PERF_CAPABILITIES_PEBS_SAVE_ARCH_REGS_MASK \
  IPM_MSR_MASK(IA32_PERF_CAPABILITIES_PEBS_SAVE_ARCH_REGS_LEN, \
               IA32_PERF_CAPABILITIES_PEBS_SAVE_ARCH_REGS_SHIFT)

#define IA32_PERF_CAPABILITIES_PEBS_RECORD_FORMAT_SHIFT (8)
#define IA32_PERF_CAPABILITIES_PEBS_RECORD_FORMAT_LEN   (4)
#define IA32_PERF_CAPABILITIES_PEBS_RECORD_FORMAT_MASK \
  IPM_MSR_MASK(IA32_PERF_CAPABILITIES_PEBS_RECORD_FORMAT_LEN, \
               IA32_PERF_CAPABILITIES_PEBS_RECORD_FORMAT_SHIFT)

#define IA32_PERF_CAPABILITIES_FREEZE_WHILE_SMM_SHIFT (12)
#define IA32_PERF_CAPABILITIES_FREEZE_WHILE_SMM_LEN   (1)
#define IA32_PERF_CAPABILITIES_FREEZE_WHILE_SMM_MASK \
  IPM_MSR_MASK(IA32_PERF_CAPABILITIES_FREEZE_WHILE_SMM_LEN, \
               IA32_PERF_CAPABILITIES_FREEZE_SHILE_SMM_SHIFT)

// Bits in the IA32_PERFEVTSELx MSRs.

#define IA32_PERFEVTSEL_EVENT_SELECT_SHIFT (0)
#define IA32_PERFEVTSEL_EVENT_SELECT_LEN   (8)
#define IA32_PERFEVTSEL_EVENT_SELECT_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_EVENT_SELECT_LEN, IA32_PERFEVTSEL_EVENT_SELECT_SHIFT)

#define IA32_PERFEVTSEL_UMASK_SHIFT (8)
#define IA32_PERFEVTSEL_UMASK_LEN   (8)
#define IA32_PERFEVTSEL_UMASK_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_UMASK_LEN, IA32_PERFEVTSEL_UMASK_SHIFT)

#define IA32_PERFEVTSEL_USR_SHIFT (16)
#define IA32_PERFEVTSEL_USR_LEN   (1)
#define IA32_PERFEVTSEL_USR_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_USR_LEN, IA32_PERFEVTSEL_USR_SHIFT)

#define IA32_PERFEVTSEL_OS_SHIFT (17)
#define IA32_PERFEVTSEL_OS_LEN   (1)
#define IA32_PERFEVTSEL_OS_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_OS_LEN, IA32_PERFEVTSEL_OS_SHIFT)

#define IA32_PERFEVTSEL_E_SHIFT (18)
#define IA32_PERFEVTSEL_E_LEN   (1)
#define IA32_PERFEVTSEL_E_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_E_LEN, IA32_PERFEVTSEL_E_SHIFT)

#define IA32_PERFEVTSEL_PC_SHIFT (19)
#define IA32_PERFEVTSEL_PC_LEN   (1)
#define IA32_PERFEVTSEL_PC_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_PC_LEN, IA32_PERFEVTSEL_PC_SHIFT)

#define IA32_PERFEVTSEL_INT_SHIFT (20)
#define IA32_PERFEVTSEL_INT_LEN   (1)
#define IA32_PERFEVTSEL_INT_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_INT_LEN, IA32_PERFEVTSEL_INT_SHIFT)

#define IA32_PERFEVTSEL_ANY_SHIFT (21)
#define IA32_PERFEVTSEL_ANY_LEN   (1)
#define IA32_PERFEVTSEL_ANY_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_ANY_LEN, IA32_PERFEVTSEL_ANY_SHIFT)

#define IA32_PERFEVTSEL_EN_SHIFT (22)
#define IA32_PERFEVTSEL_EN_LEN   (1)
#define IA32_PERFEVTSEL_EN_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_EN_LEN, IA32_PERFEVTSEL_EN_SHIFT)

#define IA32_PERFEVTSEL_INV_SHIFT (23)
#define IA32_PERFEVTSEL_INV_LEN   (1)
#define IA32_PERFEVTSEL_INV_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_INV_LEN, IA32_PERFEVTSEL_INV_SHIFT)

#define IA32_PERFEVTSEL_CMASK_SHIFT (24)
#define IA32_PERFEVTSEL_CMASK_LEN   (8)
#define IA32_PERFEVTSEL_CMASK_MASK \
  IPM_MSR_MASK(IA32_PERFEVTSEL_CMASK_LEN, IA32_PERFEVTSEL_CMASK_SHIFT)

// Bits in the IA32_FIXED_CTR_CTRL MSR.

#define IA32_FIXED_CTR_CTRL_EN_SHIFT(ctr) (0 + (ctr) * 4)
#define IA32_FIXED_CTR_CTRL_EN_LEN        (2)
#define IA32_FIXED_CTR_CTRL_EN_MASK(ctr) \
  IPM_MSR_MASK(IA32_FIXED_CTR_CTRL_EN_LEN, IA32_FIXED_CTR_CTRL_EN_SHIFT(ctr))

#define IA32_FIXED_CTR_CTRL_ANY_SHIFT(ctr) (2 + (ctr) * 4)
#define IA32_FIXED_CTR_CTRL_ANY_LEN        (1)
#define IA32_FIXED_CTR_CTRL_ANY_MASK(ctr) \
  IPM_MSR_MASK(IA32_FIXED_CTR_CTRL_ANY_LEN, IA32_FIXED_CTR_CTRL_ANY_SHIFT(ctr))

#define IA32_FIXED_CTR_CTRL_PMI_SHIFT(ctr) (3 + (ctr) * 4)
#define IA32_FIXED_CTR_CTRL_PMI_LEN        (1)
#define IA32_FIXED_CTR_CTRL_PMI_MASK(ctr) \
  IPM_MSR_MASK(IA32_FIXED_CTR_CTRL_PMI_LEN, IA32_FIXED_CTR_CTRL_PMI_SHIFT(ctr))

// The IA32_PERF_GLOBAL_CTRL MSR.

#define IA32_PERF_GLOBAL_CTRL_PMC_EN_SHIFT(ctr) (ctr)
#define IA32_PERF_GLOBAL_CTRL_PMC_EN_LEN        (1)
#define IA32_PERF_GLOBAL_CTRL_PMC_EN_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_CTRL_PMC_EN_LEN, IA32_PERF_GLOBAL_CTRL_PMC_EN_SHIFT(ctr))

#define IA32_PERF_GLOBAL_CTRL_FIXED_EN_SHIFT(ctr) (32 + (ctr))
#define IA32_PERF_GLOBAL_CTRL_FIXED_EN_LEN        (1)
#define IA32_PERF_GLOBAL_CTRL_FIXED_EN_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_CTRL_FIXED_EN_LEN, IA32_PERF_GLOBAL_CTRL_FIXED_EN_SHIFT(ctr))

// Bits in the IA32_PERF_GLOBAL_STATUS MSR.
// Note: Use these values for IA32_PERF_GLOBAL_STATUS_RESET and
// IA32_PERF_GLOBAL_STATUS_SET too.

#define IA32_PERF_GLOBAL_STATUS_PMC_OVF_SHIFT(ctr) (ctr)
#define IA32_PERF_GLOBAL_STATUS_PMC_OVF_LEN        (1)
#define IA32_PERF_GLOBAL_STATUS_PMC_OVF_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_PMC_OVF_LEN, IA32_PERF_GLOBAL_STATUS_PMC_OVF_SHIFT(ctr))

#define IA32_PERF_GLOBAL_STATUS_FIXED_OVF_SHIFT(ctr) (32 + (ctr))
#define IA32_PERF_GLOBAL_STATUS_FIXED_OVF_LEN        (1)
#define IA32_PERF_GLOBAL_STATUS_FIXED_OVF_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_FIXED_OVF_LEN, IA32_PERF_GLOBAL_STATUS_FIXED_OVF_SHIFT(ctr))

#define IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_SHIFT (55)
#define IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_LEN, IA32_PERF_GLOBAL_STATUS_TRACE_TOPA_PMI_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_LBR_FRZ_SHIFT (58)
#define IA32_PERF_GLOBAL_STATUS_LBR_FRZ_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_LBR_FRZ_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_LBR_FRZ_LEN, IA32_PERF_GLOBAL_STATUS_LBR_FRZ_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_CTR_FRZ_SHIFT (59)
#define IA32_PERF_GLOBAL_STATUS_CTR_FRZ_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_CTR_FRZ_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_CTR_FRZ_LEN, IA32_PERF_GLOBAL_STATUS_CTR_FRZ_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_ASCI_SHIFT (60)
#define IA32_PERF_GLOBAL_STATUS_ASCI_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_ASCI_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_ASCI_LEN, IA32_PERF_GLOBAL_STATUS_ASCI_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_SHIFT (61)
#define IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_LEN, IA32_PERF_GLOBAL_STATUS_UNCORE_OVF_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_SHIFT (62)
#define IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_LEN, IA32_PERF_GLOBAL_STATUS_DS_BUFFER_OVF_SHIFT)

#define IA32_PERF_GLOBAL_STATUS_COND_CHGD_SHIFT (63)
#define IA32_PERF_GLOBAL_STATUS_COND_CHGD_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_COND_CHGD_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_COND_CHGD_LEN, IA32_PERF_GLOBAL_STATUS_COND_CHGD_SHIFT)

// Bits in the IA32_PERF_GLOBAL_INUSE MSR.

#define IA32_PERF_GLOBAL_STATUS_INUSE_PERFEVTSEL_SHIFT(ctr) (ctr)
#define IA32_PERF_GLOBAL_STATUS_INUSE_PERFEVTSEL_LEN        (1)
#define IA32_PERF_GLOBAL_STATUS_INUSE_PERFEVTSEL_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_INUSE_PERFEVTSEL_LEN, IA32_PERF_GLOBAL_STATUS_INUSE_PERFEVTSEL_SHIFT(ctr))

#define IA32_PERF_GLOBAL_STATUS_INUSE_FIXED_CTR_SHIFT(ctr) (32 + (ctr))
#define IA32_PERF_GLOBAL_STATUS_INUSE_FIXED_CTR_LEN        (1)
#define IA32_PERF_GLOBAL_STATUS_INUSE_FIXED_CTR_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_INUSE_FIXED_CTR_LEN, IA32_PERF_GLOBAL_STATUS_INUSE_FIXED_CTR_SHIFT(ctr))

#define IA32_PERF_GLOBAL_STATUS_INUSE_PMI_SHIFT (63)
#define IA32_PERF_GLOBAL_STATUS_INUSE_PMI_LEN   (1)
#define IA32_PERF_GLOBAL_STATUS_INUSE_PMI_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_STATUS_INUSE_PMI_LEN, IA32_PERF_GLOBAL_STATUS_INUSE_PMI_SHIFT)

// Bits in the IA32_PERF_GLOBAL_OVF_CTRL MSR.

#define IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_SHIFT(ctr) (0)
#define IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_LEN        (1)
#define IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_LEN, IA32_PERF_GLOBAL_OVF_CTRL_PMC_CLR_OVF_SHIFT(ctr))

#define IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_SHIFT(ctr) (32 + (ctr))
#define IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_LEN   (1)
#define IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_MASK(ctr) \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_LEN, IA32_PERF_GLOBAL_OVF_CTRL_FIXED_CTR_CLR_OVF_SHIFT(ctr))

#define IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_SHIFT (61)
#define IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_LEN   (1)
#define IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_LEN, IA32_PERF_GLOBAL_OVF_CTRL_UNCORE_CLR_OVF_SHIFT)

#define IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_SHIFT (62)
#define IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_LEN   (1)
#define IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_LEN, IA32_PERF_GLOBAL_OVF_CTRL_DS_BUFFER_CLR_OVF_SHIFT)

#define IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_SHIFT (63)
#define IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_LEN   (1)
#define IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_MASK \
  IPM_MSR_MASK(IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_LEN, IA32_PERF_GLOBAL_OVF_CTRL_CLR_COND_CHGD_SHIFT)

// Bits in the IA32_DEBUGCTL MSR.

#define IA32_DEBUGCTL_LBR_SHIFT (0)
#define IA32_DEBUGCTL_LBR_LEN   (1)
#define IA32_DEBUGCTL_LBR_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_LBR_LEN, IA32_DEBUGCTL_LBR_SHIFT)

#define IA32_DEBUGCTL_BTF_SHIFT (1)
#define IA32_DEBUGCTL_BTF_LEN   (1)
#define IA32_DEBUGCTL_BTF_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_BTF_LEN, IA32_DEBUGCTL_BTF_SHIFT)

#define IA32_DEBUGCTL_TR_SHIFT (6)
#define IA32_DEBUGCTL_TR_LEN   (1)
#define IA32_DEBUGCTL_TR_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_TR_LEN, IA32_DEBUGCTL_TR_SHIFT)

#define IA32_DEBUGCTL_BTS_SHIFT (7)
#define IA32_DEBUGCTL_BTS_LEN   (1)
#define IA32_DEBUGCTL_BTS_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_BTS_LEN, IA32_DEBUGCTL_BTS_SHIFT)

#define IA32_DEBUGCTL_BTINT_SHIFT (8)
#define IA32_DEBUGCTL_BTINT_LEN   (1)
#define IA32_DEBUGCTL_BTINT_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_BTINT_LEN, IA32_DEBUGCTL_BTINT_SHIFT)

#define IA32_DEBUGCTL_BTS_OFF_OS_SHIFT (9)
#define IA32_DEBUGCTL_BTS_OFF_OS_LEN   (1)
#define IA32_DEBUGCTL_BTS_OFF_OS_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_BTS_OFF_OS_LEN, IA32_DEBUGCTL_BTS_OFF_OS_SHIFT)

#define IA32_DEBUGCTL_BTS_OFF_USR_SHIFT (10)
#define IA32_DEBUGCTL_BTS_OFF_USR_LEN   (1)
#define IA32_DEBUGCTL_BTS_OFF_USR_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_BTS_OFF_USR_LEN, IA32_DEBUGCTL_BTS_OFF_USR_SHIFT)

#define IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_SHIFT (11)
#define IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_LEN   (1)
#define IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_LEN, IA32_DEBUGCTL_FREEZE_LBRS_ON_PMI_SHIFT)

#define IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_SHIFT (12)
#define IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_LEN   (1)
#define IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_LEN, IA32_DEBUGCTL_FREEZE_PERFMON_ON_PMI_SHIFT)

#define IA32_DEBUGCTL_FREEZE_WHILE_SMM_SHIFT (14)
#define IA32_DEBUGCTL_FREEZE_WHILE_SMM_LEN   (1)
#define IA32_DEBUGCTL_FREEZE_WHILE_SMM_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_FREEZE_WHILE_SMM_LEN, IA32_DEBUGCTL_FREEZE_WHILE_SMM_SHIFT)

#define IA32_DEBUGCTL_RTM_SHIFT (15)
#define IA32_DEBUGCTL_RTM_LEN   (1)
#define IA32_DEBUGCTL_RTM_MASK \
  IPM_MSR_MASK(IA32_DEBUGCTL_RTM_LEN, IA32_DEBUGCTL_RTM_SHIFT)

// Bits in the IA32_LBR_INFO_* MSRs

#define IA32_LBR_INFO_CYCLE_COUNT_SHIFT (0)
#define IA32_LBR_INFO_CYCLE_COUNT_LEN   (16)
#define IA32_LBR_INFO_CYCLE_COUNT_MASK \
  IPM_MSR_MASK(IA32_LBR_INFO_CYCLE_COUNT_LEN, IA32_LBR_INFO_CYCLE_COUNT_SHIFT)

#define IA32_LBR_INFO_TSX_ABORT_SHIFT (61)
#define IA32_LBR_INFO_TSX_ABORT_COUNT_LEN   (1)
#define IA32_LBR_INFO_TSX_ABORT_MASK \
  IPM_MSR_MASK(IA32_LBR_INFO_TSX_ABORT_LEN, IA32_LBR_INFO_TSX_ABORT_SHIFT)

#define IA32_LBR_INFO_IN_TSX_SHIFT (62)
#define IA32_LBR_INFO_IN_TSX_LEN   (1)
#define IA32_LBR_INFO_IN_TSX_MASK \
  IPM_MSR_MASK(IA32_LBR_INFO_IN_TSX_LEN, IA32_LBR_INFO_IN_TSX_SHIFT)

#define IA32_LBR_INFO_MISPRED_SHIFT (63)
#define IA32_LBR_INFO_MISPRED_LEN   (1)
#define IA32_LBR_INFO_MISPRED_MASK \
  IPM_MSR_MASK(IA32_LBR_INFO_MISPRED_LEN, IA32_LBR_INFO_MISPRED_SHIFT)

// Bits in the IA32_LBR_TOS MSR

#define IA32_LBR_TOS_TOS_SHIFT (0)
#define IA32_LBR_TOS_TOS_LEN   (5)
#define IA32_LBR_TOS_TOS_MASK \
  IPM_MSR_MASK(IA32_LBR_TOS_TOS_LEN, IA32_LBR_TOS_TOS_SHIFT)

// maximum number of programmable counters
// These are all "events" in our parlance, but on Intel these are all
// counter events, so we use the more specific term "counter".
#define IPM_MAX_PROGRAMMABLE_COUNTERS 8

// maximum number of fixed-use counters
// These are all "events" in our parlance, but on Intel these are all
// counter events, so we use the more specific term "counter".
#define IPM_MAX_FIXED_COUNTERS 3

// misc events
// Some of these events are counters, but not all of them are, so we use
// the more generic term "events".
// See, e.g., skylake-misc-events.inc.
// This isn't the total number of events, it's just the maximum
// we can collect at one time.
#define IPM_MAX_MISC_EVENTS 16

///////////////////////////////////////////////////////////////////////////////

// These structs are used for communication between the device driver
// and the kernel.

namespace perfmon {

// Properties of perf data collection on this system.
struct X86PmuProperties : public PmuCommonProperties {
    // The PERF_CAPABILITIES MSR.
    uint64_t perf_capabilities;
    // The size of the LBR (Last Branch Record) stack.
    // A value of zero means LBR is not supported. This may be zero even if
    // LBR is supported by the chip because the device is not recognized as
    // supporting it.
    uint32_t lbr_stack_size;
};

// PMU configuration.
struct X86PmuConfig {
    // IA32_PERF_GLOBAL_CTRL
    uint64_t global_ctrl;

    // IA32_FIXED_CTR_CTRL
    uint64_t fixed_ctrl;

    // IA32_DEBUGCTL
    uint64_t debug_ctrl;

    // The id of the timebase counter to use or PERFMON_EVENT_ID_NONE.
    // A "timebase counter" is used to trigger collection of data from other
    // events. In other words it sets the sample rate for those events.
    // If zero, then no timebase is in use: Each event must trigger its own
    // data collection. Otherwise the value is the id of the timebase counter
    // to use, which must appear in one of |programmable_ids| or |fixed_ids|.
    PmuEventId timebase_event;

    // Ids of each event. These values are written to the trace buffer to
    // identify the event.
    // The used entries begin at index zero and are consecutive (no holes).
    PmuEventId fixed_events[IPM_MAX_FIXED_COUNTERS];
    PmuEventId programmable_events[IPM_MAX_PROGRAMMABLE_COUNTERS];
    // Ids of other h/w events to collect data for.
    PmuEventId misc_events[IPM_MAX_MISC_EVENTS];

    // Initial value of each counter.
    // The "misc" counters currently do not support initial values.
    uint64_t fixed_initial_value[IPM_MAX_FIXED_COUNTERS];
    uint64_t programmable_initial_value[IPM_MAX_PROGRAMMABLE_COUNTERS];

    // Flags for each counter.
    // The values are |perfmon::kPmuConfigFlag*|.
    uint32_t fixed_flags[IPM_MAX_FIXED_COUNTERS];
    uint32_t programmable_flags[IPM_MAX_PROGRAMMABLE_COUNTERS];
    uint32_t misc_flags[IPM_MAX_MISC_EVENTS];

    // IA32_PERFEVTSEL_*
    uint64_t programmable_hw_events[IPM_MAX_PROGRAMMABLE_COUNTERS];
};

} // namespace perfmon

///////////////////////////////////////////////////////////////////////////////

// Flags for the events in Intel *-pm-events.inc.
// See for example Intel Volume 3, Table 19-3.
// "Non-Architectural Performance Events of the Processor Core Supported by
// Skylake Microarchitecture and Kaby Lake Microarchitecture"

// Flags for non-architectural counters
// CounterMask values
#define IPM_REG_FLAG_CMSK_MASK 0xff
#define IPM_REG_FLAG_CMSK1   1
#define IPM_REG_FLAG_CMSK2   2
#define IPM_REG_FLAG_CMSK4   4
#define IPM_REG_FLAG_CMSK5   5
#define IPM_REG_FLAG_CMSK6   6
#define IPM_REG_FLAG_CMSK8   8
#define IPM_REG_FLAG_CMSK10 10
#define IPM_REG_FLAG_CMSK12 12
#define IPM_REG_FLAG_CMSK16 16
#define IPM_REG_FLAG_CMSK20 20
// AnyThread = 1 required
#define IPM_REG_FLAG_ANYT   0x100
// Invert = 1 required
#define IPM_REG_FLAG_INV    0x200
// Edge = 1 required
#define IPM_REG_FLAG_EDG    0x400
// Also supports PEBS and DataLA
#define IPM_REG_FLAG_PSDLA  0x800
// Also supports PEBS
#define IPM_REG_FLAG_PS     0x1000

// Extra flags

// Architectural event
#define IPM_REG_FLAG_ARCH 0x10000

// Fixed counters
#define IPM_REG_FLAG_FIXED_MASK 0xf00000
#define IPM_REG_FLAG_FIXED0     0x100000
#define IPM_REG_FLAG_FIXED1     0x200000
#define IPM_REG_FLAG_FIXED2     0x300000

// Flags for misc registers

// The register consists of a set of fields (not a counter).
// Just print in hex.
#define IPM_MISC_REG_FLAG_FIELDS (1u << 0)
// The value uses a non-standard encoding.
// Just print in hex.
#define IPM_MISC_REG_FLAG_RAW (1u << 1)
