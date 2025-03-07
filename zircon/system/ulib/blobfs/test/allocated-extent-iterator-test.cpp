// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/iterator/allocated-extent-iterator.h>
#include <blobfs/iterator/block-iterator.h>
#include <blobfs/iterator/node-populator.h>
#include <unittest/unittest.h>

#include "utils.h"

namespace blobfs {
namespace {

// Allocates a blob with the provided number of extents / nodes.
//
// Returns the allocator, the extents, and nodes used.
bool TestSetup(size_t allocated_blocks, size_t allocated_nodes, bool fragmented,
               MockSpaceManager* space_manager, fbl::unique_ptr<Allocator>* out_allocator,
               fbl::Vector<Extent>* out_extents, fbl::Vector<uint32_t>* out_nodes) {
    BEGIN_HELPER;

    // Block count is large enough to allow for both fragmentation and the
    // allocation of |allocated_blocks| extents.
    size_t block_count = 3 * allocated_blocks;
    ASSERT_TRUE(InitializeAllocator(block_count, allocated_nodes, space_manager, out_allocator));
    if (fragmented) {
        ASSERT_TRUE(ForceFragmentation(out_allocator->get(), block_count));
    }

    // Allocate the initial nodes and blocks.
    fbl::Vector<ReservedNode> nodes;
    fbl::Vector<ReservedExtent> extents;
    ASSERT_EQ(ZX_OK, (*out_allocator)->ReserveNodes(allocated_nodes, &nodes));
    ASSERT_EQ(ZX_OK, (*out_allocator)->ReserveBlocks(allocated_blocks, &extents));
    if (fragmented) {
        ASSERT_EQ(allocated_blocks, extents.size());
    }

    // Keep a copy of the nodes and blocks, since we are passing both to the
    // node populator, but want to verify them afterwards.
    CopyExtents(extents, out_extents);
    CopyNodes(nodes, out_nodes);

    // Actually populate the node with the provided extents and nodes.
    auto on_node = [&](const ReservedNode& node) {};
    auto on_extent = [&](ReservedExtent& extent) {
        return NodePopulator::IterationCommand::Continue;
    };
    NodePopulator populator(out_allocator->get(), std::move(extents), std::move(nodes));
    ASSERT_EQ(ZX_OK, populator.Walk(on_node, on_extent));

    END_HELPER;
}

// Iterate over the null blob.
bool NullTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    constexpr size_t kAllocatedExtents = 0;
    constexpr size_t kAllocatedNodes = 1;

    ASSERT_TRUE(TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/ true, &space_manager,
                          &allocator, &allocated_extents, &allocated_nodes));

    // After walking, observe that the inode is allocated.
    const uint32_t node_index = allocated_nodes[0];
    const Inode* inode = allocator->GetNode(node_index);
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_EQ(kAllocatedExtents, inode->extent_count);

    AllocatedExtentIterator iter(allocator.get(), node_index);
    ASSERT_TRUE(iter.Done());
    ASSERT_EQ(0, iter.BlockIndex());
    ASSERT_EQ(0, iter.ExtentIndex());

    END_TEST;
}

// Iterate over a blob with inline extents.
bool InlineNodeTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    constexpr size_t kAllocatedExtents = kInlineMaxExtents;
    constexpr size_t kAllocatedNodes = 1;

    ASSERT_TRUE(TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/ true, &space_manager,
                          &allocator, &allocated_extents, &allocated_nodes));

    // After walking, observe that the inode is allocated.
    const uint32_t node_index = allocated_nodes[0];
    const Inode* inode = allocator->GetNode(node_index);
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_EQ(kAllocatedExtents, inode->extent_count);

    AllocatedExtentIterator iter(allocator.get(), node_index);
    ASSERT_EQ(0, iter.BlockIndex());
    uint32_t blocks_seen = 0;

    for (size_t i = 0; i < allocated_extents.size(); i++) {
        ASSERT_FALSE(iter.Done());
        ASSERT_EQ(node_index, iter.NodeIndex());
        ASSERT_EQ(i, iter.ExtentIndex());
        ASSERT_EQ(blocks_seen, iter.BlockIndex());

        const Extent* extent;
        ASSERT_EQ(ZX_OK, iter.Next(&extent));
        ASSERT_TRUE(allocated_extents[i] == *extent);
        blocks_seen += extent->Length();
    }

    ASSERT_TRUE(iter.Done());
    ASSERT_EQ(allocated_extents.size(), iter.ExtentIndex());
    ASSERT_EQ(blocks_seen, iter.BlockIndex());

    END_TEST;
}

// Iterate over a blob with multiple nodes.
bool MultiNodeTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    constexpr size_t kAllocatedExtents = kInlineMaxExtents + kContainerMaxExtents + 1;
    constexpr size_t kAllocatedNodes = 3;

    ASSERT_TRUE(TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/ true, &space_manager,
                          &allocator, &allocated_extents, &allocated_nodes));

    // After walking, observe that the inode is allocated.
    const uint32_t node_index = allocated_nodes[0];
    const Inode* inode = allocator->GetNode(node_index);
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_EQ(kAllocatedExtents, inode->extent_count);

    AllocatedExtentIterator iter(allocator.get(), node_index);
    ASSERT_EQ(0, iter.ExtentIndex());
    ASSERT_EQ(0, iter.BlockIndex());
    uint32_t blocks_seen = 0;

    for (size_t i = 0; i < allocated_extents.size(); i++) {
        ASSERT_FALSE(iter.Done());
        if (i < kInlineMaxExtents) {
            ASSERT_EQ(allocated_nodes[0], iter.NodeIndex());
        } else if (i < kInlineMaxExtents + kContainerMaxExtents) {
            ASSERT_EQ(allocated_nodes[1], iter.NodeIndex());
        } else {
            ASSERT_EQ(allocated_nodes[2], iter.NodeIndex());
        }
        ASSERT_EQ(i, iter.ExtentIndex());
        ASSERT_EQ(blocks_seen, iter.BlockIndex());

        const Extent* extent;
        ASSERT_EQ(ZX_OK, iter.Next(&extent));
        ASSERT_TRUE(allocated_extents[i] == *extent);
        blocks_seen += extent->Length();
    }

    ASSERT_TRUE(iter.Done());
    ASSERT_EQ(allocated_extents.size(), iter.ExtentIndex());
    ASSERT_EQ(blocks_seen, iter.BlockIndex());

    END_TEST;
}

// Demonstrate that the allocated extent iterator won't let us access invalid
// nodes.
bool BadInodeNextNodeTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    constexpr size_t kAllocatedExtents = kInlineMaxExtents + kContainerMaxExtents + 1;
    constexpr size_t kAllocatedNodes = 4;

    ASSERT_TRUE(TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/ true, &space_manager,
                          &allocator, &allocated_extents, &allocated_nodes));

    // After walking, observe that the inode is allocated.
    const uint32_t node_index = allocated_nodes[0];
    Inode* inode = allocator->GetNode(node_index);
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_EQ(kAllocatedExtents, inode->extent_count);

    // Manually corrupt the next inode to point to itself.
    inode->header.next_node = node_index;

    // The iterator should reflect this corruption by returning an error during
    // traversal from the node to the container.
    {
        AllocatedExtentIterator iter(allocator.get(), node_index);
        ASSERT_TRUE(!iter.Done());
        const Extent* extent;
        for (size_t i = 0; i < kInlineMaxExtents - 1; i++) {
            ASSERT_EQ(ZX_OK, iter.Next(&extent));
        }
        ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, iter.Next(&extent));
    }

    // Manually corrupt the next inode to point to an unallocated (but otherwise
    // valid) node.
    inode->header.next_node = allocated_nodes[kAllocatedNodes - 1];

    // The iterator should reflect this corruption by returning an error during
    // traversal from the node to the container.
    {
        AllocatedExtentIterator iter(allocator.get(), node_index);
        ASSERT_TRUE(!iter.Done());
        const Extent* extent;
        for (size_t i = 0; i < kInlineMaxExtents - 1; i++) {
            ASSERT_EQ(ZX_OK, iter.Next(&extent));
        }
        ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, iter.Next(&extent));
    }

// TODO(smklein): Currently, this fails because Allocator::GetNode asserts on failure,
// rather than returning a status.
//
//    // Manually corrupt the next inode to point to a completely invalid node.
//    inode->header.next_node = 0xFFFFFFFF;
//
//    // The iterator should reflect this corruption by returning an error during
//    // traversal from the node to the container.
//    {
//        AllocatedExtentIterator iter(allocator.get(), node_index);
//        ASSERT_TRUE(!iter.Done());
//        const Extent* extent;
//        for (size_t i = 0; i < kInlineMaxExtents - 1; i++) {
//            ASSERT_EQ(ZX_OK, iter.Next(&extent));
//        }
//        ASSERT_EQ(ZX_ERR_IO_DATA_INTEGRITY, iter.Next(&extent));
//    }

    END_TEST;
}

// Test utilization of the BlockIterator over the allocated extent iterator
// while the underlying storage is maximally fragmented.
bool BlockIteratorFragmentedTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    constexpr size_t kAllocatedExtents = kInlineMaxExtents + kContainerMaxExtents + 1;
    constexpr size_t kAllocatedNodes = 3;

    ASSERT_TRUE(TestSetup(kAllocatedExtents, kAllocatedNodes, /* fragmented=*/ true, &space_manager,
                          &allocator, &allocated_extents, &allocated_nodes));

    // After walking, observe that the inode is allocated.
    const uint32_t node_index = allocated_nodes[0];
    const Inode* inode = allocator->GetNode(node_index);
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_EQ(kAllocatedExtents, inode->extent_count);

    AllocatedExtentIterator allocated_iter(allocator.get(), node_index);
    BlockIterator iter(&allocated_iter);
    ASSERT_EQ(0, iter.BlockIndex());
    ASSERT_FALSE(iter.Done());

    // Since we are maximally fragmented, we're polling for single block
    // extents. This means that each call to "Next" will return at most one.
    uint32_t blocks_seen = 0;

    for (size_t i = 0; i < allocated_extents.size(); i++) {
        ASSERT_FALSE(iter.Done());
        uint32_t actual_length;
        uint64_t actual_start;
        // "i + 1" is arbitrary, but it checks trying a request for "at least
        // one" block, and some additional request sizes. It doesn't matter in
        // the fragmented case, since the |actual_length| should always be one.
        ASSERT_EQ(ZX_OK, iter.Next(static_cast<uint32_t>(i + 1), &actual_length, &actual_start));
        ASSERT_EQ(1, actual_length);
        ASSERT_EQ(allocated_extents[i].Start(), actual_start);
        blocks_seen += actual_length;
        ASSERT_EQ(blocks_seen, iter.BlockIndex());
    }

    ASSERT_TRUE(iter.Done());
    END_TEST;
}

// Test utilization of the BlockIterator over the allocated extent iterator
// while the underlying storage is unfragmented.
bool BlockIteratorUnfragmentedTest() {
    BEGIN_TEST;

    MockSpaceManager space_manager;
    fbl::unique_ptr<Allocator> allocator;
    fbl::Vector<Extent> allocated_extents;
    fbl::Vector<uint32_t> allocated_nodes;
    constexpr size_t kAllocatedBlocks = 100;
    constexpr size_t kAllocatedNodes = 1;

    ASSERT_TRUE(TestSetup(kAllocatedBlocks, kAllocatedNodes, /* fragmented=*/ false,
                          &space_manager, &allocator, &allocated_extents, &allocated_nodes));

    // After walking, observe that the inode is allocated.
    const uint32_t node_index = allocated_nodes[0];
    const Inode* inode = allocator->GetNode(node_index);
    ASSERT_TRUE(inode->header.IsAllocated());
    ASSERT_EQ(1, inode->extent_count);

    // The allocation is contiguous, so the number of blocks we see is
    // completely dependent on the amount we ask for.

    // Try asking for all the blocks.
    {
        AllocatedExtentIterator allocated_iter(allocator.get(), node_index);
        BlockIterator iter(&allocated_iter);
        ASSERT_EQ(0, iter.BlockIndex());
        ASSERT_FALSE(iter.Done());
        uint32_t actual_length;
        uint64_t actual_start;
        ASSERT_EQ(ZX_OK, iter.Next(10000, &actual_length, &actual_start));
        ASSERT_EQ(kAllocatedBlocks, actual_length);
        ASSERT_EQ(allocated_extents[0].Start(), actual_start);
        ASSERT_TRUE(iter.Done());
    }

    // Try asking for some of the blocks (in a linearly increasing size).
    {
        AllocatedExtentIterator allocated_iter(allocator.get(), node_index);
        BlockIterator iter(&allocated_iter);
        ASSERT_EQ(0, iter.BlockIndex());
        ASSERT_FALSE(iter.Done());

        uint32_t blocks_seen = 0;
        size_t request_size = 1;
        while (!iter.Done()) {
            uint32_t actual_length;
            uint64_t actual_start;
            ASSERT_EQ(ZX_OK, iter.Next(static_cast<uint32_t>(request_size),
                                       &actual_length, &actual_start));
            ASSERT_EQ(fbl::min(request_size, kAllocatedBlocks - blocks_seen), actual_length);
            ASSERT_EQ(allocated_extents[0].Start() + blocks_seen, actual_start);
            request_size++;
            blocks_seen += actual_length;
        }
        ASSERT_EQ(kAllocatedBlocks, iter.BlockIndex());
    }

    END_TEST;
}

// TODO(smklein): Test against chains of extents which cause loops, such as:
//  - Inode -> Itself
//  - Inode -> Container A -> Container B -> Container A
// TODO(smklein): Test that we can defend against manually corrupted extent counts.

} // namespace
} // namespace blobfs

BEGIN_TEST_CASE(blobfsAllocatedExtentIteratorTests)
RUN_TEST(blobfs::NullTest)
RUN_TEST(blobfs::InlineNodeTest)
RUN_TEST(blobfs::MultiNodeTest)
RUN_TEST(blobfs::BadInodeNextNodeTest);
RUN_TEST(blobfs::BlockIteratorFragmentedTest);
RUN_TEST(blobfs::BlockIteratorUnfragmentedTest);
END_TEST_CASE(blobfsAllocatedExtentIteratorTests)
