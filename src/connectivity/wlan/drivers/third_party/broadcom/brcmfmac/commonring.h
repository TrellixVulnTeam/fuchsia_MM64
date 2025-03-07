/* Copyright (c) 2014 Broadcom Corporation
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
#ifndef BRCMFMAC_COMMONRING_H
#define BRCMFMAC_COMMONRING_H

#include <stdatomic.h>
#include <zircon/types.h>

struct brcmf_commonring {
    uint16_t r_ptr;
    uint16_t w_ptr;
    uint16_t f_ptr;
    uint16_t depth;
    uint16_t item_len;

    void* buf_addr;

    zx_status_t (*cr_ring_bell)(void* ctx);
    zx_status_t (*cr_update_rptr)(void* ctx);
    zx_status_t (*cr_update_wptr)(void* ctx);
    zx_status_t (*cr_write_rptr)(void* ctx);
    zx_status_t (*cr_write_wptr)(void* ctx);

    void* cr_ctx;

    //spinlock_t lock;
    bool inited;
    bool was_full;

    atomic_int outstanding_tx;
};

void brcmf_commonring_register_cb(struct brcmf_commonring* commonring,
                                  zx_status_t (*cr_ring_bell)(void* ctx),
                                  zx_status_t (*cr_update_rptr)(void* ctx),
                                  zx_status_t (*cr_update_wptr)(void* ctx),
                                  zx_status_t (*cr_write_rptr)(void* ctx),
                                  zx_status_t (*cr_write_wptr)(void* ctx), void* ctx);
void brcmf_commonring_config(struct brcmf_commonring* commonring, uint16_t depth, uint16_t item_len,
                             void* buf_addr);
void brcmf_commonring_lock(struct brcmf_commonring* commonring);
void brcmf_commonring_unlock(struct brcmf_commonring* commonring);
bool brcmf_commonring_write_available(struct brcmf_commonring* commonring);
void* brcmf_commonring_reserve_for_write(struct brcmf_commonring* commonring);
void* brcmf_commonring_reserve_for_write_multiple(struct brcmf_commonring* commonring,
                                                  uint16_t n_items, uint16_t* alloced);
zx_status_t brcmf_commonring_write_complete(struct brcmf_commonring* commonring);
void brcmf_commonring_write_cancel(struct brcmf_commonring* commonring, uint16_t n_items);
void* brcmf_commonring_get_read_ptr(struct brcmf_commonring* commonring, uint16_t* n_items);
zx_status_t brcmf_commonring_read_complete(struct brcmf_commonring* commonring, uint16_t n_items);

#define brcmf_commonring_n_items(commonring) (commonring->depth)
#define brcmf_commonring_len_item(commonring) (commonring->item_len)

#endif /* BRCMFMAC_COMMONRING_H */
