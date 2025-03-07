/******************************************************************************
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
 * Copyright(c) 2018        Intel Corporation
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
 *****************************************************************************/
#include <linux/etherdevice.h>
#include <linux/export.h>
#include <linux/firmware.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/types.h>

#include "fw/acpi.h"
#include "fw/api/cmdhdr.h"
#include "fw/api/commands.h"
#include "fw/api/nvm-reg.h"
#include "fw/img.h"
#include "iwl-csr.h"
#include "iwl-drv.h"
#include "iwl-io.h"
#include "iwl-modparams.h"
#include "iwl-nvm-parse.h"
#include "iwl-prph.h"

/* NVM offsets (in words) definitions */
enum nvm_offsets {
    /* NVM HW-Section offset (in words) definitions */
    SUBSYSTEM_ID = 0x0A,
    HW_ADDR = 0x15,

    /* NVM SW-Section offset (in words) definitions */
    NVM_SW_SECTION = 0x1C0,
    NVM_VERSION = 0,
    RADIO_CFG = 1,
    SKU = 2,
    N_HW_ADDRS = 3,
    NVM_CHANNELS = 0x1E0 - NVM_SW_SECTION,

    /* NVM REGULATORY -Section offset (in words) definitions */
    NVM_CHANNELS_SDP = 0,
};

enum ext_nvm_offsets {
    /* NVM HW-Section offset (in words) definitions */
    MAC_ADDRESS_OVERRIDE_EXT_NVM = 1,

    /* NVM SW-Section offset (in words) definitions */
    NVM_VERSION_EXT_NVM = 0,
    RADIO_CFG_FAMILY_EXT_NVM = 0,
    SKU_FAMILY_8000 = 2,
    N_HW_ADDRS_FAMILY_8000 = 3,

    /* NVM REGULATORY -Section offset (in words) definitions */
    NVM_CHANNELS_EXTENDED = 0,
    NVM_LAR_OFFSET_OLD = 0x4C7,
    NVM_LAR_OFFSET = 0x507,
    NVM_LAR_ENABLED = 0x7,
};

/* SKU Capabilities (actual values from NVM definition) */
enum nvm_sku_bits {
    NVM_SKU_CAP_BAND_24GHZ = BIT(0),
    NVM_SKU_CAP_BAND_52GHZ = BIT(1),
    NVM_SKU_CAP_11N_ENABLE = BIT(2),
    NVM_SKU_CAP_11AC_ENABLE = BIT(3),
    NVM_SKU_CAP_MIMO_DISABLE = BIT(5),
};

/*
 * These are the channel numbers in the order that they are stored in the NVM
 */
static const uint8_t iwl_nvm_channels[] = {
    /* 2.4 GHz */
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    /* 5 GHz */
    36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149,
    153, 157, 161, 165};

static const uint8_t iwl_ext_nvm_channels[] = {
    /* 2.4 GHz */
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14,
    /* 5 GHz */
    36, 40, 44, 48, 52, 56, 60, 64, 68, 72, 76, 80, 84, 88, 92, 96, 100, 104, 108, 112, 116, 120,
    124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165, 169, 173, 177, 181};

#define IWL_NVM_NUM_CHANNELS ARRAY_SIZE(iwl_nvm_channels)
#define IWL_NVM_NUM_CHANNELS_EXT ARRAY_SIZE(iwl_ext_nvm_channels)
#define NUM_2GHZ_CHANNELS 14
#define NUM_2GHZ_CHANNELS_EXT 14
#define FIRST_2GHZ_HT_MINUS 5
#define LAST_2GHZ_HT_PLUS 9
#define LAST_5GHZ_HT 165
#define LAST_5GHZ_HT_FAMILY_8000 181
#define N_HW_ADDR_MASK 0xF

/* rate data (static) */
static struct ieee80211_rate iwl_cfg80211_rates[] = {
    {
        .bitrate = 1 * 10,
        .hw_value = 0,
        .hw_value_short = 0,
    },
    {
        .bitrate = 2 * 10,
        .hw_value = 1,
        .hw_value_short = 1,
        .flags = IEEE80211_RATE_SHORT_PREAMBLE,
    },
    {
        .bitrate = 5.5 * 10,
        .hw_value = 2,
        .hw_value_short = 2,
        .flags = IEEE80211_RATE_SHORT_PREAMBLE,
    },
    {
        .bitrate = 11 * 10,
        .hw_value = 3,
        .hw_value_short = 3,
        .flags = IEEE80211_RATE_SHORT_PREAMBLE,
    },
    {
        .bitrate = 6 * 10,
        .hw_value = 4,
        .hw_value_short = 4,
    },
    {
        .bitrate = 9 * 10,
        .hw_value = 5,
        .hw_value_short = 5,
    },
    {
        .bitrate = 12 * 10,
        .hw_value = 6,
        .hw_value_short = 6,
    },
    {
        .bitrate = 18 * 10,
        .hw_value = 7,
        .hw_value_short = 7,
    },
    {
        .bitrate = 24 * 10,
        .hw_value = 8,
        .hw_value_short = 8,
    },
    {
        .bitrate = 36 * 10,
        .hw_value = 9,
        .hw_value_short = 9,
    },
    {
        .bitrate = 48 * 10,
        .hw_value = 10,
        .hw_value_short = 10,
    },
    {
        .bitrate = 54 * 10,
        .hw_value = 11,
        .hw_value_short = 11,
    },
};
#define RATES_24_OFFS 0
#define N_RATES_24 ARRAY_SIZE(iwl_cfg80211_rates)
#define RATES_52_OFFS 4
#define N_RATES_52 (N_RATES_24 - RATES_52_OFFS)

/**
 * enum iwl_nvm_channel_flags - channel flags in NVM
 * @NVM_CHANNEL_VALID: channel is usable for this SKU/geo
 * @NVM_CHANNEL_IBSS: usable as an IBSS channel
 * @NVM_CHANNEL_ACTIVE: active scanning allowed
 * @NVM_CHANNEL_RADAR: radar detection required
 * @NVM_CHANNEL_INDOOR_ONLY: only indoor use is allowed
 * @NVM_CHANNEL_GO_CONCURRENT: GO operation is allowed when connected to BSS
 *  on same channel on 2.4 or same UNII band on 5.2
 * @NVM_CHANNEL_UNIFORM: uniform spreading required
 * @NVM_CHANNEL_20MHZ: 20 MHz channel okay
 * @NVM_CHANNEL_40MHZ: 40 MHz channel okay
 * @NVM_CHANNEL_80MHZ: 80 MHz channel okay
 * @NVM_CHANNEL_160MHZ: 160 MHz channel okay
 * @NVM_CHANNEL_DC_HIGH: DC HIGH required/allowed (?)
 */
enum iwl_nvm_channel_flags {
    NVM_CHANNEL_VALID = BIT(0),
    NVM_CHANNEL_IBSS = BIT(1),
    NVM_CHANNEL_ACTIVE = BIT(3),
    NVM_CHANNEL_RADAR = BIT(4),
    NVM_CHANNEL_INDOOR_ONLY = BIT(5),
    NVM_CHANNEL_GO_CONCURRENT = BIT(6),
    NVM_CHANNEL_UNIFORM = BIT(7),
    NVM_CHANNEL_20MHZ = BIT(8),
    NVM_CHANNEL_40MHZ = BIT(9),
    NVM_CHANNEL_80MHZ = BIT(10),
    NVM_CHANNEL_160MHZ = BIT(11),
    NVM_CHANNEL_DC_HIGH = BIT(12),
};

static inline void iwl_nvm_print_channel_flags(struct device* dev, uint32_t level, int chan,
                                               uint16_t flags) {
#define CHECK_AND_PRINT_I(x) ((flags & NVM_CHANNEL_##x) ? " " #x : "")

    if (!(flags & NVM_CHANNEL_VALID)) {
        IWL_DEBUG_DEV(dev, level, "Ch. %d: 0x%x: No traffic\n", chan, flags);
        return;
    }

    /* Note: already can print up to 101 characters, 110 is the limit! */
    IWL_DEBUG_DEV(dev, level, "Ch. %d: 0x%x:%s%s%s%s%s%s%s%s%s%s%s%s\n", chan, flags,
                  CHECK_AND_PRINT_I(VALID), CHECK_AND_PRINT_I(IBSS), CHECK_AND_PRINT_I(ACTIVE),
                  CHECK_AND_PRINT_I(RADAR), CHECK_AND_PRINT_I(INDOOR_ONLY),
                  CHECK_AND_PRINT_I(GO_CONCURRENT), CHECK_AND_PRINT_I(UNIFORM),
                  CHECK_AND_PRINT_I(20MHZ), CHECK_AND_PRINT_I(40MHZ), CHECK_AND_PRINT_I(80MHZ),
                  CHECK_AND_PRINT_I(160MHZ), CHECK_AND_PRINT_I(DC_HIGH));
#undef CHECK_AND_PRINT_I
}

static uint32_t iwl_get_channel_flags(uint8_t ch_num, int ch_idx, bool is_5ghz, uint16_t nvm_flags,
                                      const struct iwl_cfg* cfg) {
    uint32_t flags = IEEE80211_CHAN_NO_HT40;
    uint32_t last_5ghz_ht = LAST_5GHZ_HT;

    if (cfg->nvm_type == IWL_NVM_EXT) { last_5ghz_ht = LAST_5GHZ_HT_FAMILY_8000; }

    if (!is_5ghz && (nvm_flags & NVM_CHANNEL_40MHZ)) {
        if (ch_num <= LAST_2GHZ_HT_PLUS) { flags &= ~IEEE80211_CHAN_NO_HT40PLUS; }
        if (ch_num >= FIRST_2GHZ_HT_MINUS) { flags &= ~IEEE80211_CHAN_NO_HT40MINUS; }
    } else if (ch_num <= last_5ghz_ht && (nvm_flags & NVM_CHANNEL_40MHZ)) {
        if ((ch_idx - NUM_2GHZ_CHANNELS) % 2 == 0) {
            flags &= ~IEEE80211_CHAN_NO_HT40PLUS;
        } else {
            flags &= ~IEEE80211_CHAN_NO_HT40MINUS;
        }
    }
    if (!(nvm_flags & NVM_CHANNEL_80MHZ)) { flags |= IEEE80211_CHAN_NO_80MHZ; }
    if (!(nvm_flags & NVM_CHANNEL_160MHZ)) { flags |= IEEE80211_CHAN_NO_160MHZ; }

    if (!(nvm_flags & NVM_CHANNEL_IBSS)) { flags |= IEEE80211_CHAN_NO_IR; }

    if (!(nvm_flags & NVM_CHANNEL_ACTIVE)) { flags |= IEEE80211_CHAN_NO_IR; }

    if (nvm_flags & NVM_CHANNEL_RADAR) { flags |= IEEE80211_CHAN_RADAR; }

    if (nvm_flags & NVM_CHANNEL_INDOOR_ONLY) { flags |= IEEE80211_CHAN_INDOOR_ONLY; }

    /* Set the GO concurrent flag only in case that NO_IR is set.
     * Otherwise it is meaningless
     */
    if ((nvm_flags & NVM_CHANNEL_GO_CONCURRENT) && (flags & IEEE80211_CHAN_NO_IR)) {
        flags |= IEEE80211_CHAN_IR_CONCURRENT;
    }

    return flags;
}

static int iwl_init_channel_map(struct device* dev, const struct iwl_cfg* cfg,
                                struct iwl_nvm_data* data, const __le16* const nvm_ch_flags,
                                uint32_t sbands_flags) {
    int ch_idx;
    int n_channels = 0;
    struct ieee80211_channel* channel;
    uint16_t ch_flags;
    int num_of_ch, num_2ghz_channels;
    const uint8_t* nvm_chan;

    if (cfg->nvm_type != IWL_NVM_EXT) {
        num_of_ch = IWL_NVM_NUM_CHANNELS;
        nvm_chan = &iwl_nvm_channels[0];
        num_2ghz_channels = NUM_2GHZ_CHANNELS;
    } else {
        num_of_ch = IWL_NVM_NUM_CHANNELS_EXT;
        nvm_chan = &iwl_ext_nvm_channels[0];
        num_2ghz_channels = NUM_2GHZ_CHANNELS_EXT;
    }

    for (ch_idx = 0; ch_idx < num_of_ch; ch_idx++) {
        bool is_5ghz = (ch_idx >= num_2ghz_channels);

        ch_flags = __le16_to_cpup(nvm_ch_flags + ch_idx);

        if (is_5ghz && !data->sku_cap_band_52ghz_enable) { continue; }

        /* workaround to disable wide channels in 5GHz */
        if ((sbands_flags & IWL_NVM_SBANDS_FLAGS_NO_WIDE_IN_5GHZ) && is_5ghz) {
            ch_flags &= ~(NVM_CHANNEL_40MHZ | NVM_CHANNEL_80MHZ | NVM_CHANNEL_160MHZ);
        }

        if (ch_flags & NVM_CHANNEL_160MHZ) { data->vht160_supported = true; }

        if (!(sbands_flags & IWL_NVM_SBANDS_FLAGS_LAR) && !(ch_flags & NVM_CHANNEL_VALID)) {
            /*
             * Channels might become valid later if lar is
             * supported, hence we still want to add them to
             * the list of supported channels to cfg80211.
             */
            iwl_nvm_print_channel_flags(dev, IWL_DL_EEPROM, nvm_chan[ch_idx], ch_flags);
            continue;
        }

        channel = &data->channels[n_channels];
        n_channels++;

        channel->hw_value = nvm_chan[ch_idx];
        channel->band = is_5ghz ? NL80211_BAND_5GHZ : NL80211_BAND_2GHZ;
        channel->center_freq = ieee80211_channel_to_frequency(channel->hw_value, channel->band);

        /* Initialize regulatory-based run-time data */

        /*
         * Default value - highest tx power value.  max_power
         * is not used in mvm, and is used for backwards compatibility
         */
        channel->max_power = IWL_DEFAULT_MAX_TX_POWER;

        /* don't put limitations in case we're using LAR */
        if (!(sbands_flags & IWL_NVM_SBANDS_FLAGS_LAR))
            channel->flags =
                iwl_get_channel_flags(nvm_chan[ch_idx], ch_idx, is_5ghz, ch_flags, cfg);
        else {
            channel->flags = 0;
        }

        iwl_nvm_print_channel_flags(dev, IWL_DL_EEPROM, channel->hw_value, ch_flags);
        IWL_DEBUG_EEPROM(dev, "Ch. %d: %ddBm\n", channel->hw_value, channel->max_power);
    }

    return n_channels;
}

static void iwl_init_vht_hw_capab(struct iwl_trans* trans, struct iwl_nvm_data* data,
                                  struct ieee80211_sta_vht_cap* vht_cap, uint8_t tx_chains,
                                  uint8_t rx_chains) {
    const struct iwl_cfg* cfg = trans->cfg;
    int num_rx_ants = num_of_ant(rx_chains);
    int num_tx_ants = num_of_ant(tx_chains);
    unsigned int max_ampdu_exponent =
        (cfg->max_vht_ampdu_exponent ?: IEEE80211_VHT_MAX_AMPDU_1024K);

    vht_cap->vht_supported = true;

    vht_cap->cap = IEEE80211_VHT_CAP_SHORT_GI_80 | IEEE80211_VHT_CAP_RXSTBC_1 |
                   IEEE80211_VHT_CAP_SU_BEAMFORMEE_CAPABLE |
                   3 << IEEE80211_VHT_CAP_BEAMFORMEE_STS_SHIFT |
                   max_ampdu_exponent << IEEE80211_VHT_CAP_MAX_A_MPDU_LENGTH_EXPONENT_SHIFT;

    if (data->vht160_supported) {
        vht_cap->cap |= IEEE80211_VHT_CAP_SUPP_CHAN_WIDTH_160MHZ | IEEE80211_VHT_CAP_SHORT_GI_160;
    }

    if (cfg->vht_mu_mimo_supported) { vht_cap->cap |= IEEE80211_VHT_CAP_MU_BEAMFORMEE_CAPABLE; }

    if (cfg->ht_params->ldpc) { vht_cap->cap |= IEEE80211_VHT_CAP_RXLDPC; }

    if (data->sku_cap_mimo_disabled) {
        num_rx_ants = 1;
        num_tx_ants = 1;
    }

    if (num_tx_ants > 1) {
        vht_cap->cap |= IEEE80211_VHT_CAP_TXSTBC;
    } else {
        vht_cap->cap |= IEEE80211_VHT_CAP_TX_ANTENNA_PATTERN;
    }

    switch (iwlwifi_mod_params.amsdu_size) {
    case IWL_AMSDU_DEF:
        if (cfg->mq_rx_supported) {
            vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
        } else {
            vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
        }
        break;
    case IWL_AMSDU_2K:
        if (cfg->mq_rx_supported) {
            vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
        } else {
            WARN(1, "RB size of 2K is not supported by this device\n");
        }
        break;
    case IWL_AMSDU_4K:
        vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_3895;
        break;
    case IWL_AMSDU_8K:
        vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_7991;
        break;
    case IWL_AMSDU_12K:
        vht_cap->cap |= IEEE80211_VHT_CAP_MAX_MPDU_LENGTH_11454;
        break;
    default:
        break;
    }

    vht_cap->vht_mcs.rx_mcs_map =
        cpu_to_le16(IEEE80211_VHT_MCS_SUPPORT_0_9 << 0 | IEEE80211_VHT_MCS_SUPPORT_0_9 << 2 |
                    IEEE80211_VHT_MCS_NOT_SUPPORTED << 4 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 6 |
                    IEEE80211_VHT_MCS_NOT_SUPPORTED << 8 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 10 |
                    IEEE80211_VHT_MCS_NOT_SUPPORTED << 12 | IEEE80211_VHT_MCS_NOT_SUPPORTED << 14);

    if (num_rx_ants == 1 || cfg->rx_with_siso_diversity) {
        vht_cap->cap |= IEEE80211_VHT_CAP_RX_ANTENNA_PATTERN;
        /* this works because NOT_SUPPORTED == 3 */
        vht_cap->vht_mcs.rx_mcs_map |= cpu_to_le16(IEEE80211_VHT_MCS_NOT_SUPPORTED << 2);
    }

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
    vht_cap->cap ^= trans->dbg_cfg.vht_cap_flip;
#endif

    vht_cap->vht_mcs.tx_mcs_map = vht_cap->vht_mcs.rx_mcs_map;
}

static struct ieee80211_sband_iftype_data iwl_he_capa[] = {
    {
        .types_mask = BIT(NL80211_IFTYPE_STATION),
        .he_cap =
            {
                .has_he = true,
                .he_cap_elem =
                    {
                        .mac_cap_info[0] =
                            IEEE80211_HE_MAC_CAP0_HTC_HE | IEEE80211_HE_MAC_CAP0_TWT_REQ,
                        .mac_cap_info[1] = IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
                                           IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
                        .mac_cap_info[2] = IEEE80211_HE_MAC_CAP2_32BIT_BA_BITMAP |
                                           IEEE80211_HE_MAC_CAP2_MU_CASCADING |
                                           IEEE80211_HE_MAC_CAP2_ACK_EN,
                        .mac_cap_info[3] = IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
                                           IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_VHT_2,
                        .mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMDSU_IN_AMPDU |
                                           IEEE80211_HE_MAC_CAP4_MULTI_TID_AGG_TX_QOS_B39,
                        .mac_cap_info[5] = IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B40 |
                                           IEEE80211_HE_MAC_CAP5_MULTI_TID_AGG_TX_QOS_B41 |
                                           IEEE80211_HE_MAC_CAP5_UL_2x996_TONE_RU,
                        .phy_cap_info[0] =
                            IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
                            IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
                            IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G,
                        .phy_cap_info[1] = IEEE80211_HE_PHY_CAP1_PREAMBLE_PUNC_RX_MASK |
                                           IEEE80211_HE_PHY_CAP1_DEVICE_CLASS_A |
                                           IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
                                           IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
                        .phy_cap_info[2] = IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
                                           IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
                                           IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ |
                                           IEEE80211_HE_PHY_CAP2_UL_MU_FULL_MU_MIMO |
                                           IEEE80211_HE_PHY_CAP2_UL_MU_PARTIAL_MU_MIMO,
                        .phy_cap_info[3] = IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK |
                                           IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 |
                                           IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK |
                                           IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1,
                        .phy_cap_info[4] = IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
                                           IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_8 |
                                           IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_8,
                        .phy_cap_info[5] =
                            IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2 |
                            IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_2 |
                            IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
                            IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK,
                        .phy_cap_info[6] = IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
                                           IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
                                           IEEE80211_HE_PHY_CAP6_TRIG_SU_BEAMFORMER_FB |
                                           IEEE80211_HE_PHY_CAP6_TRIG_MU_BEAMFORMER_FB |
                                           IEEE80211_HE_PHY_CAP6_TRIG_CQI_FB |
                                           IEEE80211_HE_PHY_CAP6_PARTIAL_BANDWIDTH_DL_MUMIMO |
                                           IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT,
                        .phy_cap_info[7] = IEEE80211_HE_PHY_CAP7_POWER_BOOST_FACTOR_AR |
                                           IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI |
                                           IEEE80211_HE_PHY_CAP7_MAX_NC_1,
                        .phy_cap_info[8] = IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
                                           IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
                                           IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
                                           IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU |
                                           IEEE80211_HE_PHY_CAP8_DCM_MAX_BW_160_OR_80P80_MHZ,
                        .phy_cap_info[9] =
                            IEEE80211_HE_PHY_CAP9_NON_TRIGGERED_CQI_FEEDBACK |
                            IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
                            IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB,
                    },
                /*
                 * Set default Tx/Rx HE MCS NSS Support field.
                 * Indicate support for up to 2 spatial streams and all
                 * MCS, without any special cases
                 */
                .he_mcs_nss_supp =
                    {
                        .rx_mcs_80 = cpu_to_le16(0xfffa),
                        .tx_mcs_80 = cpu_to_le16(0xfffa),
                        .rx_mcs_160 = cpu_to_le16(0xfffa),
                        .tx_mcs_160 = cpu_to_le16(0xfffa),
                        .rx_mcs_80p80 = cpu_to_le16(0xffff),
                        .tx_mcs_80p80 = cpu_to_le16(0xffff),
                    },
                /*
                 * Set default PPE thresholds, with PPET16 set to 0,
                 * PPET8 set to 7
                 */
                .ppe_thres = {0x61, 0x1c, 0xc7, 0x71},
            },
    },
    {
        .types_mask = BIT(NL80211_IFTYPE_AP),
        .he_cap =
            {
                .has_he = true,
                .he_cap_elem =
                    {
                        .mac_cap_info[0] =
#ifdef CPTCFG_IWLMVM_AX_SOFTAP_TESTMODE
                            IEEE80211_HE_MAC_CAP0_TWT_RES |
#endif
                            IEEE80211_HE_MAC_CAP0_HTC_HE,
                        .mac_cap_info[1] = IEEE80211_HE_MAC_CAP1_TF_MAC_PAD_DUR_16US |
                                           IEEE80211_HE_MAC_CAP1_MULTI_TID_AGG_RX_QOS_8,
                        .mac_cap_info[2] = IEEE80211_HE_MAC_CAP2_BSR |
                                           IEEE80211_HE_MAC_CAP2_MU_CASCADING |
                                           IEEE80211_HE_MAC_CAP2_ACK_EN,
                        .mac_cap_info[3] = IEEE80211_HE_MAC_CAP3_OMI_CONTROL |
                                           IEEE80211_HE_MAC_CAP3_MAX_AMPDU_LEN_EXP_VHT_2,
                        .mac_cap_info[4] = IEEE80211_HE_MAC_CAP4_AMDSU_IN_AMPDU,
                        .phy_cap_info[0] =
                            IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_IN_2G |
                            IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_40MHZ_80MHZ_IN_5G |
                            IEEE80211_HE_PHY_CAP0_CHANNEL_WIDTH_SET_160MHZ_IN_5G,
                        .phy_cap_info[1] = IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD |
                                           IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS,
                        .phy_cap_info[2] = IEEE80211_HE_PHY_CAP2_NDP_4x_LTF_AND_3_2US |
                                           IEEE80211_HE_PHY_CAP2_STBC_TX_UNDER_80MHZ |
                                           IEEE80211_HE_PHY_CAP2_STBC_RX_UNDER_80MHZ,
                        .phy_cap_info[3] = IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_TX_BPSK |
                                           IEEE80211_HE_PHY_CAP3_DCM_MAX_TX_NSS_1 |
                                           IEEE80211_HE_PHY_CAP3_DCM_MAX_CONST_RX_BPSK |
                                           IEEE80211_HE_PHY_CAP3_DCM_MAX_RX_NSS_1,
                        .phy_cap_info[4] = IEEE80211_HE_PHY_CAP4_SU_BEAMFORMEE |
                                           IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_ABOVE_80MHZ_8 |
                                           IEEE80211_HE_PHY_CAP4_BEAMFORMEE_MAX_STS_UNDER_80MHZ_8,
                        .phy_cap_info[5] =
                            IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_UNDER_80MHZ_2 |
                            IEEE80211_HE_PHY_CAP5_BEAMFORMEE_NUM_SND_DIM_ABOVE_80MHZ_2 |
                            IEEE80211_HE_PHY_CAP5_NG16_SU_FEEDBACK |
                            IEEE80211_HE_PHY_CAP5_NG16_MU_FEEDBACK,
                        .phy_cap_info[6] = IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_42_SU |
                                           IEEE80211_HE_PHY_CAP6_CODEBOOK_SIZE_75_MU |
                                           IEEE80211_HE_PHY_CAP6_PPE_THRESHOLD_PRESENT,
                        .phy_cap_info[7] = IEEE80211_HE_PHY_CAP7_HE_SU_MU_PPDU_4XLTF_AND_08_US_GI |
                                           IEEE80211_HE_PHY_CAP7_MAX_NC_1,
                        .phy_cap_info[8] = IEEE80211_HE_PHY_CAP8_HE_ER_SU_PPDU_4XLTF_AND_08_US_GI |
                                           IEEE80211_HE_PHY_CAP8_20MHZ_IN_40MHZ_HE_PPDU_IN_2G |
                                           IEEE80211_HE_PHY_CAP8_20MHZ_IN_160MHZ_HE_PPDU |
                                           IEEE80211_HE_PHY_CAP8_80MHZ_IN_160MHZ_HE_PPDU |
                                           IEEE80211_HE_PHY_CAP8_DCM_MAX_BW_160_OR_80P80_MHZ,
                        .phy_cap_info[9] =
                            IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_COMP_SIGB |
                            IEEE80211_HE_PHY_CAP9_RX_FULL_BW_SU_USING_MU_WITH_NON_COMP_SIGB,
                    },
                /*
                 * Set default Tx/Rx HE MCS NSS Support field.
                 * Indicate support for up to 2 spatial streams and all
                 * MCS, without any special cases
                 */
                .he_mcs_nss_supp =
                    {
                        .rx_mcs_80 = cpu_to_le16(0xfffa),
                        .tx_mcs_80 = cpu_to_le16(0xfffa),
                        .rx_mcs_160 = cpu_to_le16(0xfffa),
                        .tx_mcs_160 = cpu_to_le16(0xfffa),
                        .rx_mcs_80p80 = cpu_to_le16(0xffff),
                        .tx_mcs_80p80 = cpu_to_le16(0xffff),
                    },
                /*
                 * Set default PPE thresholds, with PPET16 set to 0,
                 * PPET8 set to 7
                 */
                .ppe_thres = {0x61, 0x1c, 0xc7, 0x71},
            },
    },
};

static void iwl_init_he_hw_capab(struct ieee80211_supported_band* sband, uint8_t tx_chains,
                                 uint8_t rx_chains) {
    if (sband->band == NL80211_BAND_2GHZ || sband->band == NL80211_BAND_5GHZ) {
        sband->iftype_data = iwl_he_capa;
    } else {
        return;
    }

    sband->n_iftype_data = ARRAY_SIZE(iwl_he_capa);

    /* If not 2x2, we need to indicate 1x1 in the Midamble RX Max NSTS */
    if ((tx_chains & rx_chains) != ANT_AB) {
        int i;

        for (i = 0; i < sband->n_iftype_data; i++) {
            iwl_he_capa[i].he_cap.he_cap_elem.phy_cap_info[1] &=
                ~IEEE80211_HE_PHY_CAP1_MIDAMBLE_RX_TX_MAX_NSTS;
            iwl_he_capa[i].he_cap.he_cap_elem.phy_cap_info[2] &=
                ~IEEE80211_HE_PHY_CAP2_MIDAMBLE_RX_TX_MAX_NSTS;
            iwl_he_capa[i].he_cap.he_cap_elem.phy_cap_info[7] &= ~IEEE80211_HE_PHY_CAP7_MAX_NC_MASK;
        }
    }
}

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
/* returns true iff there exists one spatial stream where MCS of a > b */
static bool iwl_he_mcs_greater(uint16_t a, uint16_t b) {
    int i;

    for (i = 0; i < 16; i += 2) {
        if ((((a >> i) + 1) & 3) > (((b >> i) + 1) & 3)) { return true; }
    }
    return false;
}

static void iwl_init_he_override(struct iwl_trans* trans, struct ieee80211_supported_band* sband) {
    struct ieee80211_sband_iftype_data* iftype_data;
    int i;

    if (sband->band != NL80211_BAND_2GHZ && sband->band != NL80211_BAND_5GHZ) { return; }

    for (i = 0; i < ARRAY_SIZE(iwl_he_capa); i++) {
        iftype_data = &iwl_he_capa[i];

        if (trans->dbg_cfg.rx_mcs_80) {
            if (iwl_he_mcs_greater(trans->dbg_cfg.rx_mcs_80,
                                   le16_to_cpu(iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_80)))
                IWL_ERR(trans, "Cannot set dbg rx_mcs_80 = 0x%x (too big)\n",
                        trans->dbg_cfg.rx_mcs_80);
            else
                iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_80 =
                    cpu_to_le16(trans->dbg_cfg.rx_mcs_80);
        }
        if (trans->dbg_cfg.tx_mcs_80) {
            if (iwl_he_mcs_greater(trans->dbg_cfg.tx_mcs_80,
                                   le16_to_cpu(iftype_data->he_cap.he_mcs_nss_supp.tx_mcs_80)))
                IWL_ERR(trans, "Cannot set dbg tx_mcs_80 = 0x%x (too big)\n",
                        trans->dbg_cfg.tx_mcs_80);
            else
                iftype_data->he_cap.he_mcs_nss_supp.tx_mcs_80 =
                    cpu_to_le16(trans->dbg_cfg.tx_mcs_80);
        }
        if (trans->dbg_cfg.rx_mcs_160) {
            if (iwl_he_mcs_greater(trans->dbg_cfg.rx_mcs_160,
                                   le16_to_cpu(iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_160)))
                IWL_ERR(trans, "Cannot set dbg rx_mcs_160 = 0x%x (too big)\n",
                        trans->dbg_cfg.rx_mcs_160);
            else
                iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_160 =
                    cpu_to_le16(trans->dbg_cfg.rx_mcs_160);
        }
        if (trans->dbg_cfg.tx_mcs_160) {
            if (iwl_he_mcs_greater(trans->dbg_cfg.tx_mcs_160,
                                   le16_to_cpu(iftype_data->he_cap.he_mcs_nss_supp.tx_mcs_160)))
                IWL_ERR(trans, "Cannot set dbg tx_mcs_160 = 0x%x (too big)\n",
                        trans->dbg_cfg.tx_mcs_160);
            else
                iftype_data->he_cap.he_mcs_nss_supp.tx_mcs_160 =
                    cpu_to_le16(trans->dbg_cfg.tx_mcs_160);
        }

        /*
         * If antennas were forced - make sure not declaring MIMO when
         * we actually are SISO
         * Recall that there are 2 bits per stream in the "HE Tx/Rx HE
         * MCS NSS Support Field", so if some antenna is forced on but
         * not both A and B - we should work in SISO mode, so mark the
         * 2nd SS as not supported
         */
        if (trans->dbg_cfg.valid_ants && (trans->dbg_cfg.valid_ants & ANT_AB) != ANT_AB) {
            iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_80 |=
                cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
            iftype_data->he_cap.he_mcs_nss_supp.tx_mcs_80 |=
                cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
            iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_160 |=
                cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
            iftype_data->he_cap.he_mcs_nss_supp.tx_mcs_160 |=
                cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
            iftype_data->he_cap.he_mcs_nss_supp.rx_mcs_80p80 |=
                cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
            iftype_data->he_cap.he_mcs_nss_supp.tx_mcs_80p80 |=
                cpu_to_le16(IEEE80211_HE_MCS_NOT_SUPPORTED << 2);
        }

        if (trans->dbg_cfg.no_ack_en & 0x1) {
            iftype_data->he_cap.he_cap_elem.mac_cap_info[2] &= ~IEEE80211_HE_MAC_CAP2_ACK_EN;
        }

        if (trans->dbg_cfg.no_ldpc)
            iftype_data->he_cap.he_cap_elem.phy_cap_info[1] &=
                ~IEEE80211_HE_PHY_CAP1_LDPC_CODING_IN_PAYLOAD;

        /* Check if any HE capabilities need to be set for debug */
        if (trans->dbg_cfg.he_ppe_thres.len) {
            uint8_t len = trans->dbg_cfg.he_ppe_thres.len;

            if (len > sizeof(iftype_data->he_cap.ppe_thres)) {
                len = sizeof(iftype_data->he_cap.ppe_thres);
            }
            memcpy(iftype_data->he_cap.ppe_thres, trans->dbg_cfg.he_ppe_thres.data, len);
        }

        if (trans->dbg_cfg.he_chan_width_dis)
            iftype_data->he_cap.he_cap_elem.phy_cap_info[0] &=
                ~(trans->dbg_cfg.he_chan_width_dis << 1);
    }
}
#endif

static void iwl_init_sbands(struct iwl_trans* trans, struct iwl_nvm_data* data,
                            const __le16* nvm_ch_flags, uint8_t tx_chains, uint8_t rx_chains,
                            uint32_t sbands_flags) {
    struct device* dev = trans->dev;
    const struct iwl_cfg* cfg = trans->cfg;
    int n_channels;
    int n_used = 0;
    struct ieee80211_supported_band* sband;

    n_channels = iwl_init_channel_map(dev, cfg, data, nvm_ch_flags, sbands_flags);
    sband = &data->bands[NL80211_BAND_2GHZ];
    sband->band = NL80211_BAND_2GHZ;
    sband->bitrates = &iwl_cfg80211_rates[RATES_24_OFFS];
    sband->n_bitrates = N_RATES_24;
    n_used += iwl_init_sband_channels(data, sband, n_channels, NL80211_BAND_2GHZ);
    iwl_init_ht_hw_capab(cfg, data, &sband->ht_cap, NL80211_BAND_2GHZ, tx_chains, rx_chains);

    if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax) {
        iwl_init_he_hw_capab(sband, tx_chains, rx_chains);
    }

    sband = &data->bands[NL80211_BAND_5GHZ];
    sband->band = NL80211_BAND_5GHZ;
    sband->bitrates = &iwl_cfg80211_rates[RATES_52_OFFS];
    sband->n_bitrates = N_RATES_52;
    n_used += iwl_init_sband_channels(data, sband, n_channels, NL80211_BAND_5GHZ);
    iwl_init_ht_hw_capab(cfg, data, &sband->ht_cap, NL80211_BAND_5GHZ, tx_chains, rx_chains);
    if (data->sku_cap_11ac_enable && !iwlwifi_mod_params.disable_11ac) {
        iwl_init_vht_hw_capab(trans, data, &sband->vht_cap, tx_chains, rx_chains);
    }

    if (data->sku_cap_11ax_enable && !iwlwifi_mod_params.disable_11ax) {
        iwl_init_he_hw_capab(sband, tx_chains, rx_chains);
    }

    if (n_channels != n_used) {
        IWL_ERR_DEV(dev, "NVM: used only %d of %d channels\n", n_used, n_channels);
    }
}

static int iwl_get_sku(const struct iwl_cfg* cfg, const __le16* nvm_sw, const __le16* phy_sku) {
    if (cfg->nvm_type != IWL_NVM_EXT) { return le16_to_cpup(nvm_sw + SKU); }

    return le32_to_cpup((__le32*)(phy_sku + SKU_FAMILY_8000));
}

static int iwl_get_nvm_version(const struct iwl_cfg* cfg, const __le16* nvm_sw) {
    if (cfg->nvm_type != IWL_NVM_EXT) {
        return le16_to_cpup(nvm_sw + NVM_VERSION);
    } else {
        return le32_to_cpup((__le32*)(nvm_sw + NVM_VERSION_EXT_NVM));
    }
}

static int iwl_get_radio_cfg(const struct iwl_cfg* cfg, const __le16* nvm_sw,
                             const __le16* phy_sku) {
    if (cfg->nvm_type != IWL_NVM_EXT) { return le16_to_cpup(nvm_sw + RADIO_CFG); }

    return le32_to_cpup((__le32*)(phy_sku + RADIO_CFG_FAMILY_EXT_NVM));
}

static int iwl_get_n_hw_addrs(const struct iwl_cfg* cfg, const __le16* nvm_sw) {
    int n_hw_addr;

    if (cfg->nvm_type != IWL_NVM_EXT) { return le16_to_cpup(nvm_sw + N_HW_ADDRS); }

    n_hw_addr = le32_to_cpup((__le32*)(nvm_sw + N_HW_ADDRS_FAMILY_8000));

    return n_hw_addr & N_HW_ADDR_MASK;
}

static void iwl_set_radio_cfg(const struct iwl_cfg* cfg, struct iwl_nvm_data* data,
                              uint32_t radio_cfg) {
    if (cfg->nvm_type != IWL_NVM_EXT) {
        data->radio_cfg_type = NVM_RF_CFG_TYPE_MSK(radio_cfg);
        data->radio_cfg_step = NVM_RF_CFG_STEP_MSK(radio_cfg);
        data->radio_cfg_dash = NVM_RF_CFG_DASH_MSK(radio_cfg);
        data->radio_cfg_pnum = NVM_RF_CFG_PNUM_MSK(radio_cfg);
        return;
    }

    /* set the radio configuration for family 8000 */
    data->radio_cfg_type = EXT_NVM_RF_CFG_TYPE_MSK(radio_cfg);
    data->radio_cfg_step = EXT_NVM_RF_CFG_STEP_MSK(radio_cfg);
    data->radio_cfg_dash = EXT_NVM_RF_CFG_DASH_MSK(radio_cfg);
    data->radio_cfg_pnum = EXT_NVM_RF_CFG_FLAVOR_MSK(radio_cfg);
    data->valid_tx_ant = EXT_NVM_RF_CFG_TX_ANT_MSK(radio_cfg);
    data->valid_rx_ant = EXT_NVM_RF_CFG_RX_ANT_MSK(radio_cfg);
}

static void iwl_flip_hw_address(__le32 mac_addr0, __le32 mac_addr1, uint8_t* dest) {
    const uint8_t* hw_addr;

    hw_addr = (const uint8_t*)&mac_addr0;
    dest[0] = hw_addr[3];
    dest[1] = hw_addr[2];
    dest[2] = hw_addr[1];
    dest[3] = hw_addr[0];

    hw_addr = (const uint8_t*)&mac_addr1;
    dest[4] = hw_addr[1];
    dest[5] = hw_addr[0];
}

static void iwl_set_hw_address_from_csr(struct iwl_trans* trans, struct iwl_nvm_data* data) {
    __le32 mac_addr0 = cpu_to_le32(iwl_read32(trans, trans->cfg->csr->mac_addr0_strap));
    __le32 mac_addr1 = cpu_to_le32(iwl_read32(trans, trans->cfg->csr->mac_addr1_strap));

    iwl_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);
    /*
     * If the OEM fused a valid address, use it instead of the one in the
     * OTP
     */
    if (is_valid_ether_addr(data->hw_addr)) { return; }

    mac_addr0 = cpu_to_le32(iwl_read32(trans, trans->cfg->csr->mac_addr0_otp));
    mac_addr1 = cpu_to_le32(iwl_read32(trans, trans->cfg->csr->mac_addr1_otp));

    iwl_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);
}

static void iwl_set_hw_address_family_8000(struct iwl_trans* trans, const struct iwl_cfg* cfg,
                                           struct iwl_nvm_data* data, const __le16* mac_override,
                                           const __be16* nvm_hw) {
    const uint8_t* hw_addr;

    if (mac_override) {
        static const uint8_t reserved_mac[] = {0x02, 0xcc, 0xaa, 0xff, 0xee, 0x00};

        hw_addr = (const uint8_t*)(mac_override + MAC_ADDRESS_OVERRIDE_EXT_NVM);

        /*
         * Store the MAC address from MAO section.
         * No byte swapping is required in MAO section
         */
        memcpy(data->hw_addr, hw_addr, ETH_ALEN);

        /*
         * Force the use of the OTP MAC address in case of reserved MAC
         * address in the NVM, or if address is given but invalid.
         */
        if (is_valid_ether_addr(data->hw_addr) && memcmp(reserved_mac, hw_addr, ETH_ALEN) != 0) {
            return;
        }

        IWL_ERR(trans, "mac address from nvm override section is not valid\n");
    }

    if (nvm_hw) {
        /* read the mac address from WFMP registers */
        __le32 mac_addr0 = cpu_to_le32(iwl_trans_read_prph(trans, WFMP_MAC_ADDR_0));
        __le32 mac_addr1 = cpu_to_le32(iwl_trans_read_prph(trans, WFMP_MAC_ADDR_1));

        iwl_flip_hw_address(mac_addr0, mac_addr1, data->hw_addr);

        return;
    }

    IWL_ERR(trans, "mac address is not found\n");
}

static int iwl_set_hw_address(struct iwl_trans* trans, const struct iwl_cfg* cfg,
                              struct iwl_nvm_data* data, const __be16* nvm_hw,
                              const __le16* mac_override) {
#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
    struct iwl_dbg_cfg* dbg_cfg = &trans->dbg_cfg;

    if (dbg_cfg->hw_address.len) {
        if (dbg_cfg->hw_address.len == ETH_ALEN && is_valid_ether_addr(dbg_cfg->hw_address.data)) {
            memcpy(data->hw_addr, dbg_cfg->hw_address.data, ETH_ALEN);
            return 0;
        }
        IWL_ERR(trans, "mac address from config file is invalid\n");
    }
#endif
    if (cfg->mac_addr_from_csr) {
        iwl_set_hw_address_from_csr(trans, data);
    } else if (cfg->nvm_type != IWL_NVM_EXT) {
        const uint8_t* hw_addr = (const uint8_t*)(nvm_hw + HW_ADDR);

        /* The byte order is little endian 16 bit, meaning 214365 */
        data->hw_addr[0] = hw_addr[1];
        data->hw_addr[1] = hw_addr[0];
        data->hw_addr[2] = hw_addr[3];
        data->hw_addr[3] = hw_addr[2];
        data->hw_addr[4] = hw_addr[5];
        data->hw_addr[5] = hw_addr[4];
    } else {
        iwl_set_hw_address_family_8000(trans, cfg, data, mac_override, nvm_hw);
    }

    if (!is_valid_ether_addr(data->hw_addr)) {
        IWL_ERR(trans, "no valid mac address was found\n");
        return -EINVAL;
    }

    IWL_INFO(trans, "base HW address: %pM\n", data->hw_addr);

    return 0;
}

static bool iwl_nvm_no_wide_in_5ghz(struct device* dev, const struct iwl_cfg* cfg,
                                    const __be16* nvm_hw) {
    /*
     * Workaround a bug in Indonesia SKUs where the regulatory in
     * some 7000-family OTPs erroneously allow wide channels in
     * 5GHz.  To check for Indonesia, we take the SKU value from
     * bits 1-4 in the subsystem ID and check if it is either 5 or
     * 9.  In those cases, we need to force-disable wide channels
     * in 5GHz otherwise the FW will throw a sysassert when we try
     * to use them.
     */
    if (cfg->device_family == IWL_DEVICE_FAMILY_7000) {
        /*
         * Unlike the other sections in the NVM, the hw
         * section uses big-endian.
         */
        uint16_t subsystem_id = be16_to_cpup(nvm_hw + SUBSYSTEM_ID);
        uint8_t sku = (subsystem_id & 0x1e) >> 1;

        if (sku == 5 || sku == 9) {
            IWL_DEBUG_EEPROM(dev, "disabling wide channels in 5GHz (0x%0x %d)\n", subsystem_id,
                             sku);
            return true;
        }
    }

    return false;
}

struct iwl_nvm_data* iwl_parse_nvm_data(struct iwl_trans* trans, const struct iwl_cfg* cfg,
                                        const __be16* nvm_hw, const __le16* nvm_sw,
                                        const __le16* nvm_calib, const __le16* regulatory,
                                        const __le16* mac_override, const __le16* phy_sku,
                                        uint8_t tx_chains, uint8_t rx_chains,
                                        bool lar_fw_supported) {
    struct device* dev = trans->dev;
    struct iwl_nvm_data* data;
    bool lar_enabled;
    uint32_t sku, radio_cfg;
    uint32_t sbands_flags = 0;
    uint16_t lar_config;
    const __le16* ch_section;

    if (cfg->nvm_type != IWL_NVM_EXT)
        data = kzalloc(sizeof(*data) + sizeof(struct ieee80211_channel) * IWL_NVM_NUM_CHANNELS,
                       GFP_KERNEL);
    else
        data = kzalloc(sizeof(*data) + sizeof(struct ieee80211_channel) * IWL_NVM_NUM_CHANNELS_EXT,
                       GFP_KERNEL);
    if (!data) { return NULL; }

    data->nvm_version = iwl_get_nvm_version(cfg, nvm_sw);

    radio_cfg = iwl_get_radio_cfg(cfg, nvm_sw, phy_sku);
    iwl_set_radio_cfg(cfg, data, radio_cfg);
    if (data->valid_tx_ant) { tx_chains &= data->valid_tx_ant; }
    if (data->valid_rx_ant) { rx_chains &= data->valid_rx_ant; }

    sku = iwl_get_sku(cfg, nvm_sw, phy_sku);

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
    if (trans->dbg_cfg.disable_52GHz) { /* remove support for 5.2 */
        sku &= ~NVM_SKU_CAP_BAND_52GHZ;
    }
    if (trans->dbg_cfg.disable_24GHz) { /* remove support for 2.4 */
        sku &= ~NVM_SKU_CAP_BAND_24GHZ;
    }
#endif

    data->sku_cap_band_24ghz_enable = sku & NVM_SKU_CAP_BAND_24GHZ;
    data->sku_cap_band_52ghz_enable = sku & NVM_SKU_CAP_BAND_52GHZ;
    data->sku_cap_11n_enable = sku & NVM_SKU_CAP_11N_ENABLE;
    if (iwlwifi_mod_params.disable_11n & IWL_DISABLE_HT_ALL) { data->sku_cap_11n_enable = false; }
    data->sku_cap_11ac_enable = data->sku_cap_11n_enable && (sku & NVM_SKU_CAP_11AC_ENABLE);
    data->sku_cap_mimo_disabled = sku & NVM_SKU_CAP_MIMO_DISABLE;

    data->n_hw_addrs = iwl_get_n_hw_addrs(cfg, nvm_sw);

    if (cfg->nvm_type != IWL_NVM_EXT) {
        /* Checking for required sections */
        if (!nvm_calib) {
            IWL_ERR(trans, "Can't parse empty Calib NVM sections\n");
            kfree(data);
            return NULL;
        }

        ch_section =
            cfg->nvm_type == IWL_NVM_SDP ? &regulatory[NVM_CHANNELS_SDP] : &nvm_sw[NVM_CHANNELS];

        lar_enabled = true;
    } else {
        uint16_t lar_offset = data->nvm_version < 0xE39 ? NVM_LAR_OFFSET_OLD : NVM_LAR_OFFSET;

        lar_config = le16_to_cpup(regulatory + lar_offset);
        data->lar_enabled = !!(lar_config & NVM_LAR_ENABLED);
        lar_enabled = data->lar_enabled;
        ch_section = &regulatory[NVM_CHANNELS_EXTENDED];
    }

    /* If no valid mac address was found - bail out */
    if (iwl_set_hw_address(trans, cfg, data, nvm_hw, mac_override)) {
        kfree(data);
        return NULL;
    }

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
    iwl_init_he_override(trans, &data->bands[NL80211_BAND_2GHZ]);
    iwl_init_he_override(trans, &data->bands[NL80211_BAND_5GHZ]);
#endif
    if (lar_fw_supported && lar_enabled) { sbands_flags |= IWL_NVM_SBANDS_FLAGS_LAR; }

    if (iwl_nvm_no_wide_in_5ghz(dev, cfg, nvm_hw)) {
        sbands_flags |= IWL_NVM_SBANDS_FLAGS_NO_WIDE_IN_5GHZ;
    }

    iwl_init_sbands(trans, data, ch_section, tx_chains, rx_chains, sbands_flags);
    data->calib_version = 255;

    return data;
}
IWL_EXPORT_SYMBOL(iwl_parse_nvm_data);

static uint32_t iwl_nvm_get_regdom_bw_flags(const uint8_t* nvm_chan, int ch_idx, uint16_t nvm_flags,
                                            const struct iwl_cfg* cfg) {
    uint32_t flags = NL80211_RRF_NO_HT40;
    uint32_t last_5ghz_ht = LAST_5GHZ_HT;

    if (cfg->nvm_type == IWL_NVM_EXT) { last_5ghz_ht = LAST_5GHZ_HT_FAMILY_8000; }

    if (ch_idx < NUM_2GHZ_CHANNELS && (nvm_flags & NVM_CHANNEL_40MHZ)) {
        if (nvm_chan[ch_idx] <= LAST_2GHZ_HT_PLUS) { flags &= ~NL80211_RRF_NO_HT40PLUS; }
        if (nvm_chan[ch_idx] >= FIRST_2GHZ_HT_MINUS) { flags &= ~NL80211_RRF_NO_HT40MINUS; }
    } else if (nvm_chan[ch_idx] <= last_5ghz_ht && (nvm_flags & NVM_CHANNEL_40MHZ)) {
        if ((ch_idx - NUM_2GHZ_CHANNELS) % 2 == 0) {
            flags &= ~NL80211_RRF_NO_HT40PLUS;
        } else {
            flags &= ~NL80211_RRF_NO_HT40MINUS;
        }
    }

    if (!(nvm_flags & NVM_CHANNEL_80MHZ)) { flags |= NL80211_RRF_NO_80MHZ; }
    if (!(nvm_flags & NVM_CHANNEL_160MHZ)) { flags |= NL80211_RRF_NO_160MHZ; }

    if (!(nvm_flags & NVM_CHANNEL_ACTIVE)) { flags |= NL80211_RRF_NO_IR; }

    if (nvm_flags & NVM_CHANNEL_RADAR) { flags |= NL80211_RRF_DFS; }

    if (nvm_flags & NVM_CHANNEL_INDOOR_ONLY) { flags |= NL80211_RRF_NO_OUTDOOR; }

    /* Set the GO concurrent flag only in case that NO_IR is set.
     * Otherwise it is meaningless
     */
    if ((nvm_flags & NVM_CHANNEL_GO_CONCURRENT) && (flags & NL80211_RRF_NO_IR)) {
        flags |= NL80211_RRF_GO_CONCURRENT;
    }

    return flags;
}

struct regdb_ptrs {
    struct ieee80211_wmm_rule* rule;
    uint32_t token;
};

struct ieee80211_regdomain* iwl_parse_nvm_mcc_info(struct device* dev, const struct iwl_cfg* cfg,
                                                   int num_of_ch, __le32* channels, uint16_t fw_mcc,
                                                   uint16_t geo_info) {
    int ch_idx;
    uint16_t ch_flags;
    uint32_t reg_rule_flags, prev_reg_rule_flags = 0;
    const uint8_t* nvm_chan =
        cfg->nvm_type == IWL_NVM_EXT ? iwl_ext_nvm_channels : iwl_nvm_channels;
    struct ieee80211_regdomain *regd, *copy_rd;
    int size_of_regd, regd_to_copy;
    struct ieee80211_reg_rule* rule;
    struct regdb_ptrs* regdb_ptrs;
    enum nl80211_band band;
    int center_freq, prev_center_freq = 0;
    int valid_rules = 0;
    bool new_rule;
    int max_num_ch = cfg->nvm_type == IWL_NVM_EXT ? IWL_NVM_NUM_CHANNELS_EXT : IWL_NVM_NUM_CHANNELS;

    if (WARN_ON_ONCE(num_of_ch > NL80211_MAX_SUPP_REG_RULES)) { return ERR_PTR(-EINVAL); }

    if (WARN_ON(num_of_ch > max_num_ch)) { num_of_ch = max_num_ch; }

    IWL_DEBUG_DEV(dev, IWL_DL_LAR, "building regdom for %d channels\n", num_of_ch);

    /* build a regdomain rule for every valid channel */
    size_of_regd =
        sizeof(struct ieee80211_regdomain) + num_of_ch * sizeof(struct ieee80211_reg_rule);

    regd = kzalloc(size_of_regd, GFP_KERNEL);
    if (!regd) { return ERR_PTR(-ENOMEM); }

    regdb_ptrs = kcalloc(num_of_ch, sizeof(*regdb_ptrs), GFP_KERNEL);
    if (!regdb_ptrs) {
        copy_rd = ERR_PTR(-ENOMEM);
        goto out;
    }

    /* set alpha2 from FW. */
    regd->alpha2[0] = fw_mcc >> 8;
    regd->alpha2[1] = fw_mcc & 0xff;

    for (ch_idx = 0; ch_idx < num_of_ch; ch_idx++) {
        ch_flags = (uint16_t)__le32_to_cpup(channels + ch_idx);
        band = (ch_idx < NUM_2GHZ_CHANNELS) ? NL80211_BAND_2GHZ : NL80211_BAND_5GHZ;
        center_freq = ieee80211_channel_to_frequency(nvm_chan[ch_idx], band);
        new_rule = false;

        if (!(ch_flags & NVM_CHANNEL_VALID)) {
            iwl_nvm_print_channel_flags(dev, IWL_DL_LAR, nvm_chan[ch_idx], ch_flags);
            continue;
        }

        reg_rule_flags = iwl_nvm_get_regdom_bw_flags(nvm_chan, ch_idx, ch_flags, cfg);

        /* we can't continue the same rule */
        if (ch_idx == 0 || prev_reg_rule_flags != reg_rule_flags ||
            center_freq - prev_center_freq > 20) {
            valid_rules++;
            new_rule = true;
        }

        rule = &regd->reg_rules[valid_rules - 1];

        if (new_rule) { rule->freq_range.start_freq_khz = MHZ_TO_KHZ(center_freq - 10); }

        rule->freq_range.end_freq_khz = MHZ_TO_KHZ(center_freq + 10);

        /* this doesn't matter - not used by FW */
        rule->power_rule.max_antenna_gain = DBI_TO_MBI(6);
        rule->power_rule.max_eirp = DBM_TO_MBM(IWL_DEFAULT_MAX_TX_POWER);

        rule->flags = reg_rule_flags;

        /* rely on auto-calculation to merge BW of contiguous chans */
        rule->flags |= NL80211_RRF_AUTO_BW;
        rule->freq_range.max_bandwidth_khz = 0;

        prev_center_freq = center_freq;
        prev_reg_rule_flags = reg_rule_flags;

        iwl_nvm_print_channel_flags(dev, IWL_DL_LAR, nvm_chan[ch_idx], ch_flags);

        if (!(geo_info & GEO_WMM_ETSI_5GHZ_INFO) || band == NL80211_BAND_2GHZ) { continue; }

        reg_query_regdb_wmm(regd->alpha2, center_freq, rule);
    }

    regd->n_reg_rules = valid_rules;

    /*
     * Narrow down regdom for unused regulatory rules to prevent hole
     * between reg rules to wmm rules.
     */
    regd_to_copy =
        sizeof(struct ieee80211_regdomain) + valid_rules * sizeof(struct ieee80211_reg_rule);

    copy_rd = kmemdup(regd, regd_to_copy, GFP_KERNEL);
    if (!copy_rd) {
        copy_rd = ERR_PTR(-ENOMEM);
        goto out;
    }

out:
    kfree(regdb_ptrs);
    kfree(regd);
    return copy_rd;
}
IWL_EXPORT_SYMBOL(iwl_parse_nvm_mcc_info);

#define IWL_MAX_NVM_SECTION_SIZE 0x1b58
#define IWL_MAX_EXT_NVM_SECTION_SIZE 0x1ffc
#define MAX_NVM_FILE_LEN 16384

void iwl_nvm_fixups(uint32_t hw_id, unsigned int section, uint8_t* data, unsigned int len) {
#define IWL_4165_DEVICE_ID 0x5501
#define NVM_SKU_CAP_MIMO_DISABLE BIT(5)

    if (section == NVM_SECTION_TYPE_PHY_SKU && hw_id == IWL_4165_DEVICE_ID && data && len >= 5 &&
        (data[4] & NVM_SKU_CAP_MIMO_DISABLE))
    /* OTP 0x52 bug work around: it's a 1x1 device */
    {
        data[3] = ANT_B | (ANT_B << 4);
    }
}
IWL_EXPORT_SYMBOL(iwl_nvm_fixups);

/*
 * Reads external NVM from a file into mvm->nvm_sections
 *
 * HOW TO CREATE THE NVM FILE FORMAT:
 * ------------------------------
 * 1. create hex file, format:
 *      3800 -> header
 *      0000 -> header
 *      5a40 -> data
 *
 *   rev - 6 bit (word1)
 *   len - 10 bit (word1)
 *   id - 4 bit (word2)
 *   rsv - 12 bit (word2)
 *
 * 2. flip 8bits with 8 bits per line to get the right NVM file format
 *
 * 3. create binary file from the hex file
 *
 * 4. save as "iNVM_xxx.bin" under /lib/firmware
 */
int iwl_read_external_nvm(struct iwl_trans* trans, const char* nvm_file_name,
                          struct iwl_nvm_section* nvm_sections) {
    int ret, section_size;
    uint16_t section_id;
    const struct firmware* fw_entry;
    const struct {
        __le16 word1;
        __le16 word2;
        uint8_t data[];
    } * file_sec;
    const uint8_t* eof;
    uint8_t* temp;
    int max_section_size;
    const __le32* dword_buff;

#define NVM_WORD1_LEN(x) (8 * (x & 0x03FF))
#define NVM_WORD2_ID(x) (x >> 12)
#define EXT_NVM_WORD2_LEN(x) (2 * (((x)&0xFF) << 8 | (x) >> 8))
#define EXT_NVM_WORD1_ID(x) ((x) >> 4)
#define NVM_HEADER_0 (0x2A504C54)
#define NVM_HEADER_1 (0x4E564D2A)
#define NVM_HEADER_SIZE (4 * sizeof(uint32_t))

    IWL_DEBUG_EEPROM(trans->dev, "Read from external NVM\n");

    /* Maximal size depends on NVM version */
    if (trans->cfg->nvm_type != IWL_NVM_EXT) {
        max_section_size = IWL_MAX_NVM_SECTION_SIZE;
    } else {
        max_section_size = IWL_MAX_EXT_NVM_SECTION_SIZE;
    }

    /*
     * Obtain NVM image via request_firmware. Since we already used
     * request_firmware_nowait() for the firmware binary load and only
     * get here after that we assume the NVM request can be satisfied
     * synchronously.
     */
    ret = request_firmware(&fw_entry, nvm_file_name, trans->dev);
    if (ret) {
        IWL_ERR(trans, "ERROR: %s isn't available %d\n", nvm_file_name, ret);
        return ret;
    }

    IWL_INFO(trans, "Loaded NVM file %s (%zu bytes)\n", nvm_file_name, fw_entry->size);

    if (fw_entry->size > MAX_NVM_FILE_LEN) {
        IWL_ERR(trans, "NVM file too large\n");
        ret = -EINVAL;
        goto out;
    }

    eof = fw_entry->data + fw_entry->size;
    dword_buff = (__le32*)fw_entry->data;

    /* some NVM file will contain a header.
     * The header is identified by 2 dwords header as follow:
     * dword[0] = 0x2A504C54
     * dword[1] = 0x4E564D2A
     *
     * This header must be skipped when providing the NVM data to the FW.
     */
    if (fw_entry->size > NVM_HEADER_SIZE && dword_buff[0] == cpu_to_le32(NVM_HEADER_0) &&
        dword_buff[1] == cpu_to_le32(NVM_HEADER_1)) {
        file_sec = (void*)(fw_entry->data + NVM_HEADER_SIZE);
        IWL_INFO(trans, "NVM Version %08X\n", le32_to_cpu(dword_buff[2]));
        IWL_INFO(trans, "NVM Manufacturing date %08X\n", le32_to_cpu(dword_buff[3]));

        /* nvm file validation, dword_buff[2] holds the file version */
        if (trans->cfg->device_family == IWL_DEVICE_FAMILY_8000 &&
            CSR_HW_REV_STEP(trans->hw_rev) == SILICON_C_STEP &&
            le32_to_cpu(dword_buff[2]) < 0xE4A) {
            ret = -EFAULT;
            goto out;
        }
    } else {
        file_sec = (void*)fw_entry->data;
    }

    while (true) {
        if (file_sec->data > eof) {
            IWL_ERR(trans, "ERROR - NVM file too short for section header\n");
            ret = -EINVAL;
            break;
        }

        /* check for EOF marker */
        if (!file_sec->word1 && !file_sec->word2) {
            ret = 0;
            break;
        }

        if (trans->cfg->nvm_type != IWL_NVM_EXT) {
            section_size = 2 * NVM_WORD1_LEN(le16_to_cpu(file_sec->word1));
            section_id = NVM_WORD2_ID(le16_to_cpu(file_sec->word2));
        } else {
            section_size = 2 * EXT_NVM_WORD2_LEN(le16_to_cpu(file_sec->word2));
            section_id = EXT_NVM_WORD1_ID(le16_to_cpu(file_sec->word1));
        }

        if (section_size > max_section_size) {
            IWL_ERR(trans, "ERROR - section too large (%d)\n", section_size);
            ret = -EINVAL;
            break;
        }

        if (!section_size) {
            IWL_ERR(trans, "ERROR - section empty\n");
            ret = -EINVAL;
            break;
        }

        if (file_sec->data + section_size > eof) {
            IWL_ERR(trans, "ERROR - NVM file too short for section (%d bytes)\n", section_size);
            ret = -EINVAL;
            break;
        }

        if (WARN(section_id >= NVM_MAX_NUM_SECTIONS, "Invalid NVM section ID %d\n", section_id)) {
            ret = -EINVAL;
            break;
        }

        temp = kmemdup(file_sec->data, section_size, GFP_KERNEL);
        if (!temp) {
            ret = -ENOMEM;
            break;
        }

        iwl_nvm_fixups(trans->hw_id, section_id, temp, section_size);

        kfree(nvm_sections[section_id].data);
        nvm_sections[section_id].data = temp;
        nvm_sections[section_id].length = section_size;

        /* advance to the next section */
        file_sec = (void*)(file_sec->data + section_size);
    }
out:
    release_firmware(fw_entry);
    return ret;
}
IWL_EXPORT_SYMBOL(iwl_read_external_nvm);

struct iwl_nvm_data* iwl_get_nvm(struct iwl_trans* trans, const struct iwl_fw* fw) {
    struct iwl_nvm_get_info cmd = {};
    struct iwl_nvm_get_info_rsp* rsp;
    struct iwl_nvm_data* nvm;
    struct iwl_host_cmd hcmd = {.flags = CMD_WANT_SKB | CMD_SEND_IN_RFKILL,
                                .data =
                                    {
                                        &cmd,
                                    },
                                .len = {sizeof(cmd)},
                                .id = WIDE_ID(REGULATORY_AND_NVM_GROUP, NVM_GET_INFO)};
    int ret;
    bool lar_fw_supported = !iwlwifi_mod_params.lar_disable &&
                            fw_has_capa(&fw->ucode_capa, IWL_UCODE_TLV_CAPA_LAR_SUPPORT);
    bool empty_otp;
    uint32_t mac_flags;
    uint32_t sbands_flags = 0;

    ret = iwl_trans_send_cmd(trans, &hcmd);
    if (ret) { return ERR_PTR(ret); }

    if (WARN(iwl_rx_packet_payload_len(hcmd.resp_pkt) != sizeof(*rsp),
             "Invalid payload len in NVM response from FW %d",
             iwl_rx_packet_payload_len(hcmd.resp_pkt))) {
        ret = -EINVAL;
        goto out;
    }

    rsp = (void*)hcmd.resp_pkt->data;
    empty_otp = !!(le32_to_cpu(rsp->general.flags) & NVM_GENERAL_FLAGS_EMPTY_OTP);
    if (empty_otp) { IWL_INFO(trans, "OTP is empty\n"); }

    nvm = kzalloc(sizeof(*nvm) + sizeof(struct ieee80211_channel) * IWL_NUM_CHANNELS, GFP_KERNEL);
    if (!nvm) {
        ret = -ENOMEM;
        goto out;
    }

    iwl_set_hw_address_from_csr(trans, nvm);
    /* TODO: if platform NVM has MAC address - override it here */

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
    if (trans->dbg_cfg.hw_address.len) {
        if (trans->dbg_cfg.hw_address.len == ETH_ALEN &&
            is_valid_ether_addr(trans->dbg_cfg.hw_address.data)) {
            memcpy(nvm->hw_addr, trans->dbg_cfg.hw_address.data, ETH_ALEN);
        } else {
            IWL_ERR(trans, "mac address from config file is invalid\n");
        }
    }
#endif
    if (!is_valid_ether_addr(nvm->hw_addr)) {
        IWL_ERR(trans, "no valid mac address was found\n");
        ret = -EINVAL;
        goto err_free;
    }

    IWL_INFO(trans, "base HW address: %pM\n", nvm->hw_addr);

    /* Initialize general data */
    nvm->nvm_version = le16_to_cpu(rsp->general.nvm_version);
    nvm->n_hw_addrs = rsp->general.n_hw_addrs;
    if (nvm->n_hw_addrs == 0)
        IWL_WARN(trans, "Firmware declares no reserved mac addresses. OTP is empty: %d\n",
                 empty_otp);

    /* Initialize MAC sku data */
    mac_flags = le32_to_cpu(rsp->mac_sku.mac_sku_flags);
    nvm->sku_cap_11ac_enable = !!(mac_flags & NVM_MAC_SKU_FLAGS_802_11AC_ENABLED);
    nvm->sku_cap_11n_enable = !!(mac_flags & NVM_MAC_SKU_FLAGS_802_11N_ENABLED);
    nvm->sku_cap_11ax_enable = !!(mac_flags & NVM_MAC_SKU_FLAGS_802_11AX_ENABLED);
    nvm->sku_cap_band_24ghz_enable = !!(mac_flags & NVM_MAC_SKU_FLAGS_BAND_2_4_ENABLED);
    nvm->sku_cap_band_52ghz_enable = !!(mac_flags & NVM_MAC_SKU_FLAGS_BAND_5_2_ENABLED);
    nvm->sku_cap_mimo_disabled = !!(mac_flags & NVM_MAC_SKU_FLAGS_MIMO_DISABLED);

    /* Initialize PHY sku data */
    nvm->valid_tx_ant = (uint8_t)le32_to_cpu(rsp->phy_sku.tx_chains);
    nvm->valid_rx_ant = (uint8_t)le32_to_cpu(rsp->phy_sku.rx_chains);

    if (le32_to_cpu(rsp->regulatory.lar_enabled) && lar_fw_supported) {
        nvm->lar_enabled = true;
        sbands_flags |= IWL_NVM_SBANDS_FLAGS_LAR;
    }

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
    iwl_init_he_override(trans, &nvm->bands[NL80211_BAND_2GHZ]);
    iwl_init_he_override(trans, &nvm->bands[NL80211_BAND_5GHZ]);
#endif
    iwl_init_sbands(trans, nvm, rsp->regulatory.channel_profile,
                    nvm->valid_tx_ant & fw->valid_tx_ant, nvm->valid_rx_ant & fw->valid_rx_ant,
                    sbands_flags);

    iwl_free_resp(&hcmd);
    return nvm;

err_free:
    kfree(nvm);
out:
    iwl_free_resp(&hcmd);
    return ERR_PTR(ret);
}
IWL_EXPORT_SYMBOL(iwl_get_nvm);
