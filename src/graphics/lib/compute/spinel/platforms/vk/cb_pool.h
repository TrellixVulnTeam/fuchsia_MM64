// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

//
//
//

#include <vulkan/vulkan_core.h>

//
//
//

struct spn_device;

//
//
//

void
spn_device_cb_pool_create(struct spn_device * const device);

void
spn_device_cb_pool_dispose(struct spn_device * const device);

//
//
//

VkCommandBuffer
spn_device_cb_pool_acquire(struct spn_device * const device);

void
spn_device_cb_pool_release(struct spn_device * const device,
                           VkCommandBuffer     const cb);

//
//
//
