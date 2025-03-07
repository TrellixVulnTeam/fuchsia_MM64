/*
 * Copyright (c) 2013 Broadcom Corporation
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

#include "proto.h"

#include "brcmu_wifi.h"
#include "bcdc.h"
#include "bus.h"
#include "core.h"
#include "debug.h"
#include "linuxisms.h"
#include "msgbuf.h"

zx_status_t brcmf_proto_attach(struct brcmf_pub* drvr) {
    struct brcmf_proto* proto;

    brcmf_dbg(TRACE, "Enter\n");

    proto = static_cast<decltype(proto)>(calloc(1, sizeof(*proto)));
    if (!proto) {
        goto fail;
    }

    drvr->proto = proto;

    if (drvr->bus_if->proto_type == BRCMF_PROTO_BCDC) {
        if (brcmf_proto_bcdc_attach(drvr)) {
            goto fail;
        }
    } else if (drvr->bus_if->proto_type == BRCMF_PROTO_MSGBUF) {
        if (brcmf_proto_msgbuf_attach(drvr)) {
            goto fail;
        }
    } else {
        brcmf_err("Unsupported proto type %d\n", drvr->bus_if->proto_type);
        goto fail;
    }
    if (!proto->tx_queue_data || (proto->hdrpull == NULL) || (proto->query_dcmd == NULL) ||
            (proto->set_dcmd == NULL) || (proto->configure_addr_mode == NULL) ||
            (proto->delete_peer == NULL) || (proto->add_tdls_peer == NULL)) {
        brcmf_err("Not all proto handlers have been installed\n");
        goto fail;
    }
    return ZX_OK;

fail:
    free(proto);
    drvr->proto = NULL;
    return ZX_ERR_NO_MEMORY;
}

void brcmf_proto_detach(struct brcmf_pub* drvr) {
    brcmf_dbg(TRACE, "Enter\n");

    if (drvr->proto) {
        if (drvr->bus_if->proto_type == BRCMF_PROTO_BCDC) {
            brcmf_proto_bcdc_detach(drvr);
        } else if (drvr->bus_if->proto_type == BRCMF_PROTO_MSGBUF) {
            brcmf_proto_msgbuf_detach(drvr);
        }
        free(drvr->proto);
        drvr->proto = NULL;
    }
}
