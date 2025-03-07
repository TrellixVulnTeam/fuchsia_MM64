// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <fbl/ref_ptr.h>
#include <lib/async/dispatcher.h>
#include <lib/zx/channel.h>
#include <zircon/types.h>

namespace devmgr {

struct Device;
struct Devnode;

// Initializes a devfs directory from |device|.
void devfs_init(const fbl::RefPtr<Device>& device, async_dispatcher_t* dispatcher);

// Watches the devfs directory |dn|, and sends events to |watcher|.
zx_status_t devfs_watch(Devnode* dn, zx::channel h, uint32_t mask);

// Borrows the channel connected to the root of devfs.
zx::unowned_channel devfs_root_borrow();

// Clones the channel connected to the root of devfs.
zx::channel devfs_root_clone();

zx_status_t devfs_publish(const fbl::RefPtr<Device>& parent, const fbl::RefPtr<Device>& dev);
void devfs_unpublish(Device* dev);
void devfs_advertise(const fbl::RefPtr<Device>& dev);
void devfs_advertise_modified(const fbl::RefPtr<Device>& dev);
zx_status_t devfs_connect(const Device* dev, zx::channel client_remote);

} // namespace devmgr
