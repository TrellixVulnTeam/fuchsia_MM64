// WARNING: This file is machine generated by fidlgen.

#pragma once

#include <lib/fidl/internal.h>
#include <lib/fidl/cpp/vector_view.h>
#include <lib/fidl/cpp/string_view.h>
#include <lib/fidl/llcpp/array.h>
#include <lib/fidl/llcpp/coding.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/fidl/llcpp/transaction.h>
#include <lib/zx/channel.h>
#include <zircon/fidl.h>

namespace fidl {
namespace test {
namespace llcpp {
namespace controlflow {

class ControlFlow;


// Interface for testing shutdown/epitaphs etc.
class ControlFlow final {
 public:

  using ShutdownRequest = ::fidl::AnyZeroArgMessage;

  using NoReplyMustSendAccessDeniedEpitaphRequest = ::fidl::AnyZeroArgMessage;

  struct MustSendAccessDeniedEpitaphResponse {
    FIDL_ALIGNDECL
    fidl_message_header_t _hdr;
    int32_t reply;

    static constexpr const fidl_type_t* Type = nullptr;
    static constexpr uint32_t MaxNumHandles = 0;
    static constexpr uint32_t PrimarySize = 24;
    static constexpr uint32_t MaxOutOfLine = 0;
  };
  using MustSendAccessDeniedEpitaphRequest = ::fidl::AnyZeroArgMessage;


  class SyncClient final {
   public:
    SyncClient(::zx::channel channel) : channel_(std::move(channel)) {}

    ~SyncClient() {}

    // Shutdown the server without a reply.
    // The server should unbind the channel from the dispatch loop, closing it.
    zx_status_t Shutdown();

    // Calling this method generates no reply and a epitaph with error set to
    // |ZX_ERR_ACCESS_DENIED|. The channel will then be closed.
    // This tests sending an epitaph from the one-way method call handler.
    zx_status_t NoReplyMustSendAccessDeniedEpitaph();

    // Despite the fact that a reply was defined in the method signature,
    // Calling this method generates no reply and a epitaph with error set to
    // |ZX_ERR_ACCESS_DENIED|. The channel will then be closed.
    // This tests sending an epitaph from a normal (two-way) method call handler.
    zx_status_t MustSendAccessDeniedEpitaph(int32_t* out_reply);

    // Despite the fact that a reply was defined in the method signature,
    // Calling this method generates no reply and a epitaph with error set to
    // |ZX_ERR_ACCESS_DENIED|. The channel will then be closed.
    // This tests sending an epitaph from a normal (two-way) method call handler.
    // Caller provides the backing storage for FIDL message via request and response buffers.
    zx_status_t MustSendAccessDeniedEpitaph(::fidl::BytePart _response_buffer, int32_t* out_reply);

    // Despite the fact that a reply was defined in the method signature,
    // Calling this method generates no reply and a epitaph with error set to
    // |ZX_ERR_ACCESS_DENIED|. The channel will then be closed.
    // This tests sending an epitaph from a normal (two-way) method call handler.
    // Messages are encoded and decoded in-place.
    ::fidl::DecodeResult<MustSendAccessDeniedEpitaphResponse> MustSendAccessDeniedEpitaph(::fidl::BytePart response_buffer);

   private:
    ::zx::channel channel_;
  };

  // Methods to make a sync FIDL call directly on an unowned channel, avoiding setting up a client.
  class Call final {
   public:

    // Shutdown the server without a reply.
    // The server should unbind the channel from the dispatch loop, closing it.
    static zx_status_t Shutdown(zx::unowned_channel _client_end);

    // Calling this method generates no reply and a epitaph with error set to
    // |ZX_ERR_ACCESS_DENIED|. The channel will then be closed.
    // This tests sending an epitaph from the one-way method call handler.
    static zx_status_t NoReplyMustSendAccessDeniedEpitaph(zx::unowned_channel _client_end);

    // Despite the fact that a reply was defined in the method signature,
    // Calling this method generates no reply and a epitaph with error set to
    // |ZX_ERR_ACCESS_DENIED|. The channel will then be closed.
    // This tests sending an epitaph from a normal (two-way) method call handler.
    static zx_status_t MustSendAccessDeniedEpitaph(zx::unowned_channel _client_end, int32_t* out_reply);

    // Despite the fact that a reply was defined in the method signature,
    // Calling this method generates no reply and a epitaph with error set to
    // |ZX_ERR_ACCESS_DENIED|. The channel will then be closed.
    // This tests sending an epitaph from a normal (two-way) method call handler.
    // Caller provides the backing storage for FIDL message via request and response buffers.
    static zx_status_t MustSendAccessDeniedEpitaph(zx::unowned_channel _client_end, ::fidl::BytePart _response_buffer, int32_t* out_reply);

    // Despite the fact that a reply was defined in the method signature,
    // Calling this method generates no reply and a epitaph with error set to
    // |ZX_ERR_ACCESS_DENIED|. The channel will then be closed.
    // This tests sending an epitaph from a normal (two-way) method call handler.
    // Messages are encoded and decoded in-place.
    static ::fidl::DecodeResult<MustSendAccessDeniedEpitaphResponse> MustSendAccessDeniedEpitaph(zx::unowned_channel _client_end, ::fidl::BytePart response_buffer);

  };

  // Pure-virtual interface to be implemented by a server.
  class Interface {
   public:
    Interface() = default;
    virtual ~Interface() = default;
    using _Outer = ControlFlow;
    using _Base = ::fidl::CompleterBase;

    using ShutdownCompleter = ::fidl::Completer<>;

    virtual void Shutdown(ShutdownCompleter::Sync _completer) = 0;

    using NoReplyMustSendAccessDeniedEpitaphCompleter = ::fidl::Completer<>;

    virtual void NoReplyMustSendAccessDeniedEpitaph(NoReplyMustSendAccessDeniedEpitaphCompleter::Sync _completer) = 0;

    class MustSendAccessDeniedEpitaphCompleterBase : public _Base {
     public:
      void Reply(int32_t reply);
      void Reply(::fidl::BytePart _buffer, int32_t reply);
      void Reply(::fidl::DecodedMessage<MustSendAccessDeniedEpitaphResponse> params);

     protected:
      using ::fidl::CompleterBase::CompleterBase;
    };

    using MustSendAccessDeniedEpitaphCompleter = ::fidl::Completer<MustSendAccessDeniedEpitaphCompleterBase>;

    virtual void MustSendAccessDeniedEpitaph(MustSendAccessDeniedEpitaphCompleter::Sync _completer) = 0;

  };

  // Attempts to dispatch the incoming message to a handler function in the server implementation.
  // If there is no matching handler, it returns false, leaving the message and transaction intact.
  // In all other cases, it consumes the message and returns true.
  // It is possible to chain multiple TryDispatch functions in this manner.
  static bool TryDispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn);

  // Dispatches the incoming message to one of the handlers functions in the interface.
  // If there is no matching handler, it closes all the handles in |msg| and closes the channel with
  // a |ZX_ERR_NOT_SUPPORTED| epitaph, before returning false. The message should then be discarded.
  static bool Dispatch(Interface* impl, fidl_msg_t* msg, ::fidl::Transaction* txn);

  // Same as |Dispatch|, but takes a |void*| instead of |Interface*|. Only used with |fidl::Bind|
  // to reduce template expansion.
  // Do not call this method manually. Use |Dispatch| instead.
  static bool TypeErasedDispatch(void* impl, fidl_msg_t* msg, ::fidl::Transaction* txn) {
    return Dispatch(static_cast<Interface*>(impl), msg, txn);
  }

};

}  // namespace controlflow
}  // namespace llcpp
}  // namespace test
}  // namespace fidl

namespace fidl {

template <>
struct IsFidlType<::fidl::test::llcpp::controlflow::ControlFlow::MustSendAccessDeniedEpitaphResponse> : public std::true_type {};
template <>
struct IsFidlMessage<::fidl::test::llcpp::controlflow::ControlFlow::MustSendAccessDeniedEpitaphResponse> : public std::true_type {};
static_assert(sizeof(::fidl::test::llcpp::controlflow::ControlFlow::MustSendAccessDeniedEpitaphResponse)
    == ::fidl::test::llcpp::controlflow::ControlFlow::MustSendAccessDeniedEpitaphResponse::PrimarySize);
static_assert(offsetof(::fidl::test::llcpp::controlflow::ControlFlow::MustSendAccessDeniedEpitaphResponse, reply) == 16);

}  // namespace fidl
