// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

// Standard Includes
#include <assert.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <unistd.h>

// DDK includes
#include <ddk/binding.h>
#include <ddk/debug.h>
#include <ddk/device.h>
#include <ddk/mmio-buffer.h>
#include <ddk/platform-defs.h>
#include <ddk/protocol/platform/device.h>
#include <ddk/protocol/usb/dci.h>
#include <ddk/protocol/usb/modeswitch.h>
#include <ddk/protocol/usb.h>

// Zircon USB includes
#include <zircon/hw/usb.h>

#include <zircon/listnode.h>
#include <zircon/process.h>

#include "usb_dwc_regs.h"

#define MMIO_INDEX  0
#define IRQ_INDEX   0

//#define SINGLE_EP_IN_QUEUE 1

typedef enum dwc_ep0_state {
    EP0_STATE_DISCONNECTED,
    EP0_STATE_IDLE,
    EP0_STATE_DATA_OUT,
    EP0_STATE_DATA_IN,
    EP0_STATE_STATUS,
    EP0_STATE_STALL,
} dwc_ep0_state_t;

// Internal context for USB requests
typedef struct {
     // callback to the upper layer
     usb_request_complete_t complete_cb;
     // for queueing requests internally
     list_node_t node;
} dwc_usb_req_internal_t;

#define USB_REQ_TO_INTERNAL(req)                                                                   \
    ((dwc_usb_req_internal_t*)((uintptr_t)(req) + sizeof(usb_request_t)))
#define INTERNAL_TO_USB_REQ(ctx) ((usb_request_t *)((uintptr_t)(ctx) - sizeof(usb_request_t)))

typedef struct {
    list_node_t queued_reqs;    // requests waiting to be processed
    usb_request_t* current_req; // request currently being processed
    uint8_t* req_buffer;
    uint32_t req_offset;
    uint32_t req_length;    

    // Used for synchronizing endpoint state
    // and ep specific hardware registers
    // This should be acquired before dwc_usb_t.lock
    // if acquiring both locks.
    mtx_t lock;

    uint16_t max_packet_size;
    uint8_t ep_num;
    bool enabled;
    uint8_t type;           // control, bulk, interrupt or isochronous
    uint8_t interval;
    bool send_zlp;
    bool stalled;
} dwc_endpoint_t;

typedef struct {
    zx_device_t* zxdev;
    zx_handle_t irq_handle;
    zx_handle_t bti_handle;
    thrd_t irq_thread;
    zx_device_t* parent;

    pdev_protocol_t pdev;
//    usb_mode_switch_protocol_t ums;
//    usb_mode_t usb_mode;

    mmio_buffer_t mmio;
    dwc_regs_t* regs;

    // device stuff
    dwc_endpoint_t eps[DWC_MAX_EPS];

    usb_dci_interface_protocol_t dci_intf;

#if SINGLE_EP_IN_QUEUE
    list_node_t queued_in_reqs;
    usb_request_t* current_in_req;
#endif

    // Used for synchronizing global state
    // and non ep specific hardware registers.
    // dwc_endpoint_t.lock should be acquired first
    // if acquiring both locks.
    mtx_t lock;

    bool configured;

    usb_setup_t cur_setup;    
    dwc_ep0_state_t ep0_state;

    uint8_t ep0_buffer[UINT16_MAX];

    bool got_setup;
} dwc_usb_t;

// dwc-endpoints.c
bool dwc_ep_write_packet(dwc_usb_t* dwc, int ep_num);
void dwc_ep_start_transfer(dwc_usb_t* dwc, unsigned ep_num, bool is_in, size_t length);
void dwc_complete_ep(dwc_usb_t* dwc, uint32_t ep_num);
void dwc_reset_configuration(dwc_usb_t* dwc);
void dwc_start_eps(dwc_usb_t* dwc);
void dwc_ep_queue(dwc_usb_t* dwc, unsigned ep_num, usb_request_t* req);
zx_status_t dwc_ep_config(dwc_usb_t* dwc, const usb_endpoint_descriptor_t* ep_desc,
                          const usb_ss_ep_comp_descriptor_t* ss_comp_desc);
zx_status_t dwc_ep_disable(dwc_usb_t* dwc, uint8_t ep_addr);
zx_status_t dwc_ep_set_stall(dwc_usb_t* dwc, unsigned ep_num, bool stall);

// dwc-interrupts.c
zx_status_t dwc_irq_start(dwc_usb_t* dwc);
void dwc_irq_stop(dwc_usb_t* dwc);
void dwc_flush_fifo(dwc_usb_t* dwc, const int num);
