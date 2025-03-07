// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "platform-proxy.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <ddk/binding.h>
#include <ddk/debug.h>
#include <fbl/algorithm.h>
#include <fbl/auto_lock.h>

#include <utility>

#include "platform-proxy-device.h"

namespace platform_bus {

void PlatformProxy::DdkRelease() {
    // Delete ourselves if the devmgr held the last reference to us.
    if (Release()) {
        delete this;
    }
}

zx_status_t PlatformProxy::Rpc(uint32_t device_id, const platform_proxy_req_t* req,
                               size_t req_length, platform_proxy_rsp_t* resp,
                               size_t resp_length, const zx_handle_t* in_handles,
                               size_t in_handle_count, zx_handle_t* out_handles,
                               size_t out_handle_count, size_t* out_actual) {
    uint32_t resp_size, handle_count;

    // We require the client to pass us the device_id and we set here as a precaution
    // against the code above forgetting to set it.
    const_cast<platform_proxy_req_t*>(req)->device_id = device_id;

    zx_channel_call_args_t args = {
        .wr_bytes = req,
        .wr_handles = in_handles,
        .rd_bytes = resp,
        .rd_handles = out_handles,
        .wr_num_bytes = static_cast<uint32_t>(req_length),
        .wr_num_handles = static_cast<uint32_t>(in_handle_count),
        .rd_num_bytes = static_cast<uint32_t>(resp_length),
        .rd_num_handles = static_cast<uint32_t>(out_handle_count),
    };
    auto status = rpc_channel_.call(0, zx::time::infinite(), &args, &resp_size, &handle_count);
    if (status != ZX_OK) {
        // This is a fairly serious error; subsequent requests are very likely
        // to also fail.
        //
        // TODO(ZX-3833): Make this less likely and/or handle differently.
        zxlogf(ERROR, "PlatformProxy::Rpc rpc_channel_.call failed - status: %d\n", status);
        return status;
    }

    status = resp->status;

    if (status == ZX_OK && resp_size < sizeof(*resp)) {
        zxlogf(ERROR, "PlatformProxy::Rpc resp_size too short: %u\n", resp_size);
        status = ZX_ERR_INTERNAL;
        goto fail;
    } else if (status == ZX_OK && handle_count != out_handle_count) {
        zxlogf(ERROR, "PlatformProxy::Rpc handle count %u expected %zu\n", handle_count,
               out_handle_count);
        status = ZX_ERR_INTERNAL;
        goto fail;
    }

    if (out_actual) {
        *out_actual = resp_size;
    }

fail:
    if (status != ZX_OK) {
        for (uint32_t i = 0; i < handle_count; i++) {
            zx_handle_close(out_handles[i]);
        }
    }
    return status;
}

zx_status_t PlatformProxy::Create(void* ctx, zx_device_t* parent, const char* name,
                                  const char* args, zx_handle_t rpc_channel) {
    fbl::AllocChecker ac;

    auto proxy = fbl::MakeRefCountedChecked<PlatformProxy>(&ac, parent, rpc_channel);
    if (!ac.check()) {
        return ZX_ERR_NO_MEMORY;
    }

    return proxy->Init(parent);
}

zx_status_t PlatformProxy::Init(zx_device_t* parent) {
    return ProxyDevice::CreateRoot(parent, fbl::RefPtr<PlatformProxy>(this));
}

static zx_driver_ops_t proxy_driver_ops = [](){
    zx_driver_ops_t ops = {};
    ops.version = DRIVER_OPS_VERSION;
    ops.create = PlatformProxy::Create;
    return ops;
}();

} // namespace platform_bus

ZIRCON_DRIVER_BEGIN(platform_bus_proxy, platform_bus::proxy_driver_ops, "zircon", "0.1", 1)
    BI_ABORT_IF_AUTOBIND,
ZIRCON_DRIVER_END(platform_bus_proxy)
