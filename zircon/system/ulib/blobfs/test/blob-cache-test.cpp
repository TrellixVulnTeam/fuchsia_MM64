// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <blobfs/blob-cache.h>
#include <blobfs/cache-node.h>
#include <unittest/unittest.h>

#include "utils.h"

namespace blobfs {
namespace {

// A mock Node, comparable to Blob.
//
// "ShouldCache" mimics the internal Vnode state machine.
// "UsingMemory" mimics the storage of pages and mappings, which may be evicted
// from memory when references are closed.
class TestNode : public CacheNode, fbl::Recyclable<TestNode> {
public:
    explicit TestNode(const Digest& digest, BlobCache* cache)
        : CacheNode(digest), cache_(cache), should_cache_(true), using_memory_(false) {}

    void fbl_recycle() final {
        CacheNode::fbl_recycle();
    }

    BlobCache& Cache() final {
        return *cache_;
    }

    bool ShouldCache() const final {
        return should_cache_;
    }

    void ActivateLowMemory() final {
        using_memory_ = false;
    }

    bool UsingMemory() {
        return using_memory_;
    }

    void SetCache(bool should_cache) {
        should_cache_ = should_cache;
    }

    void SetHighMemory() {
        using_memory_ = true;
    }

    bool IsDirectory() const final {
        return false;
    }

    zx_status_t GetNodeInfo(uint32_t flags, fuchsia_io_NodeInfo* info) {
        info->tag = fuchsia_io_NodeInfoTag_service;
        return ZX_OK;
    }

private:
    BlobCache* cache_;
    bool should_cache_;
    bool using_memory_;
};

Digest GenerateDigest(size_t seed) {
    Digest digest;
    ZX_ASSERT(digest.Init() == ZX_OK);
    digest.Update(&seed, sizeof(seed));
    digest.Final();
    return digest;
}

bool CheckNothingOpenHelper(BlobCache* cache) {
    BEGIN_HELPER;
    ASSERT_NONNULL(cache);
    cache->ForAllOpenNodes([](fbl::RefPtr<CacheNode>) {
        ZX_ASSERT(false);
    });
    END_HELPER;
}

bool NullTest() {
    BEGIN_TEST;

    BlobCache cache;

    ASSERT_TRUE(CheckNothingOpenHelper(&cache));
    cache.Reset();
    ASSERT_TRUE(CheckNothingOpenHelper(&cache));

    Digest digest = GenerateDigest(0);
    fbl::RefPtr<CacheNode> missing_node;
    ASSERT_EQ(ZX_ERR_NOT_FOUND, cache.Lookup(digest, nullptr));
    ASSERT_EQ(ZX_ERR_NOT_FOUND, cache.Lookup(digest, &missing_node));
    fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(digest, &cache));
    ASSERT_EQ(ZX_ERR_NOT_FOUND, cache.Evict(node));
    node->SetCache(false);

    END_TEST;
}

bool AddLookupEvictTest() {
    BEGIN_TEST;

    // Add a node to the cache.
    BlobCache cache;
    Digest digest = GenerateDigest(0);
    fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(digest, &cache));
    ASSERT_EQ(ZX_OK, cache.Add(node));
    ASSERT_EQ(ZX_ERR_ALREADY_EXISTS, cache.Add(node));

    // Observe that we can access the node inside the cache.
    fbl::RefPtr<CacheNode> found_node;
    ASSERT_EQ(ZX_OK, cache.Lookup(digest, nullptr));
    ASSERT_EQ(ZX_OK, cache.Lookup(digest, &found_node));
    ASSERT_EQ(found_node.get(), node.get());

    // Observe that evicting the node removes it from the cache.
    ASSERT_EQ(ZX_OK, cache.Evict(node));
    ASSERT_EQ(ZX_ERR_NOT_FOUND, cache.Lookup(digest, nullptr));

    END_TEST;
}

// ShouldCache = false, Evicted = false.
//
// This results in the node being deleted from the cache.
bool StopCachingTest() {
    BEGIN_TEST;

    BlobCache cache;
    Digest digest = GenerateDigest(0);
    // The node is also deleted if we stop caching it, instead of just evicting.
    {
        fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(digest, &cache));
        ASSERT_EQ(ZX_OK, cache.Add(node));
        ASSERT_EQ(ZX_OK, cache.Lookup(digest, nullptr));
        node->SetCache(false);
    }
    ASSERT_EQ(ZX_ERR_NOT_FOUND, cache.Lookup(digest, nullptr));

    END_TEST;
}

// ShouldCache = false, Evicted = True.
//
// This results in the node being deleted from the cache.
bool EvictNoCacheTest() {
    BEGIN_TEST;

    BlobCache cache;
    Digest digest = GenerateDigest(0);
    // The node is also deleted if we stop caching it, instead of just evicting.
    {
        fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(digest, &cache));
        ASSERT_EQ(ZX_OK, cache.Add(node));
        ASSERT_EQ(ZX_OK, cache.Lookup(digest, nullptr));
        ASSERT_EQ(ZX_OK, cache.Evict(node));
    }
    ASSERT_EQ(ZX_ERR_NOT_FOUND, cache.Lookup(digest, nullptr));

    END_TEST;
}

// ShouldCache = true, Evicted = true.
//
// This results in the node being deleted from the cache.
bool EvictWhileCachingTest() {
    BEGIN_TEST;

    BlobCache cache;
    Digest digest = GenerateDigest(0);
    // The node is automatically deleted if it wants to be cached, but has been
    // evicted.
    {
        fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(digest, &cache));
        ASSERT_EQ(ZX_OK, cache.Add(node));
        ASSERT_EQ(ZX_OK, cache.Lookup(digest, nullptr));
        ASSERT_EQ(ZX_OK, cache.Evict(node));
        node->SetCache(true);
    }
    ASSERT_EQ(ZX_ERR_NOT_FOUND, cache.Lookup(digest, nullptr));

    END_TEST;
}

// This helper function only operates correctly when a single node is open in the cache.
bool CheckExistsAloneInOpenCache(BlobCache* cache, void* node_ptr) {
    BEGIN_HELPER;
    ASSERT_NONNULL(cache);
    size_t node_count = 0;
    cache->ForAllOpenNodes([&node_count, &node_ptr](fbl::RefPtr<CacheNode> node) {
        node_count++;
        ZX_ASSERT(node.get() == node_ptr);
    });
    ASSERT_EQ(1, node_count);
    END_HELPER;
}

bool CacheAfterRecycleTest() {
    BEGIN_TEST;

    BlobCache cache;
    Digest digest = GenerateDigest(0);
    void* node_ptr = nullptr;

    // Add a node to the cache.
    {
        fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(digest, &cache));
        node_ptr = node.get();
        ASSERT_EQ(ZX_OK, cache.Add(node));
        ASSERT_EQ(ZX_OK, cache.Lookup(digest, nullptr));

        // Observe the node is in the set of open nodes.
        ASSERT_TRUE(CheckExistsAloneInOpenCache(&cache, node_ptr));
    }

    // Observe the node is in no longer in the set of open nodes, now that it has
    // run out of strong references.
    ASSERT_TRUE(CheckNothingOpenHelper(&cache));

    // Observe that although the node in in the "closed set", it still exists in the cache,
    // and can be re-acquired.
    ASSERT_EQ(ZX_OK, cache.Lookup(digest, nullptr));

    // Letting the node go out of scope puts it back in the cache.
    {
        fbl::RefPtr<CacheNode> node;
        ASSERT_EQ(ZX_OK, cache.Lookup(digest, &node));
        ASSERT_EQ(node_ptr, node.get());
        ASSERT_TRUE(CheckExistsAloneInOpenCache(&cache, node_ptr));
    }
    ASSERT_EQ(ZX_OK, cache.Lookup(digest, nullptr));

    // However, if we stop caching the node, it will be deleted when all references
    // go out of scope.
    {
        fbl::RefPtr<CacheNode> cache_node;
        ASSERT_EQ(ZX_OK, cache.Lookup(digest, &cache_node));
        auto vnode = fbl::RefPtr<TestNode>::Downcast(std::move(cache_node));
        ASSERT_EQ(ZX_OK, cache.Evict(vnode));
    }
    ASSERT_EQ(ZX_ERR_NOT_FOUND, cache.Lookup(digest, nullptr));

    END_TEST;
}

bool ResetClosedTest() {
    BEGIN_TEST;

    BlobCache cache;
    // Create a node which exists in the closed cache.
    Digest digest = GenerateDigest(0);
    {
        fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(digest, &cache));
        ASSERT_EQ(ZX_OK, cache.Add(node));
    }
    ASSERT_EQ(ZX_OK, cache.Lookup(digest, nullptr));

    // After resetting, the node should no longer exist.
    cache.Reset();
    ASSERT_EQ(ZX_ERR_NOT_FOUND, cache.Lookup(digest, nullptr));

    END_TEST;
}

bool ResetOpenTest() {
    BEGIN_TEST;

    BlobCache cache;
    // Create a node which exists in the open cache.
    Digest digest = GenerateDigest(0);
    fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(digest, &cache));
    ASSERT_EQ(ZX_OK, cache.Add(node));

    // After resetting, the node should no longer exist.
    cache.Reset();
    ASSERT_EQ(ZX_ERR_NOT_FOUND, cache.Lookup(digest, nullptr));

    END_TEST;
}

bool DestructorTest() {
    BEGIN_TEST;

    fbl::RefPtr<TestNode> open_node;

    {
        BlobCache cache;
        Digest open_digest = GenerateDigest(0);
        open_node = fbl::AdoptRef(new TestNode(open_digest, &cache));
        open_node->SetHighMemory();

        Digest closed_digest = GenerateDigest(1);
        auto closed_node = fbl::AdoptRef(new TestNode(closed_digest, &cache));
        ASSERT_EQ(ZX_OK, cache.Add(open_node));
        ASSERT_EQ(ZX_OK, cache.Add(closed_node));
    }
    ASSERT_TRUE(open_node->UsingMemory());

    END_TEST;
}

bool ForAllOpenNodesTest() {
    BEGIN_TEST;

    BlobCache cache;

    // Add a bunch of open nodes to the cache.
    fbl::RefPtr<TestNode> open_nodes[10];
    for (size_t i = 0; i < fbl::count_of(open_nodes); i++) {
        open_nodes[i] = fbl::AdoptRef(new TestNode(GenerateDigest(i), &cache));
        ASSERT_EQ(ZX_OK, cache.Add(open_nodes[i]));
    }

    // For fun, add some nodes to the cache which will become non-open:
    // One which runs out of strong references, and another which is evicted.
    {
        fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(GenerateDigest(0xDEAD), &cache));
        ASSERT_EQ(ZX_OK, cache.Add(node));
    }
    fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(GenerateDigest(0xBEEF), &cache));
    ASSERT_EQ(ZX_OK, cache.Add(node));
    ASSERT_EQ(ZX_OK, cache.Evict(node));

    // Double check that the nodes which should be open are open, and that the nodes
    // which aren't open aren't visible.
    size_t node_index = 0;
    cache.ForAllOpenNodes([&open_nodes, &node_index](fbl::RefPtr<CacheNode> node) {
        ZX_ASSERT(node_index < fbl::count_of(open_nodes));
        for (size_t i = 0; i < fbl::count_of(open_nodes); i++) {
            // We should be able to find this node in the set of open nodes -- but only once.
            if (open_nodes[i] && open_nodes[i].get() == node.get()) {
                open_nodes[i] = nullptr;
                node_index++;
                return;
            }
        }
        ZX_ASSERT_MSG(false, "Found open node not contained in expected open set");
    });
    ASSERT_EQ(fbl::count_of(open_nodes), node_index);

    END_TEST;
}

bool CachePolicyEvictImmediatelyTest() {
    BEGIN_TEST;

    BlobCache cache;
    Digest digest = GenerateDigest(0);

    cache.SetCachePolicy(CachePolicy::EvictImmediately);
    {
        fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(digest, &cache));
        node->SetHighMemory();
        ASSERT_EQ(ZX_OK, cache.Add(node));
        ASSERT_TRUE(node->UsingMemory());
    }

    fbl::RefPtr<CacheNode> cache_node;
    ASSERT_EQ(ZX_OK, cache.Lookup(digest, &cache_node));
    auto node = fbl::RefPtr<TestNode>::Downcast(std::move(cache_node));
    ASSERT_FALSE(node->UsingMemory());

    END_TEST;
}

bool CachePolicyNeverEvictTest() {
    BEGIN_TEST;

    BlobCache cache;
    Digest digest = GenerateDigest(0);

    cache.SetCachePolicy(CachePolicy::NeverEvict);
    {
        fbl::RefPtr<TestNode> node = fbl::AdoptRef(new TestNode(digest, &cache));
        node->SetHighMemory();
        ASSERT_EQ(ZX_OK, cache.Add(node));
        ASSERT_TRUE(node->UsingMemory());
    }

    fbl::RefPtr<CacheNode> cache_node;
    ASSERT_EQ(ZX_OK, cache.Lookup(digest, &cache_node));
    auto node = fbl::RefPtr<TestNode>::Downcast(std::move(cache_node));
    ASSERT_TRUE(node->UsingMemory());

    END_TEST;
}

} // namespace
} // namespace blobfs

BEGIN_TEST_CASE(blobfsBlobCacheTests)
RUN_TEST(blobfs::NullTest)
RUN_TEST(blobfs::AddLookupEvictTest)
RUN_TEST(blobfs::StopCachingTest)
RUN_TEST(blobfs::EvictNoCacheTest)
RUN_TEST(blobfs::EvictWhileCachingTest)
RUN_TEST(blobfs::CacheAfterRecycleTest)
RUN_TEST(blobfs::ResetClosedTest)
RUN_TEST(blobfs::ResetOpenTest)
RUN_TEST(blobfs::DestructorTest)
RUN_TEST(blobfs::ForAllOpenNodesTest)
RUN_TEST(blobfs::CachePolicyEvictImmediatelyTest)
RUN_TEST(blobfs::CachePolicyNeverEvictTest)
END_TEST_CASE(blobfsBlobCacheTests)
