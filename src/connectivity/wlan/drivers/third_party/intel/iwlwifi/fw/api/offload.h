/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
 *    the documentation and/or other materials provided with the
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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_OFFLOAD_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_OFFLOAD_H_

/**
 * enum iwl_prot_offload_subcmd_ids - protocol offload commands
 */
enum iwl_prot_offload_subcmd_ids {
    /**
     * @STORED_BEACON_NTF: &struct iwl_stored_beacon_notif
     */
    STORED_BEACON_NTF = 0xFF,
};

#define MAX_STORED_BEACON_SIZE 600

/**
 * struct iwl_stored_beacon_notif - Stored beacon notification
 *
 * @system_time: system time on air rise
 * @tsf: TSF on air rise
 * @beacon_timestamp: beacon on air rise
 * @band: band, matches &RX_RES_PHY_FLAGS_BAND_24 definition
 * @channel: channel this beacon was received on
 * @rates: rate in ucode internal format
 * @byte_count: frame's byte count
 * @data: beacon data, length in @byte_count
 */
struct iwl_stored_beacon_notif {
    __le32 system_time;
    __le64 tsf;
    __le32 beacon_timestamp;
    __le16 band;
    __le16 channel;
    __le32 rates;
    __le32 byte_count;
    uint8_t data[MAX_STORED_BEACON_SIZE];
} __packed; /* WOWLAN_STROED_BEACON_INFO_S_VER_2 */

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_OFFLOAD_H_
