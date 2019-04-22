// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dwc2.h"

#include <ddk/binding.h>
#include <ddk/protocol/platform-device-lib.h>
#include <usb/usb-request.h>

static zx_status_t usb_dwc_softreset_core(dwc_usb_t* dwc) {
    dwc_regs_t* regs = dwc->regs;

/* do we need this? */
    while (regs->grstctl.ahbidle == 0) {    
        usleep(1000);
    }

    dwc_grstctl_t grstctl = {0};
    grstctl.csftrst = 1;
    regs->grstctl = grstctl;

    for (int i = 0; i < 1000; i++) {
        if (regs->grstctl.csftrst == 0) {
            usleep(10 * 1000);
            return ZX_OK;
        }
        usleep(1000);
    }
    return ZX_ERR_TIMED_OUT;
}

static zx_status_t usb_dwc_setupcontroller(dwc_usb_t* dwc) {
    dwc_regs_t* regs = dwc->regs;

    regs->gusbcfg.force_dev_mode = 1;
	regs->gahbcfg.dmaenable = 0;

#if 1 // astro
	regs->gusbcfg.usbtrdtim = 9;
#else
	regs->gusbcfg.usbtrdtim = 5;
#endif

    regs->dctl.sftdiscon = 1;
    regs->dctl.sftdiscon = 0;

    // reset phy clock
    regs->pcgcctl.val = 0;

    regs->grxfsiz = 256;    //???

	regs->gnptxfsiz.depth = 256;
	regs->gnptxfsiz.startaddr = 256;

	dwc_flush_fifo(dwc, 0x10);

	regs->grstctl.intknqflsh = 1;

	/* Clear all pending Device Interrupts */
	regs->diepmsk.val = 0;
	regs->doepmsk.val = 0;
	regs->daint = 0xffffffff;
	regs->daintmsk = 0;

    for (int i = 0; i < MAX_EPS_CHANNELS; i++) {
        regs->depin[i].diepctl.val = 0;
        regs->depout[i].doepctl.val = 0;
        regs->depin[i].dieptsiz.val = 0;
        regs->depout[i].doeptsiz.val = 0;
    }

    dwc_interrupts_t gintmsk = {0};

    gintmsk.rxstsqlvl = 1;
    gintmsk.usbreset = 1;
    gintmsk.enumdone = 1;
    gintmsk.inepintr = 1;
    gintmsk.outepintr = 1;
//    gintmsk.sof_intr = 1;
    gintmsk.usbsuspend = 1;


    gintmsk.ginnakeff = 1;
    gintmsk.goutnakeff = 1;


/*
	gintmsk.modemismatch = 1;
	gintmsk.otgintr = 1;
	gintmsk.conidstschng = 1;
	gintmsk.wkupintr = 1;
	gintmsk.disconnect = 0;
	gintmsk.sessreqintr = 1;
*/

printf("ghwcfg1 %08x ghwcfg2 %08x ghwcfg3 %08x\n", regs->ghwcfg1, regs->ghwcfg2, regs->ghwcfg3);

	regs->gotgint = 0xFFFFFFF;
	regs->gintsts.val = 0xFFFFFFF;

zxlogf(LINFO, "enabling interrupts %08x\n", gintmsk.val);

    regs->gintmsk = gintmsk;

    regs->gahbcfg.glblintrmsk = 1;

    return ZX_OK;
}

static void dwc_request_queue(void* ctx, usb_request_t* req, const usb_request_complete_t* cb) {
    dwc_usb_t* dwc = ctx;

    zxlogf(INFO, "XXXXXXX dwc_request_queue ep: 0x%02x length %zu\n", req->header.ep_address, req->header.length);
    unsigned ep_num = DWC_ADDR_TO_INDEX(req->header.ep_address);
    if (ep_num == 0 || ep_num >= countof(dwc->eps)) {
        zxlogf(ERROR, "dwc_request_queue: bad ep address 0x%02X\n", req->header.ep_address);
        usb_request_complete(req, ZX_ERR_INVALID_ARGS, 0, cb);
        return;
    }

    dwc_ep_queue(dwc, ep_num, req);
}

static zx_status_t dwc_set_interface(void* ctx, const usb_dci_interface_protocol_t* dci_intf) {
    dwc_usb_t* dwc = ctx;
    memcpy(&dwc->dci_intf, dci_intf, sizeof(dwc->dci_intf));
    return ZX_OK;
}

static zx_status_t dwc_config_ep(void* ctx, const usb_endpoint_descriptor_t* ep_desc,
                                 const usb_ss_ep_comp_descriptor_t* ss_comp_desc) {
    dwc_usb_t* dwc = ctx;
    return dwc_ep_config(dwc, ep_desc, ss_comp_desc);
}

static zx_status_t dwc_disable_ep(void* ctx, uint8_t ep_addr) {
    dwc_usb_t* dwc = ctx;
    return dwc_ep_disable(dwc, ep_addr);
}

static zx_status_t dwc_set_stall(void* ctx, uint8_t ep_address) {
    dwc_usb_t* dwc = ctx;
    return dwc_ep_set_stall(dwc, DWC_ADDR_TO_INDEX(ep_address), true);
}

static zx_status_t dwc_clear_stall(void* ctx, uint8_t ep_address) {
    dwc_usb_t* dwc = ctx;
    return dwc_ep_set_stall(dwc, DWC_ADDR_TO_INDEX(ep_address), false);
}

static size_t dwc_get_request_size(void* ctx) {
    return sizeof(usb_request_t) + sizeof(dwc_usb_req_internal_t);
}

static zx_status_t dwc_cancel_all(void* ctx, uint8_t ep) {
    return ZX_ERR_NOT_SUPPORTED;
}

static usb_dci_protocol_ops_t dwc_dci_protocol = {
    .request_queue = dwc_request_queue,
    .set_interface = dwc_set_interface,
    .config_ep = dwc_config_ep,
    .disable_ep = dwc_disable_ep,
    .ep_set_stall = dwc_set_stall,
    .ep_clear_stall = dwc_clear_stall,
    .get_request_size = dwc_get_request_size,
    .cancel_all = dwc_cancel_all
};

#if 0
static zx_status_t dwc_set_mode(void* ctx, usb_mode_t mode) {
    dwc_usb_t* dwc = ctx;
    zx_status_t status = ZX_OK;

    if (mode != USB_MODE_PERIPHERAL && mode != USB_MODE_NONE) {
        return ZX_ERR_NOT_SUPPORTED;
    }
    if (dwc->usb_mode == mode) {
        return ZX_OK;
    }

    // Shutdown if we are in device mode
    if (dwc->usb_mode == USB_MODE_PERIPHERAL) {
        dwc_irq_stop(dwc);
    }

/* may be unsupported
    status = usb_mode_switch_set_mode(&dwc->ums, mode);
    if (status != ZX_OK) {
        goto fail;
    }
*/

    if (mode == USB_MODE_PERIPHERAL) {
        status = dwc_irq_start(dwc);
        if (status != ZX_OK) {
            zxlogf(ERROR, "dwc3_set_mode: pdev_map_interrupt failed\n");
            goto fail;
        }
    }

    dwc->usb_mode = mode;
    return ZX_OK;

fail:
//    usb_mode_switch_set_mode(&dwc->ums, USB_MODE_NONE);
    dwc->usb_mode = USB_MODE_NONE;

    return status;
}

usb_mode_switch_protocol_ops_t dwc_ums_protocol = {
    .set_mode = dwc_set_mode,
};
#endif

/*
static zx_status_t dwc_get_protocol(void* ctx, uint32_t proto_id, void* out) {
    switch (proto_id) {
    case ZX_PROTOCOL_USB_DCI: {
        usb_dci_protocol_t* proto = out;
        proto->ops = &dwc_dci_protocol;
        proto->ctx = ctx;
        return ZX_OK;
    }
    case ZX_PROTOCOL_USB_MODE_SWITCH: {
        usb_mode_switch_protocol_t* proto = out;
        proto->ops = &dwc_ums_protocol;
        proto->ctx = ctx;
        return ZX_OK;
    }
    default:
        return ZX_ERR_NOT_SUPPORTED;
    }
}

*/

static void dwc_unbind(void* ctx) {
    zxlogf(ERROR, "dwc_usb: dwc_unbind not implemented\n");
}

static void dwc_release(void* ctx) {
    zxlogf(ERROR, "dwc_usb: dwc_release not implemented\n");
}

static zx_protocol_device_t dwc_device_proto = {
    .version = DEVICE_OPS_VERSION,
//    .get_protocol = dwc_get_protocol,
    .unbind = dwc_unbind,
    .release = dwc_release,
};

// Bind is the entry point for this driver.
static zx_status_t dwc_bind(void* ctx, zx_device_t* dev) {
    zxlogf(TRACE, "dwc_bind: dev = %p\n", dev);

    // Allocate a new device object for the bus.
    dwc_usb_t* dwc = calloc(1, sizeof(*dwc));
    if (!dwc) {
        zxlogf(ERROR, "dwc_bind: bind failed to allocate usb_dwc struct\n");
        return ZX_ERR_NO_MEMORY;
    }

#if SINGLE_EP_IN_QUEUE
    list_initialize(&dwc->queued_in_reqs);
#endif

    zx_status_t status = device_get_protocol(dev, ZX_PROTOCOL_PDEV, &dwc->pdev);
    if (status != ZX_OK) {
        free(dwc);
        return status;
    }
/*
    status = device_get_protocol(dev, ZX_PROTOCOL_USB_MODE_SWITCH, &dwc->ums);
    if (status != ZX_OK) {
        free(dwc);
        return status;
    }
*/

    // hack for astro USB tuning (also optional)
//    device_get_protocol(dev, ZX_PROTOCOL_ASTRO_USB, &dwc->astro_usb);

    for (unsigned i = 0; i < countof(dwc->eps); i++) {
        dwc_endpoint_t* ep = &dwc->eps[i];
        ep->ep_num = i;
        mtx_init(&ep->lock, mtx_plain);
        list_initialize(&ep->queued_reqs);
    }
    dwc->eps[0].req_buffer = dwc->ep0_buffer;

    status = pdev_map_mmio_buffer(&dwc->pdev, MMIO_INDEX, ZX_CACHE_POLICY_UNCACHED_DEVICE, &dwc->mmio);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: pdev_map_mmio_buffer failed\n");
        goto error_return;
    }
    dwc->regs = dwc->mmio.vaddr;

    status = pdev_get_bti(&dwc->pdev, 0, &dwc->bti_handle);
    if (status != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: bind failed to get bti handle.\n");
        goto error_return;
    }

    dwc->parent = dev;
//    dwc->usb_mode = USB_MODE_NONE;

//    if (dwc->astro_usb.ops) {
//        astro_usb_do_usb_tuning(&dwc->astro_usb, false, true);
//    }

    if ((status = usb_dwc_softreset_core(dwc)) != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: failed to reset core.\n");
        goto error_return;
    }

    if ((status = usb_dwc_setupcontroller(dwc)) != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: failed setup controller.\n");
        goto error_return;
    }

    if ((status = dwc_irq_start(dwc)) != ZX_OK) {
        zxlogf(ERROR, "usb_dwc: dwc_irq_start failed\n");
        goto error_return;
    }

   device_add_args_t args = {
        .version = DEVICE_ADD_ARGS_VERSION,
        .name = "dwc2",
        .ctx = dwc,
        .ops = &dwc_device_proto,
        .proto_id = ZX_PROTOCOL_USB_DCI,
        .proto_ops = &dwc_dci_protocol,
    };

    if ((status = device_add(dev, &args, &dwc->zxdev)) != ZX_OK) {
        free(dwc);
        return status;
    }

    zxlogf(TRACE, "usb_dwc: bind success!\n");
    return ZX_OK;

error_return:
    if (dwc) {
//        if (dwc->regs) {
//            zx_vmar_unmap(zx_vmar_root_self(), (uintptr_t)dwc->regs, mmio_size);
//        }
//        zx_handle_close(mmio_handle);
        zx_handle_close(dwc->irq_handle);
        zx_handle_close(dwc->bti_handle);
        free(dwc);
    }

    return status;
}

static zx_driver_ops_t dwc2_driver_ops = {
    .version = DRIVER_OPS_VERSION,
    .bind = dwc_bind,
};

// The formatter does not play nice with these macros.
// clang-format off
ZIRCON_DRIVER_BEGIN(dwc2, dwc2_driver_ops, "zircon", "0.1", 3)
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_VID, PDEV_VID_GENERIC),
    BI_ABORT_IF(NE, BIND_PLATFORM_DEV_PID, PDEV_PID_GENERIC),
    BI_MATCH_IF(EQ, BIND_PLATFORM_DEV_DID, PDEV_DID_USB_DWC2),
ZIRCON_DRIVER_END(dwc2)
// clang-format on
