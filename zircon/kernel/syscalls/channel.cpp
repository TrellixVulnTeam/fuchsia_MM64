// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <err.h>
#include <inttypes.h>
#include <trace.h>

#include <lib/counters.h>
#include <lib/ktrace.h>

#include <object/channel_dispatcher.h>
#include <object/handle.h>
#include <object/message_packet.h>
#include <object/process_dispatcher.h>
#include <object/user_handles.h>

#include <zircon/syscalls/policy.h>
#include <zircon/types.h>

#include <fbl/algorithm.h>
#include <fbl/auto_call.h>
#include <fbl/ref_ptr.h>

#include "priv.h"

#define LOCAL_TRACE 0

KCOUNTER(channel_msg_0_bytes,   "channel.bytes.0")
KCOUNTER(channel_msg_64_bytes,  "channel.bytes.64")
KCOUNTER(channel_msg_256_bytes, "channel.bytes.256")
KCOUNTER(channel_msg_1k_bytes,  "channel.bytes.1k")
KCOUNTER(channel_msg_4k_bytes,  "channel.bytes.4k")
KCOUNTER(channel_msg_16k_bytes, "channel.bytes.16k")
KCOUNTER(channel_msg_64k_bytes, "channel.bytes.64k")
KCOUNTER(channel_msg_received,  "channel.messages")

static void record_recv_msg_sz(uint32_t size) {
    kcounter_add(channel_msg_received, 1);

    switch(size) {
        case     0          : kcounter_add(channel_msg_0_bytes, 1);   break;
        case     1 ...    64: kcounter_add(channel_msg_64_bytes, 1);  break;
        case    65 ...   256: kcounter_add(channel_msg_256_bytes, 1); break;
        case   257 ...  1024: kcounter_add(channel_msg_1k_bytes, 1);  break;
        case  1025 ...  4096: kcounter_add(channel_msg_4k_bytes, 1);  break;
        case  4097 ... 16384: kcounter_add(channel_msg_16k_bytes, 1); break;
        case 16385 ... 65536: kcounter_add(channel_msg_64k_bytes, 1); break;
    }
}

// zx_status_t zx_channel_create
zx_status_t sys_channel_create(uint32_t options,
                               user_out_handle* out0, user_out_handle* out1) {
    if (options != 0u)
        return ZX_ERR_INVALID_ARGS;

    auto up = ProcessDispatcher::GetCurrent();
    zx_status_t res = up->QueryBasicPolicy(ZX_POL_NEW_CHANNEL);
    if (res != ZX_OK)
        return res;

    KernelHandle<ChannelDispatcher> handle0, handle1;
    zx_rights_t rights;
    zx_status_t result = ChannelDispatcher::Create(&handle0, &handle1, &rights);
    if (result != ZX_OK)
        return result;

    uint64_t id0 = handle0.dispatcher()->get_koid();
    uint64_t id1 = handle1.dispatcher()->get_koid();

    result = out0->make(ktl::move(handle0), rights);
    if (result == ZX_OK)
        result = out1->make(ktl::move(handle1), rights);
    if (result == ZX_OK)
        ktrace(TAG_CHANNEL_CREATE, (uint32_t)id0, (uint32_t)id1, options, 0);
    return result;
}

static void MapHandleToValue(
    ProcessDispatcher* up, const Handle* handle, uint32_t* out) {
    *out = up->MapHandleToValue(handle);
}

static void MapHandleToValue(
    ProcessDispatcher* up, const Handle* handle, zx_handle_info_t* out) {
    out->handle = up->MapHandleToValue(handle);
    out->type = handle->dispatcher()->get_type();
    out->rights = handle->rights();
    out->unused = 0;
}

template <typename HandleT>
static void msg_get_handles(ProcessDispatcher* up, MessagePacket* msg,
                            user_out_ptr<HandleT> handles, uint32_t num_handles) {
    Handle* const* handle_list = msg->handles();
    msg->set_owns_handles(false);

    HandleT hvs[kMaxMessageHandles];
    for (size_t i = 0; i < num_handles; ++i) {
        MapHandleToValue(up, handle_list[i], &hvs[i]);
    }

    handles.copy_array_to_user(hvs, num_handles);

    for (size_t i = 0; i < num_handles; ++i) {
        if (handle_list[i]->dispatcher()->is_waitable())
            handle_list[i]->dispatcher()->Cancel(handle_list[i]);
        HandleOwner handle(handle_list[i]);
        // TODO(ZX-969): This takes a lock per call. Consider doing these in a batch.
        up->AddHandle(ktl::move(handle));
    }
}

template <typename HandleInfoT>
static zx_status_t channel_read(zx_handle_t handle_value, uint32_t options,
                             user_out_ptr<void> bytes, user_out_ptr<HandleInfoT> handles,
                             uint32_t num_bytes, uint32_t num_handles,
                             user_out_ptr<uint32_t> actual_bytes,
                             user_out_ptr<uint32_t> actual_handles) {
    LTRACEF("handle %x bytes %p num_bytes %p handles %p num_handles %p",
            handle_value, bytes.get(), actual_bytes.get(), handles.get(), actual_handles.get());

    auto up = ProcessDispatcher::GetCurrent();

    fbl::RefPtr<ChannelDispatcher> channel;
    zx_status_t result = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_READ, &channel);
    if (result != ZX_OK)
        return result;

    // Currently MAY_DISCARD is the only allowable option.
    if (options & ~ZX_CHANNEL_READ_MAY_DISCARD)
        return ZX_ERR_NOT_SUPPORTED;

    MessagePacketPtr msg;
    result = channel->Read(up->get_koid(), &num_bytes, &num_handles, &msg,
                           options & ZX_CHANNEL_READ_MAY_DISCARD);
    if (result != ZX_OK && result != ZX_ERR_BUFFER_TOO_SMALL)
        return result;

    // On ZX_ERR_BUFFER_TOO_SMALL, Read() gives us the size of the next message (which remains
    // unconsumed, unless |options| has ZX_CHANNEL_READ_MAY_DISCARD set).
    if (actual_bytes) {
        zx_status_t status = actual_bytes.copy_to_user(num_bytes);
        if (status != ZX_OK)
            return status;
    }

    if (actual_handles) {
        zx_status_t status = actual_handles.copy_to_user(num_handles);
        if (status != ZX_OK)
            return status;
    }
    if (result == ZX_ERR_BUFFER_TOO_SMALL)
        return result;

    if (num_bytes > 0u) {
        if (msg->CopyDataTo(bytes) != ZX_OK)
            return ZX_ERR_INVALID_ARGS;
    }

    // The documented public API states that that writing to the handles buffer
    // must happen after writing to the data buffer.
    if (num_handles > 0u) {
        msg_get_handles(up, msg.get(), handles, num_handles);
    }

    record_recv_msg_sz(num_bytes);
    ktrace(TAG_CHANNEL_READ, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return result;
}

// zx_status_t zx_channel_read
zx_status_t sys_channel_read(zx_handle_t handle_value, uint32_t options,
                             user_out_ptr<void> bytes,
                             user_out_ptr<zx_handle_t> handle_info,
                             uint32_t num_bytes, uint32_t num_handles,
                             user_out_ptr<uint32_t> actual_bytes,
                             user_out_ptr<uint32_t> actual_handles) {
    return channel_read(handle_value, options,
        bytes, handle_info, num_bytes, num_handles, actual_bytes, actual_handles);
}

// zx_status_t zx_channel_read_etc
zx_status_t sys_channel_read_etc(zx_handle_t handle_value, uint32_t options,
                             user_out_ptr<void> bytes,
                             user_out_ptr<zx_handle_info_t> handle_info,
                             uint32_t num_bytes, uint32_t num_handles,
                             user_out_ptr<uint32_t> actual_bytes,
                             user_out_ptr<uint32_t> actual_handles) {
    return channel_read(handle_value, options,
        bytes, handle_info, num_bytes, num_handles, actual_bytes, actual_handles);
}

static zx_status_t channel_read_out(ProcessDispatcher* up,
                                    MessagePacketPtr reply,
                                    zx_channel_call_args_t* args,
                                    user_out_ptr<uint32_t> actual_bytes,
                                    user_out_ptr<uint32_t> actual_handles) {
    uint32_t num_bytes = reply->data_size();
    uint32_t num_handles = reply->num_handles();

    if ((args->rd_num_bytes < num_bytes) || (args->rd_num_handles < num_handles)) {
        return ZX_ERR_BUFFER_TOO_SMALL;
    }

    zx_status_t status = actual_bytes.copy_to_user(num_bytes);
    if (status != ZX_OK)
        return status;
    status = actual_handles.copy_to_user(num_handles);
    if (status != ZX_OK)
        return status;

    if (num_bytes > 0u) {
        if (reply->CopyDataTo(make_user_out_ptr(args->rd_bytes)) != ZX_OK) {
            return ZX_ERR_INVALID_ARGS;
        }
    }

    if (num_handles > 0u) {
        msg_get_handles(up, reply.get(), make_user_out_ptr(args->rd_handles), num_handles);
    }
    return ZX_OK;
}

static zx_status_t channel_call_epilogue(ProcessDispatcher* up,
                                         MessagePacketPtr reply,
                                         zx_channel_call_args_t* args,
                                         user_out_ptr<uint32_t> actual_bytes,
                                         user_out_ptr<uint32_t> actual_handles) {
    auto bytes = reply? reply->data_size() : 0u;
    zx_status_t status = channel_read_out(up, ktl::move(reply), args, actual_bytes, actual_handles);
    if (status != ZX_OK)
        return status;
    record_recv_msg_sz(bytes);
    return ZX_OK;
}

static zx_status_t source_handle_checks_locked(const Handle* handle, const Dispatcher* channel) {
    if (!handle)
        return ZX_ERR_BAD_HANDLE;
    if (!handle->HasRights(ZX_RIGHT_TRANSFER))
        return ZX_ERR_ACCESS_DENIED;
    if (handle->dispatcher().get() == channel)
        return ZX_ERR_NOT_SUPPORTED;
    return ZX_OK;
}

static zx_status_t get_handle_for_message_locked(
    ProcessDispatcher* process, const Dispatcher* channel,
    zx_handle_t handle_val, Handle** raw_handle) TA_REQ(process->handle_table_lock()) {
    HandleOwner handle = process->RemoveHandleLocked(handle_val);
    auto status = source_handle_checks_locked(handle.get(), channel);
    if (status != ZX_OK)
        return status;

    *raw_handle = handle.release();
    return ZX_OK;
}

// Consumes all handles whether it succeeds or not.
template <typename UserHandles>
static zx_status_t msg_put_handles(ProcessDispatcher* up, MessagePacket* msg,
                                   UserHandles user_handles,
                                   uint32_t num_handles,
                                   Dispatcher* channel) {
    DEBUG_ASSERT(num_handles <= kMaxMessageHandles); // This must be checked before calling.

    typename UserHandles::ValueType handles[kMaxMessageHandles] = {};

    zx_status_t status = get_user_handles(user_handles, 0, num_handles, handles);
    if (status != ZX_OK)
        return status;

    {
        Guard<BrwLock, BrwLock::Writer> guard{up->handle_table_lock()};

        for (size_t ix = 0; ix != num_handles; ++ix) {
            Handle* handle = nullptr;
            auto inner_status = get_handle_for_message_locked(up, channel, handles[ix], &handle);
            if ((inner_status != ZX_OK)  && (status == ZX_OK)) {
                // Latch the first error encountered. It will be what the function returns.
                status = inner_status;
            }

            msg->mutable_handles()[ix] = handle;
        }
    }

    msg->set_owns_handles(true);
    return status;
}

template <typename UserHandles>
static zx_status_t channel_write(zx_handle_t handle_value, uint32_t options,
                                 user_in_ptr<const void> user_bytes, uint32_t num_bytes,
                                 UserHandles user_handles, uint32_t num_handles) {
    LTRACEF("handle %x bytes %p num_bytes %u handles %p num_handles %u options 0x%x\n",
        handle_value, user_bytes.get(), num_bytes, user_handles.get(), num_handles, options);

    auto up = ProcessDispatcher::GetCurrent();

    auto cleanup = fbl::MakeAutoCall([&]() { RemoveUserHandles(user_handles, num_handles, up); });

    if (options != 0u) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<ChannelDispatcher> channel;
    zx_status_t status = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_WRITE, &channel);
    if (status != ZX_OK) {
        return status;
    }

    MessagePacketPtr msg;
    status = MessagePacket::Create(user_bytes, num_bytes, num_handles, &msg);
    if (status != ZX_OK) {
        return status;
    }

    // msg_put_handles() always consumes all handles (or there are zero handles,
    // and so there's nothing to be done).
    cleanup.cancel();

    if (num_handles > 0u) {
        status = msg_put_handles(up, msg.get(), user_handles, num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (status != ZX_OK)
            return status;
    }

    status = channel->Write(up->get_koid(), ktl::move(msg));
    if (status != ZX_OK)
        return status;

    ktrace(TAG_CHANNEL_WRITE, (uint32_t)channel->get_koid(), num_bytes, num_handles, 0);
    return ZX_OK;
}

// zx_status_t zx_channel_write
zx_status_t sys_channel_write(zx_handle_t handle_value, uint32_t options,
                              user_in_ptr<const void> user_bytes, uint32_t num_bytes,
                              user_in_ptr<const zx_handle_t> user_handles,
                              uint32_t num_handles) {
    LTRACEF("handle %x bytes %p num_bytes %u handles %p num_handles %u options 0x%x\n",
        handle_value, user_bytes.get(), num_bytes, user_handles.get(), num_handles, options);

    return channel_write(
        handle_value, options, user_bytes, num_bytes, user_handles, num_handles);
}

// zx_status_t zx_channel_write_etc
zx_status_t sys_channel_write_etc(zx_handle_t handle_value, uint32_t options,
                                  user_in_ptr<const void> user_bytes, uint32_t num_bytes,
                                  user_inout_ptr<zx_handle_disposition_t> user_handles,
                                  uint32_t num_handles) {
    // TODO(cpu): Implement.
    return ZX_ERR_NOT_SUPPORTED;
}

// zx_status_t zx_channel_call_noretry
zx_status_t sys_channel_call_noretry(zx_handle_t handle_value, uint32_t options,
                                     zx_time_t deadline,
                                     user_in_ptr<const zx_channel_call_args_t> user_args,
                                     user_out_ptr<uint32_t> actual_bytes,
                                     user_out_ptr<uint32_t> actual_handles) {
    zx_channel_call_args_t args;

    zx_status_t status = user_args.copy_from_user(&args);
    if (status != ZX_OK)
        return status;

    user_in_ptr<const void> user_bytes = make_user_in_ptr(args.wr_bytes);
    user_in_ptr<const zx_handle_t> user_handles = make_user_in_ptr(args.wr_handles);

    uint32_t num_bytes = args.wr_num_bytes;
    uint32_t num_handles = args.wr_num_handles;

    auto up = ProcessDispatcher::GetCurrent();

    auto cleanup = fbl::MakeAutoCall([&]() { RemoveUserHandles(user_handles, num_handles, up); });

    if (options || num_bytes < sizeof(zx_txid_t)) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<ChannelDispatcher> channel;
    status = up->GetDispatcherWithRights(handle_value, ZX_RIGHT_WRITE | ZX_RIGHT_READ, &channel);
    if (status != ZX_OK) {
        return status;
    }

    // Prepare a MessagePacket for writing
    MessagePacketPtr msg;
    status = MessagePacket::Create(user_bytes, num_bytes, num_handles, &msg);
    if (status != ZX_OK) {
        return status;
    }

    // msg_put_handles() always consumes all handles (or there are zero handles,
    // and so there's nothing to be done).
    cleanup.cancel();

    if (num_handles > 0u) {
        status = msg_put_handles(up, msg.get(), user_handles, num_handles,
                                 static_cast<Dispatcher*>(channel.get()));
        if (status)
            return status;
    }

    // TODO(ZX-970): ktrace channel calls; maybe two traces, maybe with txid.

    // Write message and wait for reply, deadline, or cancellation
    MessagePacketPtr reply;
    status = channel->Call(up->get_koid(), ktl::move(msg), deadline, &reply);
    if (status != ZX_OK)
        return status;
    return channel_call_epilogue(up, ktl::move(reply), &args, actual_bytes, actual_handles);
}

// zx_status_t zx_channel_call_finish
zx_status_t sys_channel_call_finish(zx_time_t deadline,
                                    user_in_ptr<const zx_channel_call_args_t> user_args,
                                    user_out_ptr<uint32_t> actual_bytes,
                                    user_out_ptr<uint32_t> actual_handles) {

    zx_channel_call_args_t args;
    zx_status_t status = user_args.copy_from_user(&args);
    if (status != ZX_OK)
        return status;

    auto up = ProcessDispatcher::GetCurrent();

    auto waiter = ThreadDispatcher::GetCurrent()->GetMessageWaiter();
    fbl::RefPtr<ChannelDispatcher> channel = waiter->get_channel();
    if (!channel)
        return ZX_ERR_BAD_STATE;

    const TimerSlack slack = up->GetTimerSlackPolicy();
    const Deadline slackDeadline(deadline, slack);
    MessagePacketPtr reply;
    status = channel->ResumeInterruptedCall(waiter, slackDeadline, &reply);
    if (status != ZX_OK)
        return status;
    return channel_call_epilogue(up, ktl::move(reply), &args, actual_bytes, actual_handles);

}
