// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <object/vm_object_dispatcher.h>

#include <vm/vm_aspace.h>
#include <vm/vm_object.h>

#include <zircon/rights.h>

#include <fbl/alloc_checker.h>
#include <lib/counters.h>

#include <assert.h>
#include <err.h>
#include <inttypes.h>
#include <trace.h>

#define LOCAL_TRACE 0

KCOUNTER(dispatcher_vmo_create_count, "dispatcher.vmo.create")
KCOUNTER(dispatcher_vmo_destroy_count, "dispatcher.vmo.destroy")

zx_status_t VmObjectDispatcher::Create(fbl::RefPtr<VmObject> vmo,
                                       zx_koid_t pager_koid,
                                       fbl::RefPtr<Dispatcher>* dispatcher,
                                       zx_rights_t* rights) {
    fbl::AllocChecker ac;
    auto disp = new (&ac) VmObjectDispatcher(ktl::move(vmo), pager_koid);
    if (!ac.check())
        return ZX_ERR_NO_MEMORY;

    disp->vmo()->set_user_id(disp->get_koid());
    *rights = default_rights();
    *dispatcher = fbl::AdoptRef<Dispatcher>(disp);
    return ZX_OK;
}

VmObjectDispatcher::VmObjectDispatcher(fbl::RefPtr<VmObject> vmo, zx_koid_t pager_koid)
    : SoloDispatcher(ZX_VMO_ZERO_CHILDREN), vmo_(vmo), pager_koid_(pager_koid) {
    kcounter_add(dispatcher_vmo_create_count, 1);
    vmo_->SetChildObserver(this);
}

VmObjectDispatcher::~VmObjectDispatcher() {
    kcounter_add(dispatcher_vmo_destroy_count, 1);
    // Intentionally leave vmo_->user_id() set to our koid even though we're
    // dying and the koid will no longer map to a Dispatcher. koids are never
    // recycled, and it could be a useful breadcrumb.
}


void VmObjectDispatcher::OnZeroChild() {
    UpdateState(0, ZX_VMO_ZERO_CHILDREN);
}

void VmObjectDispatcher::OnOneChild() {
    UpdateState(ZX_VMO_ZERO_CHILDREN, 0);
}

void VmObjectDispatcher::get_name(char out_name[ZX_MAX_NAME_LEN]) const {
    canary_.Assert();
    vmo_->get_name(out_name, ZX_MAX_NAME_LEN);
}

zx_status_t VmObjectDispatcher::set_name(const char* name, size_t len) {
    canary_.Assert();
    return vmo_->set_name(name, len);
}

void VmObjectDispatcher::on_zero_handles() {
    // Clear when handle count reaches zero rather in the destructor because we're retaining a
    // VmObject that might call back into |this| via VmObjectChildObserver when it's destroyed.
    vmo_->SetChildObserver(nullptr);
}

zx_status_t VmObjectDispatcher::Read(user_out_ptr<void> user_data,
                                     size_t length,
                                     uint64_t offset) {
    canary_.Assert();

    return vmo_->ReadUser(user_data, offset, length);
}

zx_status_t VmObjectDispatcher::Write(user_in_ptr<const void> user_data,
                                      size_t length,
                                      uint64_t offset) {
    canary_.Assert();

    return vmo_->WriteUser(user_data, offset, length);
}

zx_status_t VmObjectDispatcher::SetSize(uint64_t size) {
    canary_.Assert();

    return vmo_->Resize(size);
}

zx_status_t VmObjectDispatcher::GetSize(uint64_t* size) {
    canary_.Assert();

    *size = vmo_->size();

    return ZX_OK;
}

zx_info_vmo_t VmoToInfoEntry(const VmObject* vmo,
                             bool is_handle, zx_rights_t handle_rights) {
    zx_info_vmo_t entry = {};
    entry.koid = vmo->user_id();
    vmo->get_name(entry.name, sizeof(entry.name));
    entry.size_bytes = vmo->size();
    entry.create_options = vmo->create_options();
    entry.parent_koid = vmo->parent_user_id();
    entry.num_children = vmo->num_children();
    entry.num_mappings = vmo->num_mappings();
    entry.share_count = vmo->share_count();
    entry.flags =
        (vmo->is_paged() ? ZX_INFO_VMO_TYPE_PAGED : ZX_INFO_VMO_TYPE_PHYSICAL) |
        (vmo->is_pager_backed() ? ZX_INFO_VMO_PAGER_BACKED : 0);
    entry.committed_bytes = vmo->AllocatedPages() * PAGE_SIZE;
    entry.cache_policy = vmo->GetMappingCachePolicy();
    if (is_handle) {
        entry.flags |= ZX_INFO_VMO_VIA_HANDLE;
        entry.handle_rights = handle_rights;
    } else {
        entry.flags |= ZX_INFO_VMO_VIA_MAPPING;
    }
    switch (vmo->child_type()) {
        case VmObject::ChildType::kCowClone:
            entry.flags |= ZX_INFO_VMO_IS_COW_CLONE;
            break;
        case VmObject::ChildType::kNotChild:
            break;
    }
    return entry;
}

zx_info_vmo_t VmObjectDispatcher::GetVmoInfo(void)
{
    return VmoToInfoEntry(vmo().get(), true, 0);
}

zx_status_t VmObjectDispatcher::RangeOp(uint32_t op, uint64_t offset, uint64_t size,
                                        user_inout_ptr<void> buffer, size_t buffer_size,
                                        zx_rights_t rights) {
    canary_.Assert();

    LTRACEF("op %u offset %#" PRIx64 " size %#" PRIx64
            " buffer %p buffer_size %zu rights %#x\n",
            op, offset, size, buffer.get(), buffer_size, rights);

    switch (op) {
        case ZX_VMO_OP_COMMIT: {
            if ((rights & ZX_RIGHT_WRITE) == 0) {
                return ZX_ERR_ACCESS_DENIED;
            }
            // TODO: handle partial commits
            auto status = vmo_->CommitRange(offset, size);
            return status;
        }
        case ZX_VMO_OP_DECOMMIT: {
            if ((rights & ZX_RIGHT_WRITE) == 0) {
                return ZX_ERR_ACCESS_DENIED;
            }
            // TODO: handle partial decommits
            auto status = vmo_->DecommitRange(offset, size);
            return status;
        }
        case ZX_VMO_OP_LOCK:
        case ZX_VMO_OP_UNLOCK:
            // TODO: handle or remove
            return ZX_ERR_NOT_SUPPORTED;

        case ZX_VMO_OP_CACHE_SYNC:
            if ((rights & ZX_RIGHT_READ) == 0) {
                return ZX_ERR_ACCESS_DENIED;
            }
            return vmo_->SyncCache(offset, size);
        case ZX_VMO_OP_CACHE_INVALIDATE:
            // A straight invalidate op requires the write right since
            // it may drop dirty cache lines, thus modifying the contents
            // of the VMO.
            if ((rights & ZX_RIGHT_WRITE) == 0) {
                return ZX_ERR_ACCESS_DENIED;
            }
            return vmo_->InvalidateCache(offset, size);
        case ZX_VMO_OP_CACHE_CLEAN:
            if ((rights & ZX_RIGHT_READ) == 0) {
                return ZX_ERR_ACCESS_DENIED;
            }
            return vmo_->CleanCache(offset, size);
        case ZX_VMO_OP_CACHE_CLEAN_INVALIDATE:
            if ((rights & ZX_RIGHT_READ) == 0) {
                return ZX_ERR_ACCESS_DENIED;
            }
            return vmo_->CleanInvalidateCache(offset, size);
        default:
            return ZX_ERR_INVALID_ARGS;
    }
}

zx_status_t VmObjectDispatcher::SetMappingCachePolicy(uint32_t cache_policy) {
    return vmo_->SetMappingCachePolicy(cache_policy);
}

zx_status_t VmObjectDispatcher::CreateChild(uint32_t options, uint64_t offset, uint64_t size,
        bool copy_name, fbl::RefPtr<VmObject>* child_vmo) {
    canary_.Assert();

    LTRACEF("options 0x%x offset %#" PRIx64 " size %#" PRIx64 "\n",
            options, offset, size);

    bool resizable = true;
    if (options & ZX_VMO_CHILD_COPY_ON_WRITE) {
        options &= ~ZX_VMO_CHILD_COPY_ON_WRITE;
    } else {
        return ZX_ERR_INVALID_ARGS;
    }

    if (options & ZX_VMO_CHILD_NON_RESIZEABLE) {
        resizable = false;
        options &= ~ZX_VMO_CHILD_NON_RESIZEABLE;
    }

    if (options)
        return ZX_ERR_INVALID_ARGS;

    return vmo_->CreateCowClone(resizable, offset, size, copy_name, child_vmo);
}
