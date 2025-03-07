// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <fbl/arena.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/macros.h>
#include <fbl/mutex.h>
#include <fbl/ref_ptr.h>
#include <kernel/brwlock.h>
#include <kernel/lockdep.h>
#include <ktl/atomic.h>
#include <ktl/move.h>
#include <stdint.h>
#include <zircon/types.h>

class Dispatcher;
class Handle;

template <typename T>
class KernelHandle;

// HandleOwner wraps a Handle in a unique_ptr-like object that has single
// ownership of the Handle and deletes it whenever it falls out of scope.
class HandleOwner {
public:
    HandleOwner() = default;
    HandleOwner(decltype(nullptr)) : h_(nullptr) {}

    explicit HandleOwner(Handle* h) : h_(h) {}

    HandleOwner(const HandleOwner&) = delete;
    HandleOwner& operator=(const HandleOwner&) = delete;

    HandleOwner(HandleOwner&& other) : h_(other.release()) {}

    HandleOwner& operator=(HandleOwner&& other) {
        reset(other.release());
        return *this;
    }

    ~HandleOwner() {
        Destroy();
    }

    Handle* operator->() const {
        return h_;
    }

    Handle* get() const {
        return h_;
    }

    Handle* release() {
        Handle* h = h_;
        h_ = nullptr;
        return h;
    }

    void reset(Handle* h) {
        Destroy();
        h_ = h;
    }

    void swap(HandleOwner& other) {
        Handle* h = h_;
        h_ = other.h_;
        other.h_ = h;
    }

    explicit operator bool() const { return h_ != nullptr; }

private:
    // Defined inline below.
    inline void Destroy();

    Handle* h_ = nullptr;
};

// A Handle is how a specific process refers to a specific Dispatcher.
class Handle final : public fbl::DoublyLinkedListable<Handle*> {
public:
    // The handle arena's lock. This is public since it protects
    // other things like |Dispatcher::handle_count_|.
    LOCK_DEP_SINGLETON_LOCK(ArenaLock, BrwLock);

    // Returns the Dispatcher to which this instance points.
    const fbl::RefPtr<Dispatcher>& dispatcher() const { return dispatcher_; }

    // Returns the process that owns this instance. Used to guarantee
    // that one process may not access a handle owned by a different process.
    zx_koid_t process_id() const {
        return process_id_.load(ktl::memory_order_relaxed);
    }

    // Sets the value returned by process_id().
    void set_process_id(zx_koid_t pid);

    // Returns the |rights| parameter that was provided when this instance
    // was created.
    uint32_t rights() const {
        return rights_;
    }

    // Returns true if this handle has all of the desired rights bits set.
    bool HasRights(zx_rights_t desired) const {
        return (rights_ & desired) == desired;
    }

    // Returns a value that can be decoded by Handle::FromU32() to derive a
    // pointer to this instance.  ProcessDispatcher will XOR this with its
    // |handle_rand_| to create the zx_handle_t value that user space sees.
    uint32_t base_value() const {
        return base_value_;
    }

    // To be called once during bring up.
    static void Init();

    // Maps an integer obtained by Handle::base_value() back to a Handle.
    static Handle* FromU32(uint32_t value);

    // Get the number of outstanding handles for a given dispatcher.
    static uint32_t Count(const fbl::RefPtr<const Dispatcher>&);

    // Should only be called by diagnostics.cpp.
    struct diagnostics {
        // Dumps internal details of the handle table using printf().
        static void DumpTableInfo();

        // Returns the number of outstanding handles.
        static size_t OutstandingHandles();
    };

    // Handle should never be created by anything other than Make or Dup.
    static HandleOwner Make(
        fbl::RefPtr<Dispatcher> dispatcher, zx_rights_t rights);
    static HandleOwner Make(
        KernelHandle<Dispatcher> kernel_handle, zx_rights_t rights);
    static HandleOwner Dup(Handle* source, zx_rights_t rights);

private:
    DISALLOW_COPY_ASSIGN_AND_MOVE(Handle);

    // Called only by Make.
    Handle(fbl::RefPtr<Dispatcher> dispatcher,
           zx_rights_t rights, uint32_t base_value);
    // Called only by Dup.
    Handle(Handle* rhs, zx_rights_t rights, uint32_t base_value);

    // Private subroutines of Make and Dup.
    static void* Alloc(const fbl::RefPtr<Dispatcher>&, const char* what,
                       uint32_t* base_value);
    static uint32_t GetNewBaseValue(void* addr);

    // Handle should never be destroyed by anything other than Delete,
    // which uses TearDown to do the actual destruction.
    ~Handle() = default;
    void TearDown() TA_EXCL(ArenaLock::Get());
    void Delete();

    // Only HandleOwner is allowed to call Delete.
    friend class HandleOwner;

    // process_id_ is atomic because threads from different processes can
    // access it concurrently, while holding different instances of
    // handle_table_lock_.
    ktl::atomic<zx_koid_t> process_id_;
    fbl::RefPtr<Dispatcher> dispatcher_;
    const zx_rights_t rights_;
    const uint32_t base_value_;

    // The handle arena.
    static fbl::Arena TA_GUARDED(ArenaLock::Get()) arena_;

    // NOTE! This can return an invalid address.  It must be checked
    // against the arena bounds before being cast to a Handle*.
    static uintptr_t IndexToHandle(uint32_t index) TA_NO_THREAD_SAFETY_ANALYSIS {
        return reinterpret_cast<uintptr_t>(arena_.start()) + index * sizeof(Handle);
    }

    static uint32_t HandleToIndex(Handle* handle) TA_NO_THREAD_SAFETY_ANALYSIS {
        return static_cast<uint32_t>(
            handle - reinterpret_cast<Handle*>(arena_.start()));
    }
};

// This can't be defined directly in the HandleOwner class definition
// because Handle is an incomplete type at that point.
inline void HandleOwner::Destroy() {
    if (h_)
        h_->Delete();
}

// A minimal wrapper around a Dispatcher which is owned by the kernel.
//
// Intended usage when creating new a Dispatcher object is:
//   1. Create a KernelHandle on the stack (cannot fail)
//   2. Move the RefPtr<Dispatcher> into the KernelHandle (cannot fail)
//   3. When ready to give the handle to a process, upgrade the KernelHandle
//      to a full HandleOwner via UpgradeToHandleOwner() or
//      user_out_handle::make() (can fail)
//
// This sequence ensures that the Dispatcher's on_zero_handles() method is
// called even if errors occur during or before HandleOwner creation, which
// is necessary to break circular references for some Dispatcher types.
//
// This class is thread-unsafe and must be externally synchronized if used
// across multiple threads.
template <typename T>
class KernelHandle {
public:
    KernelHandle() = default;

    explicit KernelHandle(fbl::RefPtr<T> dispatcher)
        : dispatcher_(ktl::move(dispatcher)) {}

    ~KernelHandle() {
        reset();
    }

    // Movable but not copyable since we own the underlying Dispatcher.
    KernelHandle(const KernelHandle&) = delete;
    KernelHandle& operator=(const KernelHandle&) = delete;

    template <typename U>
    KernelHandle(KernelHandle<U>&& other)
        : dispatcher_(ktl::move(other.dispatcher_)) {}

    template <typename U>
    KernelHandle& operator=(KernelHandle<U>&& other) {
        reset(ktl::move(other.dispatcher_));
        return *this;
    }

    void reset() {
        reset(fbl::RefPtr<T>());
    }

    template <typename U>
    void reset(fbl::RefPtr<U> dispatcher) {
        if (dispatcher_) {
            dispatcher_->on_zero_handles();
        }
        dispatcher_ = ktl::move(dispatcher);
    }

    const fbl::RefPtr<T>& dispatcher() const { return dispatcher_; }

    fbl::RefPtr<T> release() {
        return ktl::move(dispatcher_);
    }

private:
    template <typename U>
    friend class KernelHandle;

    fbl::RefPtr<T> dispatcher_;
};
