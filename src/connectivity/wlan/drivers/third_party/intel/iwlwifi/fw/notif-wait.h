/******************************************************************************
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2015 - 2017 Intel Deutschland GmbH
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_NOTIF_WAIT_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_NOTIF_WAIT_H_

#include <threads.h>
#include <zircon/listnode.h>

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-trans.h"

// NEEDS_PORTING: seems that this is exactly what sync_completion_t is doing.

struct iwl_notif_wait_data {
    struct list_node notif_waits;
    mtx_t notif_wait_lock;
#if 0   // NEEDS_PORTING
    wait_queue_head_t notif_waitq;
#endif  // NEEDS_PORTING
};

#define MAX_NOTIF_CMDS 5

/**
 * struct iwl_notification_wait - notification wait entry
 * @list: list head for global list
 * @fn: Function called with the notification. If the function
 *  returns true, the wait is over, if it returns false then
 *  the waiter stays blocked. If no function is given, any
 *  of the listed commands will unblock the waiter.
 * @cmds: command IDs
 * @n_cmds: number of command IDs
 * @triggered: waiter should be woken up
 * @aborted: wait was aborted
 *
 * This structure is not used directly, to wait for a
 * notification declare it on the stack, and call
 * iwl_init_notification_wait() with appropriate
 * parameters. Then do whatever will cause the ucode
 * to notify the driver, and to wait for that then
 * call iwl_wait_notification().
 *
 * Each notification is one-shot. If at some point we
 * need to support multi-shot notifications (which
 * can't be allocated on the stack) we need to modify
 * the code for them.
 */
struct iwl_notification_wait {
    list_node_t list;

    bool (*fn)(struct iwl_notif_wait_data* notif_data, struct iwl_rx_packet* pkt, void* data);
    void* fn_data;

    uint16_t cmds[MAX_NOTIF_CMDS];
    uint8_t n_cmds;
    bool triggered, aborted;
};

/* caller functions */
void iwl_notification_wait_init(struct iwl_notif_wait_data* notif_data);
bool iwl_notification_wait(struct iwl_notif_wait_data* notif_data, struct iwl_rx_packet* pkt);
void iwl_abort_notification_waits(struct iwl_notif_wait_data* notif_data);

static inline void iwl_notification_notify(struct iwl_notif_wait_data* notif_data) {
#if 0   // NEEDS_PORTING
    wake_up_all(&notif_data->notif_waitq);
#endif  // NEEDS_PORTING
}

static inline void iwl_notification_wait_notify(struct iwl_notif_wait_data* notif_data,
                                                struct iwl_rx_packet* pkt) {
#if 0   // NEEDS_PORTING
    if (iwl_notification_wait(notif_data, pkt)) { iwl_notification_notify(notif_data); }
#endif  // NEEDS_PORTING
}

#if 0   // NEEDS_PORTING
/* user functions */
void __acquires(wait_entry)
    iwl_init_notification_wait(struct iwl_notif_wait_data* notif_data,
                               struct iwl_notification_wait* wait_entry, const uint16_t* cmds,
                               int n_cmds,
                               bool (*fn)(struct iwl_notif_wait_data* notif_data,
                                          struct iwl_rx_packet* pkt, void* data),
                               void* fn_data);

int __must_check __releases(wait_entry)
    iwl_wait_notification(struct iwl_notif_wait_data* notif_data,
                          struct iwl_notification_wait* wait_entry, unsigned long timeout);

void __releases(wait_entry) iwl_remove_notification(struct iwl_notif_wait_data* notif_data,
                                                    struct iwl_notification_wait* wait_entry);
#endif  // NEEDS_PORTING

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_NOTIF_WAIT_H_
