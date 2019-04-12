// Copyright 2019 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include "../ref_counted.h"
#include "../upstream_node.h"
#include "fake_allocator.h"

namespace pci {

class FakeUpstreamNode : public UpstreamNode {
public:
    FakeUpstreamNode(Type type, uint32_t mbus_id)
        : UpstreamNode(type, mbus_id) {}

    PciAllocator& pf_mmio_regions() { return fake_alloc_; }
    PciAllocator& mmio_regions() { return fake_alloc_; }
    PciAllocator& pio_regions() { return fake_alloc_; }

    void UnplugDownstream() final {
        UpstreamNode::UnplugDownstream();
    }

    void DisableDownstream() final {
        UpstreamNode::DisableDownstream();
    }

    PCI_IMPLEMENT_REFCOUNTED;

private:
    FakeAllocator fake_alloc_;
};

} // namespace pci
