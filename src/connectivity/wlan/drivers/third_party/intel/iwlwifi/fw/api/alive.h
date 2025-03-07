/******************************************************************************
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright (C) 2018 Intel Corporation
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

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_ALIVE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_ALIVE_H_

/* alive response is_valid values */
#define ALIVE_RESP_UCODE_OK BIT(0)
#define ALIVE_RESP_RFKILL BIT(1)

/* alive response ver_type values */
enum {
    FW_TYPE_HW = 0,
    FW_TYPE_PROT = 1,
    FW_TYPE_AP = 2,
    FW_TYPE_WOWLAN = 3,
    FW_TYPE_TIMING = 4,
    FW_TYPE_WIPAN = 5
};

/* alive response ver_subtype values */
enum {
    FW_SUBTYPE_FULL_FEATURE = 0,
    FW_SUBTYPE_BOOTSRAP = 1, /* Not valid */
    FW_SUBTYPE_REDUCED = 2,
    FW_SUBTYPE_ALIVE_ONLY = 3,
    FW_SUBTYPE_WOWLAN = 4,
    FW_SUBTYPE_AP_SUBTYPE = 5,
    FW_SUBTYPE_WIPAN = 6,
    FW_SUBTYPE_INITIALIZE = 9
};

#define IWL_ALIVE_STATUS_ERR 0xDEAD
#define IWL_ALIVE_STATUS_OK 0xCAFE

#define IWL_ALIVE_FLG_RFKILL BIT(0)

struct iwl_lmac_alive {
    __le32 ucode_major;
    __le32 ucode_minor;
    uint8_t ver_subtype;
    uint8_t ver_type;
    uint8_t mac;
    uint8_t opt;
    __le32 timestamp;
    __le32 error_event_table_ptr; /* SRAM address for error log */
    __le32 log_event_table_ptr;   /* SRAM address for LMAC event log */
    __le32 cpu_register_ptr;
    __le32 dbgm_config_ptr;
    __le32 alive_counter_ptr;
    __le32 scd_base_ptr; /* SRAM address for SCD */
    __le32 st_fwrd_addr; /* pointer to Store and forward */
    __le32 st_fwrd_size;
} __packed; /* UCODE_ALIVE_NTFY_API_S_VER_3 */

struct iwl_umac_alive {
    __le32 umac_major;      /* UMAC version: major */
    __le32 umac_minor;      /* UMAC version: minor */
    __le32 error_info_addr; /* SRAM address for UMAC error log */
    __le32 dbg_print_buff_addr;
} __packed; /* UMAC_ALIVE_DATA_API_S_VER_2 */

struct mvm_alive_resp_v3 {
    __le16 status;
    __le16 flags;
    struct iwl_lmac_alive lmac_data;
    struct iwl_umac_alive umac_data;
} __packed; /* ALIVE_RES_API_S_VER_3 */

struct mvm_alive_resp {
    __le16 status;
    __le16 flags;
    struct iwl_lmac_alive lmac_data[2];
    struct iwl_umac_alive umac_data;
} __packed; /* ALIVE_RES_API_S_VER_4 */

/**
 * enum iwl_extended_cfg_flag - commands driver may send before
 *  finishing init flow
 * @IWL_INIT_DEBUG_CFG: driver is going to send debug config command
 * @IWL_INIT_NVM: driver is going to send NVM_ACCESS commands
 * @IWL_INIT_PHY: driver is going to send PHY_DB commands
 */
enum iwl_extended_cfg_flags {
    IWL_INIT_DEBUG_CFG,
    IWL_INIT_NVM,
    IWL_INIT_PHY,
};

/**
 * struct iwl_extended_cfg_cmd - mark what commands ucode should wait for
 * before finishing init flows
 * @init_flags: values from iwl_extended_cfg_flags
 */
struct iwl_init_extended_cfg_cmd {
    __le32 init_flags;
} __packed; /* INIT_EXTENDED_CFG_CMD_API_S_VER_1 */

/**
 * struct iwl_radio_version_notif - information on the radio version
 * ( RADIO_VERSION_NOTIFICATION = 0x68 )
 * @radio_flavor: radio flavor
 * @radio_step: radio version step
 * @radio_dash: radio version dash
 */
struct iwl_radio_version_notif {
    __le32 radio_flavor;
    __le32 radio_step;
    __le32 radio_dash;
} __packed; /* RADIO_VERSION_NOTOFICATION_S_VER_1 */

enum iwl_card_state_flags {
    CARD_ENABLED = 0x00,
    HW_CARD_DISABLED = 0x01,
    SW_CARD_DISABLED = 0x02,
    CT_KILL_CARD_DISABLED = 0x04,
    HALT_CARD_DISABLED = 0x08,
    CARD_DISABLED_MSK = 0x0f,
    CARD_IS_RX_ON = 0x10,
};

/**
 * struct iwl_radio_version_notif - information on the card state
 * ( CARD_STATE_NOTIFICATION = 0xa1 )
 * @flags: &enum iwl_card_state_flags
 */
struct iwl_card_state_notif {
    __le32 flags;
} __packed; /* CARD_STATE_NTFY_API_S_VER_1 */

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_FW_API_ALIVE_H_
