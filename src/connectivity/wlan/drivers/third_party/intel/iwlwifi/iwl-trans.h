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
 *
 *****************************************************************************/
#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_TRANS_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_TRANS_H_

#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/img.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-config.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-debug.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-op-mode.h"
#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-dbg-cfg.h"
#endif
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/cmdhdr.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/txq.h"
#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/testmode.h"
#endif
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/fw/api/dbg-tlv.h"
#include "src/connectivity/wlan/drivers/third_party/intel/iwlwifi/iwl-dbg-tlv.h"

/**
 * DOC: Transport layer - what is it ?
 *
 * The transport layer is the layer that deals with the HW directly. It provides
 * an abstraction of the underlying HW to the upper layer. The transport layer
 * doesn't provide any policy, algorithm or anything of this kind, but only
 * mechanisms to make the HW do something. It is not completely stateless but
 * close to it.
 * We will have an implementation for each different supported bus.
 */

/**
 * DOC: Life cycle of the transport layer
 *
 * The transport layer has a very precise life cycle.
 *
 *  1) A helper function is called during the module initialization and
 *     registers the bus driver's ops with the transport's alloc function.
 *  2) Bus's probe calls to the transport layer's allocation functions.
 *     Of course this function is bus specific.
 *  3) This allocation functions will spawn the upper layer which will
 *     register mac80211.
 *
 *  4) At some point (i.e. mac80211's start call), the op_mode will call
 *     the following sequence:
 *     start_hw
 *     start_fw
 *
 *  5) Then when finished (or reset):
 *     stop_device
 *
 *  6) Eventually, the free function will be called.
 */

#define FH_RSCSR_FRAME_SIZE_MSK 0x00003FFF /* bits 0-13 */
#define FH_RSCSR_FRAME_INVALID 0x55550000
#define FH_RSCSR_FRAME_ALIGN 0x40
#define FH_RSCSR_RPA_EN BIT(25)
#define FH_RSCSR_RADA_EN BIT(26)
#define FH_RSCSR_RXQ_POS 16
#define FH_RSCSR_RXQ_MASK 0x3F0000

struct iwl_rx_packet {
    /*
     * The first 4 bytes of the RX frame header contain both the RX frame
     * size and some flags.
     * Bit fields:
     * 31:    flag flush RB request
     * 30:    flag ignore TC (terminal counter) request
     * 29:    flag fast IRQ request
     * 28-27: Reserved
     * 26:    RADA enabled
     * 25:    Offload enabled
     * 24:    RPF enabled
     * 23:    RSS enabled
     * 22:    Checksum enabled
     * 21-16: RX queue
     * 15-14: Reserved
     * 13-00: RX frame size
     */
    __le32 len_n_flags;
    struct iwl_cmd_header hdr;
    uint8_t data[];
} __packed;

static inline uint32_t iwl_rx_packet_len(const struct iwl_rx_packet* pkt) {
    return le32_to_cpu(pkt->len_n_flags) & FH_RSCSR_FRAME_SIZE_MSK;
}

static inline uint32_t iwl_rx_packet_payload_len(const struct iwl_rx_packet* pkt) {
    return iwl_rx_packet_len(pkt) - sizeof(pkt->hdr);
}

/**
 * enum CMD_MODE - how to send the host commands ?
 *
 * @CMD_ASYNC: Return right away and don't wait for the response
 * @CMD_WANT_SKB: Not valid with CMD_ASYNC. The caller needs the buffer of
 *  the response. The caller needs to call iwl_free_resp when done.
 * @CMD_HIGH_PRIO: The command is high priority - it goes to the front of the
 *  command queue, but after other high priority commands. Valid only
 *  with CMD_ASYNC.
 * @CMD_SEND_IN_IDLE: The command should be sent even when the trans is idle.
 * @CMD_MAKE_TRANS_IDLE: The command response should mark the trans as idle.
 * @CMD_WAKE_UP_TRANS: The command response should wake up the trans
 *  (i.e. mark it as non-idle).
 * @CMD_WANT_ASYNC_CALLBACK: the op_mode's async callback function must be
 *  called after this command completes. Valid only with CMD_ASYNC.
 */
enum CMD_MODE {
    CMD_ASYNC = BIT(0),
    CMD_WANT_SKB = BIT(1),
    CMD_SEND_IN_RFKILL = BIT(2),
    CMD_HIGH_PRIO = BIT(3),
    CMD_SEND_IN_IDLE = BIT(4),
    CMD_MAKE_TRANS_IDLE = BIT(5),
    CMD_WAKE_UP_TRANS = BIT(6),
    CMD_WANT_ASYNC_CALLBACK = BIT(7),
};

#define DEF_CMD_PAYLOAD_SIZE 320

/**
 * struct iwl_device_cmd
 *
 * For allocation of the command and tx queues, this establishes the overall
 * size of the largest command we send to uCode, except for commands that
 * aren't fully copied and use other TFD space.
 */
struct iwl_device_cmd {
    union {
        struct {
            struct iwl_cmd_header hdr; /* uCode API */
            uint8_t payload[DEF_CMD_PAYLOAD_SIZE];
        };
        struct {
            struct iwl_cmd_header_wide hdr_wide;
            uint8_t payload_wide[DEF_CMD_PAYLOAD_SIZE - sizeof(struct iwl_cmd_header_wide) +
                                 sizeof(struct iwl_cmd_header)];
        };
    };
} __packed;

#define TFD_MAX_PAYLOAD_SIZE (sizeof(struct iwl_device_cmd))

/*
 * number of transfer buffers (fragments) per transmit frame descriptor;
 * this is just the driver's idea, the hardware supports 20
 */
#define IWL_MAX_CMD_TBS_PER_TFD 2

/**
 * enum iwl_hcmd_dataflag - flag for each one of the chunks of the command
 *
 * @IWL_HCMD_DFL_NOCOPY: By default, the command is copied to the host command's
 *  ring. The transport layer doesn't map the command's buffer to DMA, but
 *  rather copies it to a previously allocated DMA buffer. This flag tells
 *  the transport layer not to copy the command, but to map the existing
 *  buffer (that is passed in) instead. This saves the memcpy and allows
 *  commands that are bigger than the fixed buffer to be submitted.
 *  Note that a TFD entry after a NOCOPY one cannot be a normal copied one.
 * @IWL_HCMD_DFL_DUP: Only valid without NOCOPY, duplicate the memory for this
 *  chunk internally and free it again after the command completes. This
 *  can (currently) be used only once per command.
 *  Note that a TFD entry after a DUP one cannot be a normal copied one.
 */
enum iwl_hcmd_dataflag {
    IWL_HCMD_DFL_NOCOPY = BIT(0),
    IWL_HCMD_DFL_DUP = BIT(1),
};

/**
 * struct iwl_host_cmd - Host command to the uCode
 *
 * @data: array of chunks that composes the data of the host command
 * @resp_pkt: response packet, if %CMD_WANT_SKB was set
 * @_rx_page_order: (internally used to free response packet)
 * @_rx_page_addr: (internally used to free response packet)
 * @flags: can be CMD_*
 * @len: array of the lengths of the chunks in data
 * @dataflags: IWL_HCMD_DFL_*
 * @id: command id of the host command, for wide commands encoding the
 *  version and group as well
 */
struct iwl_host_cmd {
    const void* data[IWL_MAX_CMD_TBS_PER_TFD];
    struct iwl_rx_packet* resp_pkt;
    unsigned long _rx_page_addr;
    uint32_t _rx_page_order;

    uint32_t flags;
    uint32_t id;
    uint16_t len[IWL_MAX_CMD_TBS_PER_TFD];
    uint8_t dataflags[IWL_MAX_CMD_TBS_PER_TFD];
};

#if 0   // NEEDS_PORTING
static inline void iwl_free_resp(struct iwl_host_cmd* cmd) {
    free_pages(cmd->_rx_page_addr, cmd->_rx_page_order);
}
#endif  // NEEDS_PORTING

struct iwl_rx_cmd_buffer {
    struct page* _page;
    int _offset;
    bool _page_stolen;
    uint32_t _rx_page_order;
    unsigned int truesize;
    uint8_t status;
};

static inline void* rxb_addr(struct iwl_rx_cmd_buffer* r) {
    return (void*)((unsigned long)page_address(r->_page) + r->_offset);
}

static inline int rxb_offset(struct iwl_rx_cmd_buffer* r) {
    return r->_offset;
}

static inline struct page* rxb_steal_page(struct iwl_rx_cmd_buffer* r) {
    r->_page_stolen = true;
#if 0   // NEEDS_PORTING
    get_page(r->_page);
#endif  // NEEDS_PORTING
    return r->_page;
}

#if 0   // NEEDS_PORTING
static inline void iwl_free_rxb(struct iwl_rx_cmd_buffer* r) {
    __free_pages(r->_page, r->_rx_page_order);
}
#endif  // NEEDS_PORTING

#define MAX_NO_RECLAIM_CMDS 6

#define IWL_MASK(lo, hi) ((1 << (hi)) | ((1 << (hi)) - (1 << (lo))))

/*
 * Maximum number of HW queues the transport layer
 * currently supports
 */
#define IWL_MAX_HW_QUEUES 32
#define IWL_MAX_TVQM_QUEUES 512

#define IWL_MAX_TID_COUNT 8
#define IWL_MGMT_TID 15
#define IWL_FRAME_LIMIT 64
#define IWL_MAX_RX_HW_QUEUES 16

/**
 * enum iwl_wowlan_status - WoWLAN image/device status
 * @IWL_D3_STATUS_ALIVE: firmware is still running after resume
 * @IWL_D3_STATUS_RESET: device was reset while suspended
 */
enum iwl_d3_status {
    IWL_D3_STATUS_ALIVE,
    IWL_D3_STATUS_RESET,
};

/**
 * enum iwl_trans_status: transport status flags
 * @STATUS_SYNC_HCMD_ACTIVE: a SYNC command is being processed
 * @STATUS_DEVICE_ENABLED: APM is enabled
 * @STATUS_TPOWER_PMI: the device might be asleep (need to wake it up)
 * @STATUS_INT_ENABLED: interrupts are enabled
 * @STATUS_RFKILL_HW: the actual HW state of the RF-kill switch
 * @STATUS_RFKILL_OPMODE: RF-kill state reported to opmode
 * @STATUS_FW_ERROR: the fw is in error state
 * @STATUS_TRANS_GOING_IDLE: shutting down the trans, only special commands
 *  are sent
 * @STATUS_TRANS_IDLE: the trans is idle - general commands are not to be sent
 * @STATUS_TA_ACTIVE: target access is in progress
 * @STATUS_TRANS_DEAD: trans is dead - avoid any read/write operation
 */
enum iwl_trans_status {
    STATUS_SYNC_HCMD_ACTIVE,
    STATUS_DEVICE_ENABLED,
    STATUS_TPOWER_PMI,
    STATUS_INT_ENABLED,
    STATUS_RFKILL_HW,
    STATUS_RFKILL_OPMODE,
    STATUS_FW_ERROR,
    STATUS_TRANS_GOING_IDLE,
    STATUS_TRANS_IDLE,
    STATUS_TA_ACTIVE,
    STATUS_TRANS_DEAD,
};

#if 0   // NEEDS_PORTING
static inline int
iwl_trans_get_rb_size_order(enum iwl_amsdu_size rb_size) {
    switch (rb_size) {
    case IWL_AMSDU_2K:
        return get_order(2 * 1024);
    case IWL_AMSDU_4K:
        return get_order(4 * 1024);
    case IWL_AMSDU_8K:
        return get_order(8 * 1024);
    case IWL_AMSDU_12K:
        return get_order(12 * 1024);
    default:
        WARN_ON(1);
        return -1;
    }
}
#endif  // NEEDS_PORTING

struct iwl_hcmd_names {
    uint8_t cmd_id;
    const char* const cmd_name;
};

#define HCMD_NAME(x) \
    { .cmd_id = x, .cmd_name = #x }

struct iwl_hcmd_arr {
    const struct iwl_hcmd_names* arr;
    int size;
};

#define HCMD_ARR(x) \
    { .arr = x, .size = ARRAY_SIZE(x) }

/**
 * struct iwl_trans_config - transport configuration
 *
 * @op_mode: pointer to the upper layer.
 * @cmd_queue: the index of the command queue.
 *  Must be set before start_fw.
 * @cmd_fifo: the fifo for host commands
 * @cmd_q_wdg_timeout: the timeout of the watchdog timer for the command queue.
 * @no_reclaim_cmds: Some devices erroneously don't set the
 *  SEQ_RX_FRAME bit on some notifications, this is the
 *  list of such notifications to filter. Max length is
 *  %MAX_NO_RECLAIM_CMDS.
 * @n_no_reclaim_cmds: # of commands in list
 * @rx_buf_size: RX buffer size needed for A-MSDUs
 *  if unset 4k will be the RX buffer size
 * @bc_table_dword: set to true if the BC table expects the byte count to be
 *  in DWORD (as opposed to bytes)
 * @scd_set_active: should the transport configure the SCD for HCMD queue
 * @sw_csum_tx: transport should compute the TCP checksum
 * @command_groups: array of command groups, each member is an array of the
 *  commands in the group; for debugging only
 * @command_groups_size: number of command groups, to avoid illegal access
 * @cb_data_offs: offset inside skb->cb to store transport data at, must have
 *  space for at least two pointers
 */
struct iwl_trans_config {
    struct iwl_op_mode* op_mode;

    uint8_t cmd_queue;
    uint8_t cmd_fifo;
    unsigned int cmd_q_wdg_timeout;
    const uint8_t* no_reclaim_cmds;
    unsigned int n_no_reclaim_cmds;

    enum iwl_amsdu_size rx_buf_size;
    bool bc_table_dword;
    bool scd_set_active;
    bool sw_csum_tx;
    const struct iwl_hcmd_arr* command_groups;
    int command_groups_size;

    uint8_t cb_data_offs;
};

struct iwl_trans_dump_data {
    uint32_t len;
    uint8_t data[];
};

struct iwl_trans;

struct iwl_trans_txq_scd_cfg {
    uint8_t fifo;
    uint8_t sta_id;
    uint8_t tid;
    bool aggregate;
    int frame_limit;
};

/**
 * struct iwl_trans_rxq_dma_data - RX queue DMA data
 * @fr_bd_cb: DMA address of free BD cyclic buffer
 * @fr_bd_wid: Initial write index of the free BD cyclic buffer
 * @urbd_stts_wrptr: DMA address of urbd_stts_wrptr
 * @ur_bd_cb: DMA address of used BD cyclic buffer
 */
struct iwl_trans_rxq_dma_data {
    uint64_t fr_bd_cb;
    uint32_t fr_bd_wid;
    uint64_t urbd_stts_wrptr;
    uint64_t ur_bd_cb;
};

/**
 * struct iwl_trans_ops - transport specific operations
 *
 * All the handlers MUST be implemented
 *
 * @start_hw: starts the HW. If low_power is true, the NIC needs to be taken
 *  out of a low power state. From that point on, the HW can send
 *  interrupts. May sleep.
 * @op_mode_leave: Turn off the HW RF kill indication if on
 *  May sleep
 * @start_fw: allocates and inits all the resources for the transport
 *  layer. Also kick a fw image.
 *  May sleep
 * @fw_alive: called when the fw sends alive notification. If the fw provides
 *  the SCD base address in SRAM, then provide it here, or 0 otherwise.
 *  May sleep
 * @stop_device: stops the whole device (embedded CPU put to reset) and stops
 *  the HW. If low_power is true, the NIC will be put in low power state.
 *  From that point on, the HW will be stopped but will still issue an
 *  interrupt if the HW RF kill switch is triggered.
 *  This callback must do the right thing and not crash even if %start_hw()
 *  was called but not &start_fw(). May sleep.
 * @d3_suspend: put the device into the correct mode for WoWLAN during
 *  suspend. This is optional, if not implemented WoWLAN will not be
 *  supported. This callback may sleep.
 * @d3_resume: resume the device after WoWLAN, enabling the opmode to
 *  talk to the WoWLAN image to get its status. This is optional, if not
 *  implemented WoWLAN will not be supported. This callback may sleep.
 * @send_cmd:send a host command. Must return -ERFKILL if RFkill is asserted.
 *  If RFkill is asserted in the middle of a SYNC host command, it must
 *  return -ERFKILL straight away.
 *  May sleep only if CMD_ASYNC is not set
 * @tx: send an skb. The transport relies on the op_mode to zero the
 *  the ieee80211_tx_info->driver_data. If the MPDU is an A-MSDU, all
 *  the CSUM will be taken care of (TCP CSUM and IP header in case of
 *  IPv4). If the MPDU is a single MSDU, the op_mode must compute the IP
 *  header if it is IPv4.
 *  Must be atomic
 * @reclaim: free packet until ssn. Returns a list of freed packets.
 *  Must be atomic
 * @txq_enable: setup a queue. To setup an AC queue, use the
 *  iwl_trans_ac_txq_enable wrapper. fw_alive must have been called before
 *  this one. The op_mode must not configure the HCMD queue. The scheduler
 *  configuration may be %NULL, in which case the hardware will not be
 *  configured. If true is returned, the operation mode needs to increment
 *  the sequence number of the packets routed to this queue because of a
 *  hardware scheduler bug. May sleep.
 * @txq_disable: de-configure a Tx queue to send AMPDUs
 *  Must be atomic
 * @txq_set_shared_mode: change Tx queue shared/unshared marking
 * @wait_tx_queues_empty: wait until tx queues are empty. May sleep.
 * @wait_txq_empty: wait until specific tx queue is empty. May sleep.
 * @freeze_txq_timer: prevents the timer of the queue from firing until the
 *  queue is set to awake. Must be atomic.
 * @block_txq_ptrs: stop updating the write pointers of the Tx queues. Note
 *  that the transport needs to refcount the calls since this function
 *  will be called several times with block = true, and then the queues
 *  need to be unblocked only after the same number of calls with
 *  block = false.
 * @write8: write a uint8_t to a register at offset ofs from the BAR
 * @write32: write a uint32_t to a register at offset ofs from the BAR
 * @read32: read a uint32_t register at offset ofs from the BAR
 * @read_prph: read a DWORD from a periphery register
 * @write_prph: write a DWORD to a periphery register
 * @read_mem: read device's SRAM in DWORD
 * @write_mem: write device's SRAM in DWORD. If %buf is %NULL, then the memory
 *  will be zeroed.
 * @configure: configure parameters required by the transport layer from
 *  the op_mode. May be called several times before start_fw, can't be
 *  called after that.
 * @set_pmi: set the power pmi state
 * @grab_nic_access: wake the NIC to be able to access non-HBUS regs.
 *  Sleeping is not allowed between grab_nic_access and
 *  release_nic_access.
 * @release_nic_access: let the NIC go to sleep. The "flags" parameter
 *  must be the same one that was sent before to the grab_nic_access.
 * @set_bits_mask - set SRAM register according to value and mask.
 * @ref: grab a reference to the transport/FW layers, disallowing
 *  certain low power states
 * @unref: release a reference previously taken with @ref. Note that
 *  initially the reference count is 1, making an initial @unref
 *  necessary to allow low power states.
 * @dump_data: return a vmalloc'ed buffer with debug data, maybe containing last
 *  TX'ed commands and similar. The buffer will be vfree'd by the caller.
 *  Note that the transport must fill in the proper file headers.
 * @debugfs_cleanup: used in the driver unload flow to make a proper cleanup
 *  of the trans debugfs
 */
struct iwl_trans_ops {
    int (*start_hw)(struct iwl_trans* iwl_trans, bool low_power);
    void (*op_mode_leave)(struct iwl_trans* iwl_trans);
#if IS_ENABLED(CPTCFG_IWLXVT)
    int (*start_fw_dbg)(struct iwl_trans* trans, const struct fw_img* fw, bool run_in_rfkill,
                        uint32_t fw_dbg_flags);
    int (*test_mode_cmd)(struct iwl_trans* trans, bool enable);
#endif
    int (*start_fw)(struct iwl_trans* trans, const struct fw_img* fw, bool run_in_rfkill);
    void (*fw_alive)(struct iwl_trans* trans, uint32_t scd_addr);
    void (*stop_device)(struct iwl_trans* trans, bool low_power);

    void (*d3_suspend)(struct iwl_trans* trans, bool test, bool reset);
    int (*d3_resume)(struct iwl_trans* trans, enum iwl_d3_status* status, bool test, bool reset);

    int (*send_cmd)(struct iwl_trans* trans, struct iwl_host_cmd* cmd);

    int (*tx)(struct iwl_trans* trans, struct sk_buff* skb, struct iwl_device_cmd* dev_cmd,
              int queue);
    void (*reclaim)(struct iwl_trans* trans, int queue, int ssn, struct sk_buff_head* skbs);

    bool (*txq_enable)(struct iwl_trans* trans, int queue, uint16_t ssn,
                       const struct iwl_trans_txq_scd_cfg* cfg, unsigned int queue_wdg_timeout);
    void (*txq_disable)(struct iwl_trans* trans, int queue, bool configure_scd);
    /* 22000 functions */
    int (*txq_alloc)(struct iwl_trans* trans, __le16 flags, uint8_t sta_id, uint8_t tid, int cmd_id,
                     int size, unsigned int queue_wdg_timeout);
    void (*txq_free)(struct iwl_trans* trans, int queue);
    int (*rxq_dma_data)(struct iwl_trans* trans, int queue, struct iwl_trans_rxq_dma_data* data);

    void (*txq_set_shared_mode)(struct iwl_trans* trans, uint32_t txq_id, bool shared);

    int (*wait_tx_queues_empty)(struct iwl_trans* trans, uint32_t txq_bm);
    int (*wait_txq_empty)(struct iwl_trans* trans, int queue);
    void (*freeze_txq_timer)(struct iwl_trans* trans, unsigned long txqs, bool freeze);
    void (*block_txq_ptrs)(struct iwl_trans* trans, bool block);

    void (*write8)(struct iwl_trans* trans, uint32_t ofs, uint8_t val);
    void (*write32)(struct iwl_trans* trans, uint32_t ofs, uint32_t val);
    uint32_t (*read32)(struct iwl_trans* trans, uint32_t ofs);
    uint32_t (*read_prph)(struct iwl_trans* trans, uint32_t ofs);
    void (*write_prph)(struct iwl_trans* trans, uint32_t ofs, uint32_t val);
    int (*read_mem)(struct iwl_trans* trans, uint32_t addr, void* buf, int dwords);
    int (*write_mem)(struct iwl_trans* trans, uint32_t addr, const void* buf, int dwords);
    void (*configure)(struct iwl_trans* trans, const struct iwl_trans_config* trans_cfg);
    void (*set_pmi)(struct iwl_trans* trans, bool state);
    void (*sw_reset)(struct iwl_trans* trans);
    bool (*grab_nic_access)(struct iwl_trans* trans, unsigned long* flags);
    void (*release_nic_access)(struct iwl_trans* trans, unsigned long* flags);
    void (*set_bits_mask)(struct iwl_trans* trans, uint32_t reg, uint32_t mask, uint32_t value);
    void (*ref)(struct iwl_trans* trans);
    void (*unref)(struct iwl_trans* trans);
    int (*suspend)(struct iwl_trans* trans);
    void (*resume)(struct iwl_trans* trans);

    struct iwl_trans_dump_data* (*dump_data)(struct iwl_trans* trans, uint32_t dump_mask);
    void (*debugfs_cleanup)(struct iwl_trans* trans);
};

/**
 * enum iwl_trans_state - state of the transport layer
 *
 * @IWL_TRANS_NO_FW: no fw has sent an alive response
 * @IWL_TRANS_FW_ALIVE: a fw has sent an alive response
 */
enum iwl_trans_state {
    IWL_TRANS_NO_FW = 0,
    IWL_TRANS_FW_ALIVE = 1,
};

/**
 * DOC: Platform power management
 *
 * There are two types of platform power management: system-wide
 * (WoWLAN) and runtime.
 *
 * In system-wide power management the entire platform goes into a low
 * power state (e.g. idle or suspend to RAM) at the same time and the
 * device is configured as a wakeup source for the entire platform.
 * This is usually triggered by userspace activity (e.g. the user
 * presses the suspend button or a power management daemon decides to
 * put the platform in low power mode).  The device's behavior in this
 * mode is dictated by the wake-on-WLAN configuration.
 *
 * In runtime power management, only the devices which are themselves
 * idle enter a low power state.  This is done at runtime, which means
 * that the entire system is still running normally.  This mode is
 * usually triggered automatically by the device driver and requires
 * the ability to enter and exit the low power modes in a very short
 * time, so there is not much impact in usability.
 *
 * The terms used for the device's behavior are as follows:
 *
 *  - D0: the device is fully powered and the host is awake;
 *  - D3: the device is in low power mode and only reacts to
 *      specific events (e.g. magic-packet received or scan
 *      results found);
 *  - D0I3: the device is in low power mode and reacts to any
 *      activity (e.g. RX);
 *
 * These terms reflect the power modes in the firmware and are not to
 * be confused with the physical device power state.  The NIC can be
 * in D0I3 mode even if, for instance, the PCI device is in D3 state.
 */

/**
 * enum iwl_plat_pm_mode - platform power management mode
 *
 * This enumeration describes the device's platform power management
 * behavior when in idle mode (i.e. runtime power management) or when
 * in system-wide suspend (i.e WoWLAN).
 *
 * @IWL_PLAT_PM_MODE_DISABLED: power management is disabled for this
 *  device.  At runtime, this means that nothing happens and the
 *  device always remains in active.  In system-wide suspend mode,
 *  it means that the all connections will be closed automatically
 *  by mac80211 before the platform is suspended.
 * @IWL_PLAT_PM_MODE_D3: the device goes into D3 mode (i.e. WoWLAN).
 *  For runtime power management, this mode is not officially
 *  supported.
 * @IWL_PLAT_PM_MODE_D0I3: the device goes into D0I3 mode.
 */
enum iwl_plat_pm_mode {
    IWL_PLAT_PM_MODE_DISABLED,
    IWL_PLAT_PM_MODE_D3,
    IWL_PLAT_PM_MODE_D0I3,
};

/* Max time to wait for trans to become idle/non-idle on d0i3
 * enter/exit (in msecs).
 */
#define IWL_TRANS_IDLE_TIMEOUT (CPTCFG_IWL_TIMEOUT_FACTOR * 2000)

/**
 * struct iwl_dram_data
 * @physical: page phy pointer
 * @block: pointer to the allocated block/page
 * @size: size of the block/page
 */
struct iwl_dram_data {
    dma_addr_t physical;
    void* block;
    int size;
};

/**
 * struct iwl_trans - transport common data
 *
 * @ops - pointer to iwl_trans_ops
 * @op_mode - pointer to the op_mode
 * @cfg - pointer to the configuration
 * @drv - pointer to iwl_drv
 * @status: a bit-mask of transport status flags
 * @dev - pointer to struct device * that represents the device
 * @max_skb_frags: maximum number of fragments an SKB can have when transmitted.
 *  0 indicates that frag SKBs (NETIF_F_SG) aren't supported.
 * @hw_rf_id a uint32_t with the device RF ID
 * @hw_id: a uint32_t with the ID of the device / sub-device.
 *  Set during transport allocation.
 * @hw_id_str: a string with info about HW ID. Set during transport allocation.
 * @pm_support: set to true in start_hw if link pm is supported
 * @ltr_enabled: set to true if the LTR is enabled
 * @wide_cmd_header: true when ucode supports wide command header format
 * @num_rx_queues: number of RX queues allocated by the transport;
 *  the transport must set this before calling iwl_drv_start()
 * @iml_len: the length of the image loader
 * @iml: a pointer to the image loader itself
 * @dev_cmd_pool: pool for Tx cmd allocation - for internal use only.
 *  The user should use iwl_trans_{alloc,free}_tx_cmd.
 * @rx_mpdu_cmd: MPDU RX command ID, must be assigned by opmode before
 *  starting the firmware, used for tracing
 * @rx_mpdu_cmd_hdr_size: used for tracing, amount of data before the
 *  start of the 802.11 header in the @rx_mpdu_cmd
 * @dflt_pwr_limit: default power limit fetched from the platform (ACPI)
 * @dbg_dest_tlv: points to the destination TLV for debug
 * @dbg_conf_tlv: array of pointers to configuration TLVs for debug
 * @dbg_trigger_tlv: array of pointers to triggers TLVs for debug
 * @dbg_n_dest_reg: num of reg_ops in %dbg_dest_tlv
 * @num_blocks: number of blocks in fw_mon
 * @fw_mon: address of the buffers for firmware monitor
 * @system_pm_mode: the system-wide power management mode in use.
 *  This mode is set dynamically, depending on the WoWLAN values
 *  configured from the userspace at runtime.
 * @runtime_pm_mode: the runtime power management mode in use.  This
 *  mode is set during the initialization phase and is not
 *  supposed to change during runtime.
 * @dbg_rec_on: true iff there is a fw debug recording currently active
 */
struct iwl_trans {
    const struct iwl_trans_ops* ops;
    struct iwl_op_mode* op_mode;
    const struct iwl_cfg* cfg;
    struct iwl_drv* drv;
    struct iwl_tm_gnl_dev* tmdev;
    enum iwl_trans_state state;
    unsigned long status;

    zx_device_t* zxdev;
    struct device* dev;
    uint32_t max_skb_frags;
    uint32_t hw_rev;
    uint32_t hw_rf_id;
    uint32_t hw_id;
    char hw_id_str[52];

    uint8_t rx_mpdu_cmd, rx_mpdu_cmd_hdr_size;

    bool pm_support;
    bool ltr_enabled;

    const struct iwl_hcmd_arr* command_groups;
    int command_groups_size;
    bool wide_cmd_header;

    uint8_t num_rx_queues;

    size_t iml_len;
    uint8_t* iml;

    /* The following fields are internal only */
    struct kmem_cache* dev_cmd_pool;
    char dev_cmd_pool_name[50];

    struct dentry* dbgfs_dir;

#ifdef CONFIG_LOCKDEP
    struct lockdep_map sync_cmd_lockdep_map;
#endif

#ifdef CPTCFG_IWLWIFI_SUPPORT_DEBUG_OVERRIDES
    struct iwl_dbg_cfg dbg_cfg;
#endif
    struct iwl_apply_point_data apply_points[IWL_FW_INI_APPLY_NUM];
    struct iwl_apply_point_data apply_points_ext[IWL_FW_INI_APPLY_NUM];

    bool external_ini_loaded;
    bool ini_valid;

    const struct iwl_fw_dbg_dest_tlv_v1* dbg_dest_tlv;
    const struct iwl_fw_dbg_conf_tlv* dbg_conf_tlv[FW_DBG_CONF_MAX];
    struct iwl_fw_dbg_trigger_tlv* const* dbg_trigger_tlv;
    uint8_t dbg_n_dest_reg;
    int num_blocks;
    struct iwl_dram_data fw_mon[IWL_FW_INI_APPLY_NUM];

    enum iwl_plat_pm_mode system_pm_mode;
    enum iwl_plat_pm_mode runtime_pm_mode;
    bool suspending;
    bool dbg_rec_on;

#ifdef CPTCFG_IWLWIFI_DEVICE_TESTMODE
    struct iwl_testmode testmode;
#endif

    /* pointer to trans specific struct */
    /*Ensure that this pointer will always be aligned to sizeof pointer */
    char trans_specific[0] __aligned(sizeof(void*));
};

const char* iwl_get_cmd_string(struct iwl_trans* trans, uint32_t id);
int iwl_cmd_groups_verify_sorted(const struct iwl_trans_config* trans);

static inline void iwl_trans_configure(struct iwl_trans* trans,
                                       const struct iwl_trans_config* trans_cfg) {
    trans->op_mode = trans_cfg->op_mode;

    trans->ops->configure(trans, trans_cfg);
    WARN_ON(iwl_cmd_groups_verify_sorted(trans_cfg));
}

static inline int _iwl_trans_start_hw(struct iwl_trans* trans, bool low_power) {
    might_sleep();

    return trans->ops->start_hw(trans, low_power);
}

static inline int iwl_trans_start_hw(struct iwl_trans* trans) {
    return trans->ops->start_hw(trans, true);
}

static inline void iwl_trans_op_mode_leave(struct iwl_trans* trans) {
    might_sleep();

    if (trans->ops->op_mode_leave) { trans->ops->op_mode_leave(trans); }

    trans->op_mode = NULL;

    trans->state = IWL_TRANS_NO_FW;
}

static inline void iwl_trans_fw_alive(struct iwl_trans* trans, uint32_t scd_addr) {
    might_sleep();

    trans->state = IWL_TRANS_FW_ALIVE;

    trans->ops->fw_alive(trans, scd_addr);
}

static inline int iwl_trans_start_fw(struct iwl_trans* trans, const struct fw_img* fw,
                                     bool run_in_rfkill) {
    might_sleep();

    WARN_ON_ONCE(!trans->rx_mpdu_cmd);

    clear_bit(STATUS_FW_ERROR, &trans->status);
    return trans->ops->start_fw(trans, fw, run_in_rfkill);
}

#if IS_ENABLED(CPTCFG_IWLXVT)
enum iwl_xvt_dbg_flags {
    IWL_XVT_DBG_ADC_SAMP_TEST = BIT(0),
    IWL_XVT_DBG_ADC_SAMP_SYNC_RX = BIT(1),
};

static inline int iwl_trans_start_fw_dbg(struct iwl_trans* trans, const struct fw_img* fw,
                                         bool run_in_rfkill, uint32_t dbg_flags) {
    might_sleep();

    if (WARN_ON_ONCE(!trans->ops->start_fw_dbg && dbg_flags)) { return -ENOTSUPP; }

    clear_bit(STATUS_FW_ERROR, &trans->status);
    if (trans->ops->start_fw_dbg) {
        return trans->ops->start_fw_dbg(trans, fw, run_in_rfkill, dbg_flags);
    }

    return trans->ops->start_fw(trans, fw, run_in_rfkill);
}
#endif

static inline void _iwl_trans_stop_device(struct iwl_trans* trans, bool low_power) {
    might_sleep();

    trans->ops->stop_device(trans, low_power);

    trans->state = IWL_TRANS_NO_FW;
}

static inline void iwl_trans_stop_device(struct iwl_trans* trans) {
    _iwl_trans_stop_device(trans, true);
}

static inline void iwl_trans_d3_suspend(struct iwl_trans* trans, bool test, bool reset) {
    might_sleep();
    if (trans->ops->d3_suspend) { trans->ops->d3_suspend(trans, test, reset); }
}

static inline int iwl_trans_d3_resume(struct iwl_trans* trans, enum iwl_d3_status* status,
                                      bool test, bool reset) {
    might_sleep();
    if (!trans->ops->d3_resume) { return 0; }

    return trans->ops->d3_resume(trans, status, test, reset);
}

static inline int iwl_trans_suspend(struct iwl_trans* trans) {
    if (!trans->ops->suspend) { return 0; }

    return trans->ops->suspend(trans);
}

static inline void iwl_trans_resume(struct iwl_trans* trans) {
    if (trans->ops->resume) { trans->ops->resume(trans); }
}

static inline struct iwl_trans_dump_data* iwl_trans_dump_data(struct iwl_trans* trans,
                                                              uint32_t dump_mask) {
    if (!trans->ops->dump_data) { return NULL; }
    return trans->ops->dump_data(trans, dump_mask);
}

#if 0  // NEEDS_PORTING
static inline struct iwl_device_cmd*
iwl_trans_alloc_tx_cmd(struct iwl_trans* trans) {
    return kmem_cache_alloc(trans->dev_cmd_pool, GFP_ATOMIC);
}

int iwl_trans_send_cmd(struct iwl_trans* trans, struct iwl_host_cmd* cmd);

static inline void iwl_trans_free_tx_cmd(struct iwl_trans* trans,
        struct iwl_device_cmd* dev_cmd) {
    kmem_cache_free(trans->dev_cmd_pool, dev_cmd);
}

static inline int iwl_trans_tx(struct iwl_trans* trans, struct sk_buff* skb,
                               struct iwl_device_cmd* dev_cmd, int queue) {
    if (unlikely(test_bit(STATUS_FW_ERROR, &trans->status))) {
        return -EIO;
    }

    if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
        IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
        return -EIO;
    }

    return trans->ops->tx(trans, skb, dev_cmd, queue);
}

static inline void iwl_trans_reclaim(struct iwl_trans* trans, int queue,
                                     int ssn, struct sk_buff_head* skbs) {
    if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
        IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
        return;
    }

    trans->ops->reclaim(trans, queue, ssn, skbs);
}

static inline void iwl_trans_txq_disable(struct iwl_trans* trans, int queue,
        bool configure_scd) {
    trans->ops->txq_disable(trans, queue, configure_scd);
}

static inline bool
iwl_trans_txq_enable_cfg(struct iwl_trans* trans, int queue, uint16_t ssn,
                         const struct iwl_trans_txq_scd_cfg* cfg,
                         unsigned int queue_wdg_timeout) {
    might_sleep();

    if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
        IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
        return false;
    }

    return trans->ops->txq_enable(trans, queue, ssn,
                                  cfg, queue_wdg_timeout);
}

static inline int
iwl_trans_get_rxq_dma_data(struct iwl_trans* trans, int queue,
                           struct iwl_trans_rxq_dma_data* data) {
    if (WARN_ON_ONCE(!trans->ops->rxq_dma_data)) {
        return -ENOTSUPP;
    }

    return trans->ops->rxq_dma_data(trans, queue, data);
}

static inline void
iwl_trans_txq_free(struct iwl_trans* trans, int queue) {
    if (WARN_ON_ONCE(!trans->ops->txq_free)) {
        return;
    }

    trans->ops->txq_free(trans, queue);
}

static inline int
iwl_trans_txq_alloc(struct iwl_trans* trans,
                    __le16 flags, uint8_t sta_id, uint8_t tid,
                    int cmd_id, int size,
                    unsigned int wdg_timeout) {
    might_sleep();

    if (WARN_ON_ONCE(!trans->ops->txq_alloc)) {
        return -ENOTSUPP;
    }

    if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
        IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
        return -EIO;
    }

    return trans->ops->txq_alloc(trans, flags, sta_id, tid,
                                 cmd_id, size, wdg_timeout);
}

static inline void iwl_trans_txq_set_shared_mode(struct iwl_trans* trans,
        int queue, bool shared_mode) {
    if (trans->ops->txq_set_shared_mode) {
        trans->ops->txq_set_shared_mode(trans, queue, shared_mode);
    }
}

static inline void iwl_trans_txq_enable(struct iwl_trans* trans, int queue,
                                        int fifo, int sta_id, int tid,
                                        int frame_limit, uint16_t ssn,
                                        unsigned int queue_wdg_timeout) {
    struct iwl_trans_txq_scd_cfg cfg = {
        .fifo = fifo,
        .sta_id = sta_id,
        .tid = tid,
        .frame_limit = frame_limit,
        .aggregate = sta_id >= 0,
    };

    iwl_trans_txq_enable_cfg(trans, queue, ssn, &cfg, queue_wdg_timeout);
}

static inline
void iwl_trans_ac_txq_enable(struct iwl_trans* trans, int queue, int fifo,
                             unsigned int queue_wdg_timeout) {
    struct iwl_trans_txq_scd_cfg cfg = {
        .fifo = fifo,
        .sta_id = -1,
        .tid = IWL_MAX_TID_COUNT,
        .frame_limit = IWL_FRAME_LIMIT,
        .aggregate = false,
    };

    iwl_trans_txq_enable_cfg(trans, queue, 0, &cfg, queue_wdg_timeout);
}

static inline void iwl_trans_freeze_txq_timer(struct iwl_trans* trans,
        unsigned long txqs,
        bool freeze) {
    if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
        IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
        return;
    }

    if (trans->ops->freeze_txq_timer) {
        trans->ops->freeze_txq_timer(trans, txqs, freeze);
    }
}
#endif  // NEEDS_PORTING

static inline void iwl_trans_block_txq_ptrs(struct iwl_trans* trans,
        bool block) {
    if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
        IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
        return;
    }

    if (trans->ops->block_txq_ptrs) {
        trans->ops->block_txq_ptrs(trans, block);
    }
}

#if 0   // NEEDS_PORTING
static inline int iwl_trans_wait_tx_queues_empty(struct iwl_trans* trans,
        uint32_t txqs) {
    if (WARN_ON_ONCE(!trans->ops->wait_tx_queues_empty)) {
        return -ENOTSUPP;
    }

    if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
        IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
        return -EIO;
    }

    return trans->ops->wait_tx_queues_empty(trans, txqs);
}

static inline int iwl_trans_wait_txq_empty(struct iwl_trans* trans, int queue) {
    if (WARN_ON_ONCE(!trans->ops->wait_txq_empty)) {
        return -ENOTSUPP;
    }

    if (WARN_ON_ONCE(trans->state != IWL_TRANS_FW_ALIVE)) {
        IWL_ERR(trans, "%s bad state = %d\n", __func__, trans->state);
        return -EIO;
    }

    return trans->ops->wait_txq_empty(trans, queue);
}

#if IS_ENABLED(CPTCFG_IWLXVT)
static inline int iwl_trans_test_mode_cmd(struct iwl_trans* trans, bool enable) {
    if (trans->ops->test_mode_cmd) {
        return trans->ops->test_mode_cmd(trans, enable);
    }
    return -ENOTSUPP;
}
#endif
#endif   // NEEDS_PORTING

static inline void iwl_trans_write8(struct iwl_trans* trans, uint32_t ofs, uint8_t val) {
    trans->ops->write8(trans, ofs, val);
}

static inline void iwl_trans_write32(struct iwl_trans* trans, uint32_t ofs, uint32_t val) {
    trans->ops->write32(trans, ofs, val);
}

static inline uint32_t iwl_trans_read32(struct iwl_trans* trans, uint32_t ofs) {
    return trans->ops->read32(trans, ofs);
}

static inline uint32_t iwl_trans_read_prph(struct iwl_trans* trans, uint32_t ofs) {
    return trans->ops->read_prph(trans, ofs);
}

static inline void iwl_trans_write_prph(struct iwl_trans* trans, uint32_t ofs,
                                        uint32_t val) {
    return trans->ops->write_prph(trans, ofs, val);
}

#if 0   // NEEDS_PORTING
static inline int iwl_trans_read_mem(struct iwl_trans* trans, uint32_t addr,
                                     void* buf, int dwords) {
    return trans->ops->read_mem(trans, addr, buf, dwords);
}

#define iwl_trans_read_mem_bytes(trans, addr, buf, bufsize)                            \
    do {                                                                               \
        if (__builtin_constant_p(bufsize)) BUILD_BUG_ON((bufsize) % sizeof(uint32_t)); \
        iwl_trans_read_mem(trans, addr, buf, (bufsize) / sizeof(uint32_t));            \
    } while (0)

static inline uint32_t iwl_trans_read_mem32(struct iwl_trans* trans, uint32_t addr) {
    uint32_t value;

    if (WARN_ON(iwl_trans_read_mem(trans, addr, &value, 1))) {
        return 0xa5a5a5a5;
    }

    return value;
}

static inline int iwl_trans_write_mem(struct iwl_trans* trans, uint32_t addr,
                                      const void* buf, int dwords) {
    return trans->ops->write_mem(trans, addr, buf, dwords);
}

static inline uint32_t iwl_trans_write_mem32(struct iwl_trans* trans, uint32_t addr,
        uint32_t val) {
    return iwl_trans_write_mem(trans, addr, &val, 1);
}

static inline void iwl_trans_set_pmi(struct iwl_trans* trans, bool state) {
    if (trans->ops->set_pmi) {
        trans->ops->set_pmi(trans, state);
    }
}

static inline void iwl_trans_sw_reset(struct iwl_trans* trans) {
    if (trans->ops->sw_reset) {
        trans->ops->sw_reset(trans);
    }
}
#endif   // NEEDS_PORTING

static inline void
iwl_trans_set_bits_mask(struct iwl_trans* trans, uint32_t reg, uint32_t mask, uint32_t value) {
    trans->ops->set_bits_mask(trans, reg, mask, value);
}

#define iwl_trans_grab_nic_access(trans, flags) \
    ((trans)->ops->grab_nic_access(trans, flags))

static inline void iwl_trans_release_nic_access(struct iwl_trans* trans, unsigned long* flags) {
    trans->ops->release_nic_access(trans, flags);
}

#if 0   // NEEDS_PORTING
static inline void iwl_trans_fw_error(struct iwl_trans* trans) {
    if (WARN_ON_ONCE(!trans->op_mode)) {
        return;
    }

    /* prevent double restarts due to the same erroneous FW */
    if (!test_and_set_bit(STATUS_FW_ERROR, &trans->status)) {
        iwl_op_mode_nic_error(trans->op_mode);
    }
}
#endif  // NEEDS_PORTING

static inline bool iwl_trans_fw_running(struct iwl_trans* trans) {
    return trans->state == IWL_TRANS_FW_ALIVE;
}

/*****************************************************
 * transport helper functions
 *****************************************************/
struct iwl_trans* iwl_trans_alloc(unsigned int priv_size, struct device* dev,
                                  const struct iwl_cfg* cfg, const struct iwl_trans_ops* ops);
void iwl_trans_free(struct iwl_trans* trans);
void iwl_trans_ref(struct iwl_trans* trans);
void iwl_trans_unref(struct iwl_trans* trans);

/*****************************************************
 * driver (transport) register/unregister functions
 ******************************************************/
/* PCI */
int __must_check iwl_pci_register_driver(void);
void iwl_pci_unregister_driver(void);

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_THIRD_PARTY_INTEL_IWLWIFI_IWL_TRANS_H_
