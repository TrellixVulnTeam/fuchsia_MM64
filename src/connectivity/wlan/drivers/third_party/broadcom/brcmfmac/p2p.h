/*
 * Copyright (c) 2012 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#ifndef GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_P2P_H_
#define GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_P2P_H_

#include <lib/sync/completion.h>

#include "core.h"
#include "device.h"
#include "fwil_types.h"
#include "linuxisms.h"
#include "workqueue.h"

struct brcmf_cfg80211_info;

/**
 * enum p2p_bss_type - different type of BSS configurations.
 *
 * @P2PAPI_BSSCFG_PRIMARY: maps to driver's primary bsscfg.
 * @P2PAPI_BSSCFG_DEVICE: maps to driver's P2P device discovery bsscfg.
 * @P2PAPI_BSSCFG_CONNECTION: maps to driver's P2P connection bsscfg.
 * @P2PAPI_BSSCFG_MAX: used for range checking.
 */
enum p2p_bss_type {
    P2PAPI_BSSCFG_PRIMARY,    /* maps to driver's primary bsscfg */
    P2PAPI_BSSCFG_DEVICE,     /* maps to driver's P2P device discovery bsscfg */
    P2PAPI_BSSCFG_CONNECTION, /* maps to driver's P2P connection bsscfg */
    P2PAPI_BSSCFG_MAX
};

/**
 * struct p2p_bss - peer-to-peer bss related information.
 *
 * @vif: virtual interface of this P2P bss.
 * @private_data: TBD
 */
struct p2p_bss {
    struct brcmf_cfg80211_vif* vif;
    void* private_data;
};

/**
 * enum brcmf_p2p_status - P2P specific dongle status.
 *
 * @BRCMF_P2P_STATUS_IF_ADD: peer-to-peer vif add sent to dongle.
 * @BRCMF_P2P_STATUS_IF_DEL: NOT-USED?
 * @BRCMF_P2P_STATUS_IF_DELETING: peer-to-peer vif delete sent to dongle.
 * @BRCMF_P2P_STATUS_IF_CHANGING: peer-to-peer vif change sent to dongle.
 * @BRCMF_P2P_STATUS_IF_CHANGED: peer-to-peer vif change completed on dongle.
 * @BRCMF_P2P_STATUS_ACTION_TX_COMPLETED: action frame tx completed.
 * @BRCMF_P2P_STATUS_ACTION_TX_NOACK: action frame tx not acked.
 * @BRCMF_P2P_STATUS_GO_NEG_PHASE: P2P GO negotiation ongoing.
 * @BRCMF_P2P_STATUS_DISCOVER_LISTEN: P2P listen, remaining on channel.
 * @BRCMF_P2P_STATUS_SENDING_ACT_FRAME: In the process of sending action frame.
 * @BRCMF_P2P_STATUS_WAITING_NEXT_AF_LISTEN: extra listen time for af tx.
 * @BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME: waiting for action frame response.
 * @BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL: search channel for AF active.
 */
enum brcmf_p2p_status {
    BRCMF_P2P_STATUS_ENABLED,
    BRCMF_P2P_STATUS_IF_ADD,
    BRCMF_P2P_STATUS_IF_DEL,
    BRCMF_P2P_STATUS_IF_DELETING,
    BRCMF_P2P_STATUS_IF_CHANGING,
    BRCMF_P2P_STATUS_IF_CHANGED,
    BRCMF_P2P_STATUS_ACTION_TX_COMPLETED,
    BRCMF_P2P_STATUS_ACTION_TX_NOACK,
    BRCMF_P2P_STATUS_GO_NEG_PHASE,
    BRCMF_P2P_STATUS_DISCOVER_LISTEN,
    BRCMF_P2P_STATUS_SENDING_ACT_FRAME,
    BRCMF_P2P_STATUS_WAITING_NEXT_AF_LISTEN,
    BRCMF_P2P_STATUS_WAITING_NEXT_ACT_FRAME,
    BRCMF_P2P_STATUS_FINDING_COMMON_CHANNEL
};

/**
 * struct afx_hdl - action frame off channel storage.
 *
 * @afx_work: worker thread for searching channel
 * @act_frm_scan: thread synchronizing struct.
 * @is_active: channel searching active.
 * @peer_chan: current channel.
 * @is_listen: sets mode for afx worker.
 * @my_listen_chan: this peers listen channel.
 * @peer_listen_chan: remote peers listen channel.
 * @tx_dst_addr: mac address where tx af should be sent to.
 */
struct afx_hdl {
    struct work_struct afx_work;
    sync_completion_t act_frm_scan;
    bool is_active;
    int32_t peer_chan;
    bool is_listen;
    uint16_t my_listen_chan;
    uint16_t peer_listen_chan;
    uint8_t tx_dst_addr[ETH_ALEN];
};

/**
 * struct brcmf_p2p_info - p2p specific driver information.
 *
 * @cfg: driver private data for cfg80211 interface.
 * @status: status of P2P (see enum brcmf_p2p_status).
 * @dev_addr: P2P device address.
 * @int_addr: P2P interface address.
 * @bss_idx: informate for P2P bss types.
 * @listen_timer: timer for @WL_P2P_DISC_ST_LISTEN discover state.
 * @listen_channel: channel for @WL_P2P_DISC_ST_LISTEN discover state.
 * @remain_on_channel: contains copy of struct used by cfg80211.
 * @remain_on_channel_cookie: cookie counter for remain on channel cmd
 * @next_af_subtype: expected action frame subtype.
 * @send_af_done: indication that action frame tx is complete.
 * @afx_hdl: action frame search handler info.
 * @af_sent_channel: channel action frame is sent.
 * @af_tx_sent_time: ZX time when af tx was transmitted.
 * @wait_next_af: thread synchronizing struct.
 * @gon_req_action: about to send go negotiation requets frame.
 * @block_gon_req_tx: drop tx go negotiation requets frame.
 * @p2pdev_dynamically: is p2p device if created by module param or supplicant.
 */
struct brcmf_p2p_info {
    struct brcmf_cfg80211_info* cfg;
    atomic_ulong status;
    uint8_t dev_addr[ETH_ALEN];
    uint8_t int_addr[ETH_ALEN];
    struct p2p_bss bss_idx[P2PAPI_BSSCFG_MAX];
    brcmf_timer_info_t listen_timer;
    uint8_t listen_channel;
    struct ieee80211_channel remain_on_channel;
    uint32_t remain_on_channel_cookie;
    uint8_t next_af_subtype;
    sync_completion_t send_af_done;
    struct afx_hdl afx_hdl;
    uint32_t af_sent_channel;
    zx_time_t af_tx_sent_time;
    sync_completion_t wait_next_af;
    bool gon_req_action;
    bool block_gon_req_tx;
    bool p2pdev_dynamically;
};

zx_status_t brcmf_p2p_ifchange(struct brcmf_cfg80211_info* cfg,
                               enum brcmf_fil_p2p_if_types if_type);
zx_status_t brcmf_p2p_start_device(struct wiphy* wiphy, struct wireless_dev* wdev);
void brcmf_p2p_stop_device(struct wiphy* wiphy, struct wireless_dev* wdev);
zx_status_t brcmf_p2p_remain_on_channel(struct wiphy* wiphy, struct wireless_dev* wdev,
                                        struct ieee80211_channel* channel, unsigned int duration,
                                        uint64_t* cookie);
bool brcmf_p2p_scan_finding_common_channel(struct brcmf_cfg80211_info* cfg,
                                           struct brcmf_bss_info_le* bi);
#endif /* GARNET_DRIVERS_WLAN_THIRD_PARTY_BROADCOM_BRCMFMAC_P2P_H_ */
