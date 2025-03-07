# C++ in Zircon

A subset of the C++17 language is used in the Zircon tree.  This includes both
the kernel and userspace code.  C++ is mixed with C (and some assembly) in
both places.  Some C++ language features are avoided or prohibited.  Use of
the C++ standard library features is very circumspect.

## Language features

- Not allowed
  - Exceptions
  - RTTI and `dynamic_cast`
  - Operator overloading
  - Virtual inheritance
  - Statically constructed objects
  - Trailing return type syntax
    - Exception: when necessary for lambdas with otherwise unutterable return types
  - Initializer lists
  - `thread_local` in kernel code
- Allowed
  - Pure interface inheritance
  - Lambdas
  - `constexpr`
  - `nullptr`
  - `enum class`es
  - `template`s
  - Default parameters
    - But use judgment. One optional out parameter at the end is
      probably fine. Four optional bool arguments, probably not.
  - Plain old classes
  - `auto`
  - Multiple implementation inheritance
    - But be judicious. This is used widely for e.g. intrusive
    container mixins.
- Needs more ruling TODO(cpu)
  - Global constructors
    - Currently we have these for global data structures.

**TODO:** pointer to style guide(s)?

## C++ Standard Edition (17 vs 14)

Zircon code is built with `-std=c++17` and in general can use C++ 17 language
and library features freely (subject to style/feature constraints described
[above](#language-features) and library use guidelines described
[below](#standard-library)).  There is no *general* concern with staying
compatible with C++ 14 or earlier versions.  When a standard C++ 17 feature is
the cleanest way to do something, do it that way.

**However** any library that is **published to the SDK** must be compatible
with SDK users building in **both** C++ 14 and C++ 17 modes.  So, any
libraries exported to the SDK must have public header files that are
*compatible with both `-std=c++14` and `-std=c++17`*.  If a library is
exported to the SDK as source code rather than as a binary, then its *source
code must also be completely compatible with both `-std=c++14` and
`-std=c++17`* (and not require other special options). **TODO(mcgrathr):**
_pointer to build-system docs about maintaining code to be exported to SDK_

All pure C code (`.c` source files and headers used by them) is C 11.  Some
special exceptions are made for code meant to be reused by out-of-tree boot
loaders, which stick to a conservative C 89 subset for embedded code.

## Standard Library

The C++ standard library API has many interfaces of widely varying
characteristics.  We subdivide the standard library API into several
categories below, based on the predictability and complexity of each
particular interface's code generation and use of machine and OS facilities.
These can be thought of as widening concentric circles of the API from the
most minimal C-like subset out to the full C++ 17 API.

**NOTE: TODO([ZX-1751](https://fuchsia.atlassian.net/browse/ZX-1751))**
Currently **Zircon userspace code is restricted to header-only library APIs**
from the standard C++ library.  This limitation **will be lifted** by
forthcoming fixes (or overhauls) to the C++ standard library runtime build and
the build system.  There are no limitations on host-only C++ code.  If you're
not sure whether an API is header-only, try using it in userspace code in the
Zircon tree and if you get a link-time error then it's not header-only.

#### Context Matters

This section gives guidelines for how to think about the impact of using a
particular standard C++ library API on the system as a whole.  There are no
hard and fast rules, except for the kernel (see the next section)--and except
for implementation constraints, which one always hopes should be temporary.

The overwhelming rule is **be circumspect**.

 * Consider how well you understand the time and space complexity, the dynamic
   allocation behavior (if any), and the failure modes of *each API you use*.

 * Then consider the specific *context* where it's being used, and how
   sensitive that context is to those various kinds of concerns.

 * Be especially wary about **input-dependent** behavior that can quickly
   become far harder to predict when using nontrivial library facilities.

If you're writing the main I/O logic in a driver, or anything that's in a hot
path for latency, throughput, or reliability, in any kind of system service,
then you should be pretty conservative in what library facilities you rely on.
They're all technically available to you in userspace (though far fewer in the
kernel; see the next section).  But there's not so many you actually should
use.  You probably don't want to lean on a lot of `std` containers that do
fancy dynamic allocation behind the scenes.  They will make it hard for you to
understand, predict, and control the storage/memory footprint, allocation
behavior, performance, and reliability of your service.

Nonetheless, even a driver is a userspace program that starts up and parses
configuration files or arguments and so on.  For all those nonessential or
start-time functions that are not part of the hot path, using more complex
library facilities is probably fine when that makes the work easier.  Just
remember to pay attention to overall metrics for your code, such as
minimal/total/peak runtime memory use, code bloat (which uses both device
storage and runtime memory), and resilience to unexpected failure modes.
Maybe don't double the code size and memory footprint of your driver just to
leverage that fancy configuration-parsing library.

#### No `std` in kernel

The C++ `std` namespace **cannot** be used in [kernel](../kernel) code, which
also includes [bootloader](../bootloader).  The few C++ standard library
headers that don't involve `std::` APIs can still be used directly.  See the
next section.

No other C++ standard headers should be used in kernel code.  Instead,
any library facilities worthwhile to have in the kernel (such as
`std::move`) are provided via kernel-specific APIs (such as
`ktl::move`).  The kernel's implementations of these APIs may in fact
rely on toolchain headers providing `std::` implementations that are
aliased to kernel API names.  But only those API implementations and
very special cases in certain library headers should ever use `std::`
in source code built into the kernel.

#### Universal headers

These header APIs are safe to use everywhere, even in the kernel.

They include the C++ wrappers on the subset of standard C interfaces that the
kernel supports:

 * [`<cstdarg>`](https://en.cppreference.com/w/cpp/header/cstdarg)
 * [`<cstddef>`](https://en.cppreference.com/w/cpp/header/cstddef)
 * [`<climits>`](https://en.cppreference.com/w/cpp/header/climits)
 * [`<cstdint>`](https://en.cppreference.com/w/cpp/header/cstdint)
 * [`<cinttypes>`](https://en.cppreference.com/w/cpp/header/cinttypes)
 * [`<cassert>`](https://en.cppreference.com/w/cpp/header/cassert)
 * [`<cstring>`](https://en.cppreference.com/w/cpp/header/cstring)

The `std` namespace aliases for C library APIs from these headers should not
be used in kernel code.

One pure C++ header is also available even in the kernel:

 * [`<new>`](https://en.cppreference.com/w/cpp/header/new)

   The vanilla non-placement `operator new` and `operator new[]` are not
   available in the kernel.  Use [`fbl::AllocChecker`
   `new`](../system/ulib/fbl/include/fbl/alloc_checker.h) instead.

#### Conservative userspace

These header APIs are safe to use everywhere.  They're not allowed in the
kernel because they're all entirely in the `std` namespace.  But subsets of
these APIs are likely candidates to get an in-kernel API alias if there is a
good case for using such an API in kernel code.

These are pure header-only types and templates.  They don't do any dynamic
allocation of their own.  The time and space complexity of each function
should be clear from its description.

 * [`<algorithm>`](https://en.cppreference.com/w/cpp/header/algorithm)
 * [`<array>`](https://en.cppreference.com/w/cpp/header/array)
 * [`<atomic>`](https://en.cppreference.com/w/cpp/header/atomic)
 * [`<bitset>`](https://en.cppreference.com/w/cpp/header/bitset)
 * [`<initializer_list>`](https://en.cppreference.com/w/cpp/header/initializer_list)
 * [`<iterator>`](https://en.cppreference.com/w/cpp/header/iterator)
 * [`<limits>`](https://en.cppreference.com/w/cpp/header/limits)
 * [`<optional>`](https://en.cppreference.com/w/cpp/header/optional)
 * [`<tuple>`](https://en.cppreference.com/w/cpp/header/tuple)
 * [`<type_traits>`](https://en.cppreference.com/w/cpp/header/type_traits)
 * [`<utility>`](https://en.cppreference.com/w/cpp/header/utility)
 * [`<variant>`](https://en.cppreference.com/w/cpp/header/variant)

These involve some dynamic allocation, but only what's explicit:

 * [`<any>`](https://en.cppreference.com/w/cpp/header/any)
 * [`<memory>`](https://en.cppreference.com/w/cpp/header/memory)

   The `std::shared_ptr`, `std::weak_ptr`, and `std::auto_ptr` APIs should
   **never** be used.  Use `std::unique_ptr` and `fbl::RefPtr` instead.

##### Userspace-only

These are not things that would ever be available at all or by any similar API
or name in the kernel.  But they are generally harmless everywhere in
userspace.  They do not involve dynamic allocation.

 * Floating-point is never available in kernel code, but can be used
   (subject to performance considerations) in all userspace code.
   * [`<cfenv>`](https://en.cppreference.com/w/cpp/header/cfenv)
   * [`<cfloat>`](https://en.cppreference.com/w/cpp/header/cfloat)
   * [`<cmath>`](https://en.cppreference.com/w/cpp/header/cmath)
   * [`<complex>`](https://en.cppreference.com/w/cpp/header/complex)
   * [`<numeric>`](https://en.cppreference.com/w/cpp/header/numeric)
   * [`<ratio>`](https://en.cppreference.com/w/cpp/header/ratio)
   * [`<valarray>`](https://en.cppreference.com/w/cpp/header/valarray)

 * Full C 11 standard library, via C++ wrappers or in standard C `<*.h>`.
   * [`<csetjmp>`](https://en.cppreference.com/w/cpp/header/csetjmp)
   * [`<cstdlib>`](https://en.cppreference.com/w/cpp/header/cstdlib)
   * [Standard C11 interfaces](https://en.cppreference.com/w/c/header)

 * Synchronization and threads.  These standard APIs are safe to use in all
   userspace code with appropriate discretion.  But it may often be better to
   use Zircon's own library APIs for similar things, such as
   [<lib/sync/...>](../system/ulib/sync/include).
   * [`<condition_variable>`](https://en.cppreference.com/w/cpp/header/condition_variable)
   * [`<execution>`](https://en.cppreference.com/w/cpp/header/execution)
   * [`<mutex>`](https://en.cppreference.com/w/cpp/header/mutex)
   * [`<shared_mutex>`](https://en.cppreference.com/w/cpp/header/shared_mutex)
   * [`<thread>`](https://en.cppreference.com/w/cpp/header/thread)

#### Kitchen sink

These involve dynamic allocation that is hard to predict and is generally out
of your control.  The exact runtime behavior and memory requirements are often
hard to reason about.  Think very hard before using these interfaces in any
critical path for reliability or performance or in any component that is meant
to be lean and space-efficient.

 * The entire [Containers library](https://en.cppreference.com/w/cpp/container)

 * [`<functional>`](https://en.cppreference.com/w/cpp/header/functional)

   See [`<lib/fit/function.h>`](../system/ulib/fit/include/lib/fit/function.h)
   for a homegrown alternative.

 * [`<memory_resource>`](https://en.cppreference.com/w/cpp/header/memory_resource)
 * [`<scoped_allocator>`](https://en.cppreference.com/w/cpp/header/scoped_allocator)

 * [`<filesystem>`](https://en.cppreference.com/w/cpp/header/filesystem)
 * [`<regex>`](https://en.cppreference.com/w/cpp/header/regex)

## FBL

FBL is the Fuchsia Base Library, which is shared between kernel and userspace.
As a result, FBL has very strict dependencies.  For example, FBL cannot depend
on the syscall interface because the syscall interface is not available within
the kernel.  Similarly, FBL cannot depend on C library features that are not
available in the kernel.

1. [system/ulib/fbl](../system/ulib/fbl) which is usable from both
   kernel and userspace.
2. [kernel/lib/fbl](../kernel/lib/fbl) which is usable only from
    the kernel.

**NOTE:** Some FBL interfaces below that overlap with standard C++ library
interfaces will probably be either removed entirely or made kernel-only (and
perhaps renamed inside the kernel) once userspace code has migrated to using
standard C++ library facilities where appropriate.

FBL provides:

- utility code
  - [basic algorithms](../system/ulib/fbl/include/fbl/algorithm.h)
  - [atomics](../system/ulib/fbl/include/fbl/atomic.h)
  - [alloc checking new](../system/ulib/fbl/include/fbl/alloc_checker.h)
- allocators
  - [slab allocation](../system/ulib/fbl/include/fbl/slab_allocator.h)
  - [slab malloc](../system/ulib/fbl/include/fbl/slab_malloc.h)
- arrays
  - [fixed sized arrays](../system/ulib/fbl/include/fbl/array.h)
  - [fixed sized arrays](../kernel/lib/fbl/include/fbl/inline_array.h),
    which stack allocates small arrays
- inline containers
  - [doubly linked list](../system/ulib/fbl/include/fbl/intrusive_double_list.h)
  - [hash table](../system/ulib/fbl/include/fbl/intrusive_hash_table.h)
  - [singly linked list](../system/ulib/fbl/include/fbl/intrusive_single_list.h)
  - [wavl trees](../system/ulib/fbl/include/fbl/intrusive_wavl_tree.h)
- smart pointers
  - [intrusive refcounted mixin](../system/ulib/fbl/include/fbl/ref_counted.h)
  - [intrusive refcounting pointer](../system/ulib/fbl/include/fbl/ref_ptr.h)
  - [unique pointer](../system/ulib/fbl/include/fbl/unique_ptr.h)
- raii utilities
  - [auto call](../system/ulib/fbl/include/fbl/auto_call.h) to run
    code upon leaving scope
  - [AutoLock](../system/ulib/fbl/include/fbl/auto_lock.h)

FBL has strict controls on memory allocation.  Memory allocation should be
explicit, using an AllocChecker to let clients recover from allocation
failures.  In some cases, implicit memory allocation is permitted, but
functions that implicitly allocate memory must be #ifdef'ed to be unavailable
in the kernel.

FBL not available outside the Platform Source Tree.

## ZX

ZX contains C++ wrappers for the Zircon [objects](objects) and
[syscalls](syscalls.md).  These wrappers provide type safety and move semantics
for handles but offer no opinion beyond what's in syscalls.abigen.  At some
point in the future, we might autogenerate ZX from syscalls.abigen, similar to
how we autogenerate the syscall wrappers in other languages.

ZX is part of the Fuchsia SDK.

## FZL

FZL is the Fuchsia Zircon Library.  This library provides value-add for common
operations involving kernel objects and is free to have opinions about how to
interact with the Zircon syscalls.  If a piece of code has no dependency on
Zircon syscalls, the code should go in FBL instead.

FZL not available outside the Platform Source Tree.

## ZXCPP

Some of our code runs in an environment which cannot include the
standard C++ runtime environment. This environment includes symbols
like __cxa_pure_virtual that are defined by the ABI and that the
compiler expects to be ambient. [The zxcpp
library](../system/ulib/zxcpp) provides that dependency. It also
includes the placement operator new overloads and, in userspace, the
standard new and delete operators. Note that it does not include the
similarly named __cxa_atexit, which in userspace must be provided by
the libc. See extensive comments in musl's atexit implementation if
you are curious.

*This library is mutually exclusive of the standard C++ library.*

**NOTE: TODO([ZX-1751](https://fuchsia.atlassian.net/browse/ZX-1751))** The
ZXCPP library will be removed in favor of hermetic static linking for the
standard C++ library.  Closed-world C++ modules such as drivers or shared
libraries with a pure C ABI can use the standard C++ library internally
without affecting their outward ABIs.  Current users of ZXCPP will become
users of the hermetic static standard C++ library that link in only the
minimal ABI symbols that ZXCPP used to provide.
