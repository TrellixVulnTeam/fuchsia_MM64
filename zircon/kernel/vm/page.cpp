// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2014 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <vm/page.h>

#include <err.h>
#include <inttypes.h>
#include <kernel/percpu.h>
#include <lib/console.h>
#include <stdio.h>
#include <string.h>
#include <trace.h>
#include <vm/physmap.h>
#include <vm/pmm.h>
#include <vm/vm.h>

#define LOCAL_TRACE 0

const char* page_state_to_string(unsigned int state) {
    switch (state) {
    case VM_PAGE_STATE_FREE:
        return "free";
    case VM_PAGE_STATE_ALLOC:
        return "alloc";
    case VM_PAGE_STATE_WIRED:
        return "wired";
    case VM_PAGE_STATE_HEAP:
        return "heap";
    case VM_PAGE_STATE_OBJECT:
        return "object";
    case VM_PAGE_STATE_MMU:
        return "mmu";
    case VM_PAGE_STATE_IPC:
        return "ipc";
    default:
        return "unknown";
    }
}

void vm_page::dump() const {
    printf("page %p: address %#" PRIxPTR " state %s flags %#x\n", this, paddr(),
           page_state_to_string(state_priv), flags);
}

void vm_page::set_state(vm_page_state new_state) {
    constexpr uint32_t kMask = (1 << VM_PAGE_STATE_BITS) - 1;
    DEBUG_ASSERT_MSG(new_state == (new_state & kMask), "invalid state %u\n", new_state);

    const vm_page_state old_state = vm_page_state(state_priv);
    state_priv = (new_state & kMask);

    Percpus::WithCurrentPreemptDisable(
        [&old_state, &new_state](percpu* p) {
            p->vm_page_counts.by_state[old_state] -= 1;
            p->vm_page_counts.by_state[new_state] += 1;
        });
}

uint64_t vm_page::get_count(vm_page_state state) {
    int64_t result = 0;
    Percpus::ForEachPreemptDisable([&state, &result](percpu* p) {
        result += p->vm_page_counts.by_state[state];
    });
    return result >= 0 ? result : 0;
}

void vm_page::add_to_initial_count(vm_page_state state, uint64_t n) {
    Percpus::WithCurrentPreemptDisable(
        [&state, &n](percpu* p) {
            p->vm_page_counts.by_state[state] += n;
        });
}

static int cmd_vm_page(int argc, const cmd_args* argv, uint32_t flags) {
    if (argc < 2) {
    notenoughargs:
        printf("not enough arguments\n");
    usage:
        printf("usage:\n");
        printf("%s dump <address>\n", argv[0].str);
        printf("%s hexdump <address>\n", argv[0].str);
        return ZX_ERR_INTERNAL;
    }

    if (!strcmp(argv[1].str, "dump")) {
        if (argc < 2) {
            goto notenoughargs;
        }

        vm_page* page = reinterpret_cast<vm_page*>(argv[2].u);

        page->dump();
    } else if (!strcmp(argv[1].str, "hexdump")) {
        if (argc < 2) {
            goto notenoughargs;
        }

        vm_page* page = reinterpret_cast<vm_page*>(argv[2].u);

        paddr_t pa = page->paddr();
        void* ptr = paddr_to_physmap(pa);
        if (!ptr) {
            printf("bad page or page not mapped in kernel space\n");
            return -1;
        }
        hexdump(ptr, PAGE_SIZE);
    } else {
        printf("unknown command\n");
        goto usage;
    }

    return ZX_OK;
}

STATIC_COMMAND_START
#if LK_DEBUGLEVEL > 0
STATIC_COMMAND("vm_page", "vm_page debug commands", &cmd_vm_page)
#endif
STATIC_COMMAND_END(vm_page)
