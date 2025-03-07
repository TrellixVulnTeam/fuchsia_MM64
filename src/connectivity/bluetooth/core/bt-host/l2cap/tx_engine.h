// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_L2CAP_TX_ENGINE_H_
#define SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_L2CAP_TX_ENGINE_H_

#include <lib/fit/function.h>
#include <zircon/assert.h>

#include "src/connectivity/bluetooth/core/bt-host/common/byte_buffer.h"
#include "src/connectivity/bluetooth/core/bt-host/l2cap/l2cap.h"

namespace bt {
namespace l2cap {
namespace internal {

// The interface between a Channel, and the module implementing the
// mode-specific transmit logic. The primary purposes of an TxEngine are a) to
// transform SDUs into PDUs, and b) to transmit/retransmit the PDUs at the
// appropriate time. See Bluetooth Core Spec v5.0, Volume 3, Part A, Sec 2.4,
// "Modes of Operation" for more information about the possible modes.
class TxEngine {
 public:
  // Type defining the callback that a TxEngine uses to deliver a PDU to lower
  // layers. The callee may assume that the ByteBufferPtr owns an instance of a
  // DynamicByteBuffer or SlabBuffer.
  using SendBasicFrameCallback = fit::function<void(common::ByteBufferPtr pdu)>;

  // Creates a transmit engine, which will invoke |send_basic_frame_callback|
  // when a PDU is ready for transmission. This callback may be invoked
  // synchronously from QueueSdu(), as well as asynchronously (e.g. when a
  // retransmission timer expires).
  //
  // NOTE: The user of this class must ensure that a synchronous invocation of
  // |send_basic_frame_callback| does not deadlock. E.g., the callback must not
  // attempt to lock the same mutex as the caller of QueueSdu().
  TxEngine(ChannelId channel_id, uint16_t tx_mtu,
           SendBasicFrameCallback send_basic_frame_callback)
      : channel_id_(channel_id),
        tx_mtu_(tx_mtu),
        send_basic_frame_callback_(std::move(send_basic_frame_callback)) {
    ZX_ASSERT(tx_mtu_);
  }
  virtual ~TxEngine() = default;

  // Queues an SDU for transmission, returning true on success.
  //
  // * As noted in the ctor documentation, this _may_ result in a synchronous
  //   invocation of |send_basic_frame_callback_|.
  // * It is presumed that the ByteBufferPtr owns an instance of a
  //   DynamicByteBuffer or SlabBuffer.
  virtual bool QueueSdu(common::ByteBufferPtr) = 0;

 protected:
  const ChannelId channel_id_;
  const uint16_t tx_mtu_;
  // Invoked when a PDU is ready for transmission.
  const SendBasicFrameCallback send_basic_frame_callback_;

 private:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(TxEngine);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace bt

#endif  // SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_L2CAP_TX_ENGINE_H_
