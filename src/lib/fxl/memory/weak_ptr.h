// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file provides weak pointers and weak pointer factories that work like
// Chromium's |base::WeakPtr<T>| and |base::WeakPtrFactory<T>|. Note that we do
// not provide anything analogous to |base::SupportsWeakPtr<T>|.

#ifndef LIB_FXL_MEMORY_WEAK_PTR_H_
#define LIB_FXL_MEMORY_WEAK_PTR_H_

#include <inttypes.h>
#include <utility>

#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/memory/weak_ptr_internal.h"

namespace fxl {

// Forward declaration, so |WeakPtr<T>| can friend it.
template <typename T>
class WeakPtrFactory;

// Class for "weak pointers" that can be invalidated. Valid weak pointers can
// only originate from a |WeakPtrFactory| (see below), though weak pointers are
// copyable and movable.
//
// Weak pointers are not in general thread-safe. They may only be *used* on a
// single thread, namely the same thread as the "originating" |WeakPtrFactory|
// (which can invalidate the weak pointers that it generates).
//
// However, weak pointers may be passed to other threads, reset on other
// threads, or destroyed on other threads. They may also be reassigned on other
// threads (in which case they should then only be used on the thread
// corresponding to the new "originating" |WeakPtrFactory|).
template <typename T>
class WeakPtr {
 public:
  WeakPtr() : ptr_(nullptr) {}

  // Copy constructor.
  WeakPtr(const WeakPtr<T>& r) = default;

  template <typename U>
  WeakPtr(const WeakPtr<U>& r) : ptr_(r.ptr_), flag_(r.flag_) {}

  // Move constructor.
  WeakPtr(WeakPtr<T>&& r) = default;

  template <typename U>
  WeakPtr(WeakPtr<U>&& r)
      : ptr_(std::exchange(r.ptr_, nullptr)), flag_(std::move(r.flag_)) {}

  ~WeakPtr() = default;

  // The following methods are thread-friendly, in the sense that they may be
  // called subject to additional synchronization.

  // Copy assignment.
  WeakPtr<T>& operator=(const WeakPtr<T>& r) = default;

  // Move assignment.
  WeakPtr<T>& operator=(WeakPtr<T>&& r) = default;

  void reset() { flag_ = nullptr; }

  // The following methods should only be called on the same thread as the
  // "originating" |WeakPtrFactory|.

  explicit operator bool() const { return flag_ && flag_->is_valid(); }

  T* get() const { return *this ? ptr_ : nullptr; }

  T& operator*() const {
    FXL_DCHECK(*this);
    return *get();
  }

  T* operator->() const {
    FXL_DCHECK(*this);
    return get();
  }

 private:
  template <typename U>
  friend class WeakPtr;

  friend class WeakPtrFactory<T>;

  explicit WeakPtr(T* ptr, RefPtr<internal::WeakPtrFlag>&& flag)
      : ptr_(ptr), flag_(std::move(flag)) {}

  T* ptr_;
  RefPtr<internal::WeakPtrFlag> flag_;

  // Copy/move construction/assignment supported.
};

// Class that produces (valid) |WeakPtr<T>|s. Typically, this is used as a
// member variable of |T| (preferably the last one -- see below), and |T|'s
// methods control how weak pointers to it are vended. This class is not
// thread-safe, and should only be used on a single thread.
//
// Example:
//
//  class Controller {
//   public:
//    Controller() : ..., weak_factory_(this) {}
//    ...
//
//    void SpawnWorker() { Worker::StartNew(weak_factory_.GetWeakPtr()); }
//    void WorkComplete(const Result& result) { ... }
//
//   private:
//    ...
//
//    // Member variables should appear before the |WeakPtrFactory|, to ensure
//    // that any |WeakPtr|s to |Controller| are invalidated before its member
//    // variables' destructors are executed.
//    WeakPtrFactory<Controller> weak_factory_;
//  };
//
//  class Worker {
//   public:
//    static void StartNew(const WeakPtr<Controller>& controller) {
//      Worker* worker = new Worker(controller);
//      // Kick off asynchronous processing....
//    }
//
//   private:
//    Worker(const WeakPtr<Controller>& controller) : controller_(controller) {}
//
//    void DidCompleteAsynchronousProcessing(const Result& result) {
//      if (controller_)
//        controller_->WorkComplete(result);
//    }
//
//    WeakPtr<Controller> controller_;
//  };
template <typename T>
class WeakPtrFactory {
 public:
  explicit WeakPtrFactory(T* ptr) : ptr_(ptr) { FXL_DCHECK(ptr_); }
  ~WeakPtrFactory() {
    InvalidateWeakPtrs();
    FXL_DCHECK(*reinterpret_cast<uintptr_t volatile*>(const_cast<T**>(&ptr_)) =
                   kPoisonedPointer);
  }

  // Gets a new weak pointer, which will be valid until either
  // |InvalidateWeakPtrs()| is called or this object is destroyed.
  WeakPtr<T> GetWeakPtr() {
    FXL_DCHECK(reinterpret_cast<uintptr_t>(ptr_) != kPoisonedPointer);
    if (!flag_)
      flag_ = MakeRefCounted<internal::WeakPtrFlag>();
    return WeakPtr<T>(ptr_, flag_.Clone());
  }

  // Call this method to invalidate all existing weak pointers. (Note that
  // additional weak pointers can be produced even after this is called.)
  void InvalidateWeakPtrs() {
    if (!flag_)
      return;
    flag_->Invalidate();
    flag_ = nullptr;
  }

  // Call this method to determine if any weak pointers exist. (Note that a
  // "false" result is definitive, but a "true" result may not be if weak
  // pointers are held/reset/destroyed/reassigned on other threads.)
  bool HasWeakPtrs() const { return flag_ && !flag_->HasOneRef(); }

 private:
  // Value to poison |ptr_| with in debug mode to ensure that weak pointer are
  // not generated once this class has been destroyed.
  // Value must be different from 0, and invalid as a real pointer address.
  static constexpr uintptr_t kPoisonedPointer = 1;
  // Note: See weak_ptr_internal.h for an explanation of why we store the
  // pointer here, instead of in the "flag".
  T* const ptr_;
  RefPtr<internal::WeakPtrFlag> flag_;

  FXL_DISALLOW_COPY_AND_ASSIGN(WeakPtrFactory);
};

}  // namespace fxl

#endif  // LIB_FXL_MEMORY_WEAK_PTR_H_
