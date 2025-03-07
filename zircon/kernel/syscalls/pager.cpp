// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include "priv.h"

#include <fbl/ref_ptr.h>
#include <ktl/move.h>
#include <object/pager_dispatcher.h>
#include <object/vm_object_dispatcher.h>
#include <vm/vm_object_paged.h>

// zx_status_t zx_pager_create
zx_status_t sys_pager_create(uint32_t options, user_out_handle* out) {
    if (options) {
        return ZX_ERR_INVALID_ARGS;
    }

    KernelHandle<PagerDispatcher> handle;
    zx_rights_t rights;
    zx_status_t result = PagerDispatcher::Create(&handle, &rights);
    if (result != ZX_OK) {
        return result;
    }

    return out->make(ktl::move(handle), rights);
}

// zx_status_t zx_pager_create_vmo
zx_status_t sys_pager_create_vmo(zx_handle_t pager, uint32_t options, zx_handle_t port,
                                 uint64_t key, uint64_t size, user_out_handle* out) {
    auto up = ProcessDispatcher::GetCurrent();

    zx_status_t status = up->QueryBasicPolicy(ZX_POL_NEW_VMO);
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<PagerDispatcher> pager_dispatcher;
    status = up->GetDispatcher(pager, &pager_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<PortDispatcher> port_dispatcher;
    status = up->GetDispatcherWithRights(port, ZX_RIGHT_WRITE, &port_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<PageSource> src;
    status = pager_dispatcher->CreateSource(ktl::move(port_dispatcher), key, &src);
    if (status != ZX_OK) {
        return status;
    }

    uint32_t vmo_options = 0;
    if (options & ZX_VMO_NON_RESIZABLE) {
        options &= ~ZX_VMO_NON_RESIZABLE;
    } else {
        vmo_options |= VmObjectPaged::kResizable;
    }

    if (options) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<VmObject> vmo;
    status = VmObjectPaged::CreateExternal(ktl::move(src), vmo_options, size, &vmo);
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<Dispatcher> dispatcher;
    zx_rights_t rights;
    status = VmObjectDispatcher::Create(vmo, pager_dispatcher->get_koid(), &dispatcher, &rights);
    if (status != ZX_OK) {
        return status;
    }

    return out->make(ktl::move(dispatcher), rights);
}

// zx_status_t zx_pager_detach_vmo
zx_status_t sys_pager_detach_vmo(zx_handle_t pager, zx_handle_t vmo) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<PagerDispatcher> pager_dispatcher;
    // TODO: Consider rights on the pager dispatcher.
    zx_status_t status = up->GetDispatcher(pager, &pager_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<VmObjectDispatcher> vmo_dispatcher;
    status = up->GetDispatcher(vmo, &vmo_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    if (vmo_dispatcher->pager_koid() != pager_dispatcher->get_koid()) {
        return ZX_ERR_INVALID_ARGS;
    }

    vmo_dispatcher->vmo()->DetachSource();
    return ZX_OK;
}

// zx_status_t zx_pager_supply_pages
zx_status_t sys_pager_supply_pages(zx_handle_t pager, zx_handle_t pager_vmo,
                                   uint64_t offset, uint64_t size,
                                   zx_handle_t aux_vmo_handle, uint64_t aux_offset) {
    auto up = ProcessDispatcher::GetCurrent();
    fbl::RefPtr<PagerDispatcher> pager_dispatcher;
    zx_status_t status = up->GetDispatcher(pager, &pager_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    fbl::RefPtr<VmObjectDispatcher> pager_vmo_dispatcher;
    status = up->GetDispatcher(pager_vmo, &pager_vmo_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    if (pager_vmo_dispatcher->pager_koid() != pager_dispatcher->get_koid()) {
        return ZX_ERR_INVALID_ARGS;
    }

    fbl::RefPtr<VmObjectDispatcher> aux_vmo_dispatcher;
    status = up->GetDispatcherWithRights(aux_vmo_handle,
                                         ZX_RIGHT_READ | ZX_RIGHT_WRITE, &aux_vmo_dispatcher);
    if (status != ZX_OK) {
        return status;
    }

    if (!IS_PAGE_ALIGNED(offset) || !IS_PAGE_ALIGNED(size) || !IS_PAGE_ALIGNED(aux_offset)) {
        return ZX_ERR_INVALID_ARGS;
    }

    VmPageSpliceList pages;
    status = aux_vmo_dispatcher->vmo()->TakePages(aux_offset, size, &pages);
    if (status != ZX_OK) {
        return status;
    }

    return pager_vmo_dispatcher->vmo()->SupplyPages(offset, size, &pages);
}
