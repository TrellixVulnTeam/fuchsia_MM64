/******************************************************************************
 *
 * Copyright(c) 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2014 Intel Mobile Communications GmbH
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
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_DNT_CFG_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_DNT_CFG_H_

#include <linux/kthread.h>
#include <linux/sched.h>

#include "iwl-config.h"
#include "iwl-drv.h"
#include "iwl-op-mode.h"
#include "iwl-trans.h"

#define IWL_DNT_ARRAY_SIZE 128

#define BUS_TYPE_PCI "pci"
#define BUS_TYPE_SDIO "sdio"

#define GET_RX_PACKET_SIZE(pkt) \
    ((le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK) - sizeof(struct iwl_cmd_header))

#define MONITOR_INPUT_MODE_MASK 0x01
#define UCODE_MSGS_INPUT_MODE_MASK 0x02

/* DnT status */
enum {
    IWL_DNT_STATUS_MON_CONFIGURED = BIT(0),
    IWL_DNT_STATUS_UCODE_MSGS_CONFIGURED = BIT(1),
    IWL_DNT_STATUS_DMA_BUFFER_ALLOCATED = BIT(2),
    IWL_DNT_STATUS_FAILED_TO_ALLOCATE_DMA = BIT(3),
    IWL_DNT_STATUS_FAILED_START_MONITOR = BIT(4),
    IWL_DNT_STATUS_INVALID_MONITOR_CONF = BIT(5),
    IWL_DNT_STATUS_FAILED_TO_ALLOCATE_DB = BIT(6),
    IWL_DNT_STATUS_FW_CRASH = BIT(7),
};

/* input modes */
enum { MONITOR = BIT(0), UCODE_MESSAGES = BIT(1) };

/* output modes */
enum { NETLINK = BIT(0), DEBUGFS = BIT(1), FTRACE = BIT(2) };

/* monitor types */
enum {
    NO_MONITOR = 0,
    MIPI = BIT(0),
    INTERFACE = BIT(1),
    DMA = BIT(2),
    MARBH_ADC = BIT(3),
    MARBH_DBG = BIT(4),
    SMEM = BIT(5)
};

/* monitor modes */
enum { NO_MON, DEBUG, SNIFFER, DBGC };

/* incoming mode */
enum { NO_IN, COLLECT, RETRIEVE };

/* outgoing mode */
enum { NO_OUT, PUSH, PULL };

/* crash data */
enum {
    NONE = 0,
    SRAM = BIT(0),
    DBGM = BIT(1),
    TX_FIFO = BIT(2),
    RX_FIFO = BIT(3),
    PERIPHERY = BIT(4)
};

struct dnt_collect_entry {
    uint8_t* data;
    uint32_t size;
};

/**
 * @list_read_ptr: list read pointer
 * @list_wr_ptr: list write pointer
 * @waitq: waitqueue for new data
 * @db_lock: lock for the list
 */
struct dnt_collect_db {
    struct dnt_collect_entry collect_array[IWL_DNT_ARRAY_SIZE];
    unsigned int read_ptr;
    unsigned int wr_ptr;
    wait_queue_head_t waitq;
    spinlock_t db_lock; /*locks the array */
};

/**
 * struct dnt_crash_data - holds pointers for crash data
 * @sram: sram data pointer
 * @dbgm: monitor data pointer
 * @rx: rx fifo data pointer
 * @tx: tx fifo data pointer
 * @periph: periphery registers data pointer
 */
struct dnt_crash_data {
    uint8_t* sram;
    uint32_t sram_buf_size;
    uint8_t* dbgm;
    uint32_t dbgm_buf_size;
    uint8_t* rx;
    uint32_t rx_buf_size;
    uint8_t* tx;
    uint32_t tx_buf_size;
    uint8_t* periph;
    uint32_t periph_buf_size;
};

/**
 * struct iwl_dnt_dispatch - the iwl Debug and Trace Dispatch
 *
 * @in_mode: The dispatch incoming data mode
 * @out_mode: The dispatch outgoing data mode
 * @dbgm_list: dbgm link list
 * @um_list: uCodeMessages link list
 */
struct iwl_dnt_dispatch {
    uint32_t mon_in_mode;
    uint32_t mon_out_mode;
    uint32_t mon_output;

    uint32_t ucode_msgs_in_mode;
    uint32_t ucode_msgs_out_mode;
    uint32_t ucode_msgs_output;

    uint32_t crash_out_mode;

    struct dnt_collect_db* dbgm_db;
    struct dnt_collect_db* um_db;

    struct dnt_crash_data crash;
};

/**
 * struct iwl_dnt - the iwl Debug and Trace
 *
 * @cfg: pointer to user configuration
 * @dev: pointer to struct device for printing purposes
 * @iwl_dnt_status: represents the DnT status
 * @is_configuration_valid: indicates whether the persistent configuration
 *  is valid or not
 * @cur_input_mask: current mode mask
 * @cur_output_mask: current output mask
 * @cur_mon_type: current monitor type
 * @mon_buf_cpu_addr: DMA buffer CPU address
 * @mon_dma_addr: DMA buffer address
 * @mon_base_addr: monitor dma buffer start address
 * @mon_end_addr: monitor dma buffer end address
 * @mon_buf_size: monitor dma buffer size
 * @cur_mon_mode: current monitor mode (DBGM/SNIFFER)
 * @dispatch: a pointer to dispatch
 */
struct iwl_dnt {
    struct device* dev;
    const struct fw_img* image;

    uint32_t iwl_dnt_status;
    bool is_configuration_valid;
    uint8_t cur_input_mask;
    uint8_t cur_output_mask;

    uint32_t cur_mon_type;
    uint8_t* mon_buf_cpu_addr;
    dma_addr_t mon_dma_addr;
    uint64_t mon_base_addr;
    uint64_t mon_end_addr;
    uint32_t mon_buf_size;
    uint32_t cur_mon_mode;

    struct iwl_dnt_dispatch dispatch;
#ifdef CPTCFG_IWLWIFI_DEBUGFS
    uint8_t debugfs_counter;
    wait_queue_head_t debugfs_waitq;
    struct dentry* debugfs_entry;
#endif
};

/**
 * iwl_dnt_init - initialize iwl_dnt.
 *
 */
void iwl_dnt_init(struct iwl_trans* trans, struct dentry* dbgfs_dir);

/**
 * iwl_dnt_free - free iwl_dnt.
 *
 */
void iwl_dnt_free(struct iwl_trans* trans);

/**
 * iwl_dnt_configure - configures iwl_dnt.
 *
 */
void iwl_dnt_configure(struct iwl_trans* trans, const struct fw_img* image);

/**
 * iwl_dnt_start - starts monitor and set log level
 *
 */
void iwl_dnt_start(struct iwl_trans* trans);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_DNT_CFG_H_
