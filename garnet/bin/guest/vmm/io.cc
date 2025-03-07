// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/io.h"

#include <src/lib/fxl/logging.h>
#include <zircon/syscalls/hypervisor.h>
#include <lib/zx/port.h>

#include "garnet/bin/guest/vmm/guest.h"

static constexpr IoValue kBellValue = {};

IoMapping::IoMapping(uint32_t kind, zx_gpaddr_t base, size_t size,
                     zx_gpaddr_t off, IoHandler* handler)
    : kind_(kind),
      base_(base),
      size_(size),
      off_(off),
      handler_(handler),
      async_trap_(this) {}

zx_status_t IoMapping::SetTrap(Guest* guest, async_dispatcher_t* dispatcher) {
  if (kind_ == ZX_GUEST_TRAP_BELL) {
    return async_trap_.SetTrap(dispatcher, guest->object(), base_, size_);
  } else {
    uintptr_t key = reinterpret_cast<uintptr_t>(this);
    return guest->object().set_trap(kind_, base_, size_, zx::port(), key);
  }
}

void IoMapping::CallIoHandlerAsync(async_dispatcher_t* dispatcher,
                                   async::GuestBellTrapBase* trap,
                                   zx_status_t status,
                                   const zx_packet_guest_bell_t* bell) {
  FXL_CHECK(Write(bell->addr, kBellValue) == ZX_OK)
      << "Failed to handle async IO";
}
