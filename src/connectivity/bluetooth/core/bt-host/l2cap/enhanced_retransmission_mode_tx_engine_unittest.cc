// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "enhanced_retransmission_mode_tx_engine.h"

#include "gtest/gtest.h"
#include "lib/gtest/test_loop_fixture.h"
#include "src/connectivity/bluetooth/core/bt-host/common/byte_buffer.h"
#include "src/connectivity/bluetooth/core/bt-host/common/test_helpers.h"
#include "src/connectivity/bluetooth/core/bt-host/l2cap/frame_headers.h"

namespace bt {
namespace l2cap {
namespace internal {
namespace {

constexpr ChannelId kTestChannelId = 0x0001;

using TxEngine = EnhancedRetransmissionModeTxEngine;

class L2CAP_EnhancedRetransmissionModeTxEngineTest
    : public ::gtest::TestLoopFixture {
 public:
  L2CAP_EnhancedRetransmissionModeTxEngineTest()
      : kDefaultPayload('h', 'e', 'l', 'l', 'o') {}

 protected:
  // The default values are provided for use by tests which don't depend on the
  // specific value a given parameter. This should make the tests easier to
  // read, because the reader can focus on only the non-defaulted parameter
  // values.
  static constexpr auto kDefaultMTU = bt::l2cap::kDefaultMTU;

  const common::StaticByteBuffer<5> kDefaultPayload;

  void VerifyIsReceiverReadyPollFrame(common::ByteBuffer* buf) {
    ASSERT_TRUE(buf);
    ASSERT_EQ(sizeof(SimpleSupervisoryFrame), buf->size());

    const auto sframe = buf->As<SimpleSupervisoryFrame>();
    EXPECT_EQ(SupervisoryFunction::ReceiverReady, sframe.function());
    EXPECT_TRUE(sframe.is_poll_request());
  }
};

TEST_F(L2CAP_EnhancedRetransmissionModeTxEngineTest,
       QueueSduTransmitsMinimalSizedSdu) {
  common::ByteBufferPtr last_pdu;
  size_t n_pdus = 0;
  auto tx_callback = [&](auto pdu) {
    ++n_pdus;
    last_pdu = std::move(pdu);
  };

  constexpr size_t kMtu = 10;
  const auto payload = common::CreateStaticByteBuffer(1);
  TxEngine(kTestChannelId, kMtu, tx_callback)
      .QueueSdu(std::make_unique<common::DynamicByteBuffer>(payload));
  EXPECT_EQ(1u, n_pdus);
  ASSERT_TRUE(last_pdu);

  // See Core Spec v5.0, Volume 3, Part A, Table 3.2.
  const auto expected_pdu =
      common::CreateStaticByteBuffer(0,   // Final Bit, TxSeq, MustBeZeroBit
                                     0,   // SAR bits, ReqSeq
                                     1);  // Payload
  EXPECT_TRUE(common::ContainersEqual(expected_pdu, *last_pdu));
}

TEST_F(L2CAP_EnhancedRetransmissionModeTxEngineTest,
       QueueSduTransmitsMaximalSizedSdu) {
  common::ByteBufferPtr last_pdu;
  size_t n_pdus = 0;
  auto tx_callback = [&](auto pdu) {
    ++n_pdus;
    last_pdu = std::move(pdu);
  };

  constexpr size_t kMtu = 1;
  const auto payload = common::CreateStaticByteBuffer(1);
  TxEngine(kTestChannelId, kMtu, tx_callback)
      .QueueSdu(std::make_unique<common::DynamicByteBuffer>(payload));
  EXPECT_EQ(1u, n_pdus);
  ASSERT_TRUE(last_pdu);

  // See Core Spec v5.0, Volume 3, Part A, Table 3.2.
  const auto expected_pdu =
      common::CreateStaticByteBuffer(0,   // Final Bit, TxSeq, MustBeZeroBit
                                     0,   // SAR bits, ReqSeq
                                     1);  // Payload
  EXPECT_TRUE(common::ContainersEqual(expected_pdu, *last_pdu));
}

TEST_F(L2CAP_EnhancedRetransmissionModeTxEngineTest,
       QueueSduSurvivesOversizedSdu) {
  // TODO(BT-440): Update this test when we add support for segmentation.
  constexpr size_t kMtu = 1;
  TxEngine(kTestChannelId, kMtu, [](auto pdu) {})
      .QueueSdu(std::make_unique<common::DynamicByteBuffer>(
          common::CreateStaticByteBuffer(1, 2)));
}

TEST_F(L2CAP_EnhancedRetransmissionModeTxEngineTest,
       QueueSduSurvivesZeroByteSdu) {
  TxEngine(kTestChannelId, kDefaultMTU, [](auto pdu) {
  }).QueueSdu(std::make_unique<common::DynamicByteBuffer>());
}

TEST_F(L2CAP_EnhancedRetransmissionModeTxEngineTest,
       QueueSduAdvancesSequenceNumber) {
  const auto payload = common::CreateStaticByteBuffer(1);
  common::ByteBufferPtr last_pdu;
  auto tx_callback = [&](auto pdu) { last_pdu = std::move(pdu); };
  TxEngine tx_engine(kTestChannelId, kDefaultMTU, tx_callback);

  {
    // See Core Spec v5.0, Volume 3, Part A, Table 3.2.
    const auto expected_pdu =
        common::CreateStaticByteBuffer(0,   // Final Bit, TxSeq, MustBeZeroBit
                                       0,   // SAR bits, ReqSeq
                                       1);  // Payload

    tx_engine.QueueSdu(std::make_unique<common::DynamicByteBuffer>(payload));
    ASSERT_TRUE(last_pdu);
    EXPECT_TRUE(common::ContainersEqual(expected_pdu, *last_pdu));
  }

  {
    // See Core Spec v5.0, Volume 3, Part A, Table 3.2.
    const auto expected_pdu = common::CreateStaticByteBuffer(
        1 << 1,  // Final Bit, TxSeq=1, MustBeZeroBit
        0,       // SAR bits, ReqSeq
        1);      // Payload
    tx_engine.QueueSdu(std::make_unique<common::DynamicByteBuffer>(payload));
    ASSERT_TRUE(last_pdu);
    EXPECT_TRUE(common::ContainersEqual(expected_pdu, *last_pdu));
  }

  {
    // See Core Spec v5.0, Volume 3, Part A, Table 3.2.
    const auto expected_pdu = common::CreateStaticByteBuffer(
        2 << 1,  // Final Bit, TxSeq=2, MustBeZeroBit
        0,       // SAR bits, ReqSeq
        1);      // Payload
    tx_engine.QueueSdu(std::make_unique<common::DynamicByteBuffer>(payload));
    ASSERT_TRUE(last_pdu);
    EXPECT_TRUE(common::ContainersEqual(expected_pdu, *last_pdu));
  }
}

TEST_F(L2CAP_EnhancedRetransmissionModeTxEngineTest,
       QueueSduRollsOverSequenceNumber) {
  const auto payload = common::CreateStaticByteBuffer(1);
  common::ByteBufferPtr last_pdu;
  auto tx_callback = [&](auto pdu) { last_pdu = std::move(pdu); };
  TxEngine tx_engine(kTestChannelId, kDefaultMTU, tx_callback);

  constexpr size_t kMaxSeq = 64;
  for (size_t i = 0; i < kMaxSeq; ++i) {
    tx_engine.QueueSdu(std::make_unique<common::DynamicByteBuffer>(payload));
  }

  // See Core Spec v5.0, Volume 3, Part A, Table 3.2.
  const auto expected_pdu = common::CreateStaticByteBuffer(
      0,   // Final Bit, TxSeq (rolls over from 63 to 0), MustBeZeroBit
      0,   // SAR bits, ReqSeq
      1);  // Payload
  last_pdu = nullptr;
  tx_engine.QueueSdu(std::make_unique<common::DynamicByteBuffer>(payload));
  ASSERT_TRUE(last_pdu);
  EXPECT_TRUE(common::ContainersEqual(expected_pdu, *last_pdu));
}

TEST_F(L2CAP_EnhancedRetransmissionModeTxEngineTest,
       EngineTransmitsReceiverReadyPollAfterTimeout) {
  common::ByteBufferPtr last_pdu;
  auto tx_callback = [&](auto pdu) { last_pdu = std::move(pdu); };
  TxEngine tx_engine(kTestChannelId, kDefaultMTU, tx_callback);

  tx_engine.QueueSdu(
      std::make_unique<common::DynamicByteBuffer>(kDefaultPayload));
  RunLoopUntilIdle();
  ASSERT_TRUE(last_pdu);
  last_pdu = nullptr;

  ASSERT_TRUE(RunLoopFor(zx::sec(2)));
  SCOPED_TRACE("");
  VerifyIsReceiverReadyPollFrame(last_pdu.get());
}

TEST_F(L2CAP_EnhancedRetransmissionModeTxEngineTest,
       EngineTransmitsReceiverReadyPollOnlyOnceAfterTimeout) {
  common::ByteBufferPtr last_pdu;
  auto tx_callback = [&](auto pdu) { last_pdu = std::move(pdu); };
  TxEngine tx_engine(kTestChannelId, kDefaultMTU, tx_callback);

  tx_engine.QueueSdu(
      std::make_unique<common::DynamicByteBuffer>(kDefaultPayload));
  RunLoopUntilIdle();
  ASSERT_TRUE(last_pdu);
  last_pdu = nullptr;

  ASSERT_TRUE(RunLoopFor(zx::sec(2)));
  SCOPED_TRACE("");
  RETURN_IF_FATAL(VerifyIsReceiverReadyPollFrame(last_pdu.get()));
  last_pdu = nullptr;

  EXPECT_FALSE(RunLoopFor(zx::sec(2)));  // No tasks were run.
  EXPECT_FALSE(last_pdu);
}

TEST_F(L2CAP_EnhancedRetransmissionModeTxEngineTest,
       EngineAdvancesReceiverReadyPollTimeoutOnNewTransmission) {
  common::ByteBufferPtr last_pdu;
  auto tx_callback = [&](auto pdu) { last_pdu = std::move(pdu); };
  TxEngine tx_engine(kTestChannelId, kDefaultMTU, tx_callback);

  tx_engine.QueueSdu(
      std::make_unique<common::DynamicByteBuffer>(kDefaultPayload));
  RunLoopUntilIdle();
  ASSERT_TRUE(last_pdu);
  last_pdu = nullptr;

  ASSERT_FALSE(RunLoopFor(zx::sec(1)));  // No events should fire.
  tx_engine.QueueSdu(
      std::make_unique<common::DynamicByteBuffer>(kDefaultPayload));
  RunLoopUntilIdle();
  last_pdu = nullptr;

  ASSERT_FALSE(RunLoopFor(zx::sec(1)));  // Original timeout should not fire.
  ASSERT_TRUE(RunLoopFor(zx::sec(1)));   // New timeout should fire.
  SCOPED_TRACE("");
  VerifyIsReceiverReadyPollFrame(last_pdu.get());
}

}  // namespace
}  // namespace internal
}  // namespace l2cap
}  // namespace bt
