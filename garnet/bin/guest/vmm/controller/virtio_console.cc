// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/controller/virtio_console.h"

#include <lib/svc/cpp/services.h>

static constexpr char kVirtioConsoleUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_console#meta/virtio_console.cmx";

VirtioConsole::VirtioConsole(const PhysMem& phys_mem)
    : VirtioComponentDevice(
          phys_mem, 0 /* device_features */,
          fit::bind_member(this, &VirtioConsole::ConfigureQueue),
          fit::bind_member(this, &VirtioConsole::Ready)) {
  config_.max_nr_ports = kVirtioConsoleMaxNumPorts;
}

zx_status_t VirtioConsole::Start(const zx::guest& guest, zx::socket socket,
                                 fuchsia::sys::Launcher* launcher,
                                 async_dispatcher_t* dispatcher) {
  component::Services services;
  fuchsia::sys::LaunchInfo launch_info{
      .url = kVirtioConsoleUrl,
      .directory_request = services.NewRequest(),
  };
  launcher->CreateComponent(std::move(launch_info), controller_.NewRequest());
  services.ConnectToService(console_.NewRequest());

  fuchsia::guest::device::StartInfo start_info;
  zx_status_t status = PrepStart(guest, dispatcher, &start_info);
  if (status != ZX_OK) {
    return status;
  }
  return console_->Start(std::move(start_info), std::move(socket));
}

zx_status_t VirtioConsole::ConfigureQueue(uint16_t queue, uint16_t size,
                                          zx_gpaddr_t desc, zx_gpaddr_t avail,
                                          zx_gpaddr_t used) {
  return console_->ConfigureQueue(queue, size, desc, avail, used);
}

zx_status_t VirtioConsole::Ready(uint32_t negotiated_features) {
  return console_->Ready(negotiated_features);
}
