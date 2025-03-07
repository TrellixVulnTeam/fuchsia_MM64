// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/connectivity/bluetooth/core/bt-host/sdp/client.h"

#include "gtest/gtest.h"

#include "src/connectivity/bluetooth/core/bt-host/common/test_helpers.h"
#include "src/connectivity/bluetooth/core/bt-host/l2cap/fake_channel.h"
#include "src/connectivity/bluetooth/core/bt-host/l2cap/fake_channel_test.h"
#include "src/connectivity/bluetooth/core/bt-host/sdp/service_record.h"

namespace bt {
namespace sdp {
namespace {

using TestingBase = bt::l2cap::testing::FakeChannelTest;
constexpr l2cap::ChannelId kTestChannelId = 0x0041;

using common::CreateStaticByteBuffer;
using common::LowerBits;
using common::UpperBits;

class SDP_ClientTest : public TestingBase {
 public:
  SDP_ClientTest() = default;
  ~SDP_ClientTest() = default;

 protected:
  void SetUp() override {
    ChannelOptions options(kTestChannelId);
    options.link_type = hci::Connection::LinkType::kACL;
    channel_ = CreateFakeChannel(options);
  }

  void TearDown() override { channel_ = nullptr; }

  fbl::RefPtr<l2cap::testing::FakeChannel> channel() { return channel_; }

 private:
  fbl::RefPtr<l2cap::testing::FakeChannel> channel_;
};

// Flower Path Test:
//  - sends correctly formatted request
//  - receives response in the callback
//  - receives kNotFound at the end of the callbacks
//  - closes SDP channel when client is deallocated
TEST_F(SDP_ClientTest, ConnectAndQuery) {
  {
    auto client = Client::Create(channel());

    EXPECT_TRUE(fake_chan()->activated());

    size_t cb_count = 0;
    auto result_cb = [&](auto status,
                         const std::map<AttributeId, DataElement> &attrs) {
      cb_count++;
      if (cb_count == 3) {
        EXPECT_FALSE(status);
        EXPECT_EQ(common::HostError::kNotFound, status.error());
        return true;
      }
      // All results should have the ServiceClassIdList.
      EXPECT_EQ(1u, attrs.count(kServiceClassIdList));
      // The first result has a kProtocolDescriptorList and the second has a
      // kBluetoothProfileDescriptorList
      if (cb_count == 1) {
        EXPECT_EQ(1u, attrs.count(kProtocolDescriptorList));
        EXPECT_EQ(0u, attrs.count(kBluetoothProfileDescriptorList));
      } else if (cb_count == 2) {
        EXPECT_EQ(0u, attrs.count(kProtocolDescriptorList));
        EXPECT_EQ(1u, attrs.count(kBluetoothProfileDescriptorList));
      }
      return true;
    };

    const auto kSearchExpectedParams = common::CreateStaticByteBuffer(
        // ServiceSearchPattern
        0x35, 0x03,        // Sequence uint8 3 bytes
        0x19, 0x11, 0x0B,  // UUID (kAudioSink)
        0xFF, 0xFF,        // MaxAttributeByteCount (no max)
        // Attribute ID list
        0x35, 0x09,        // Sequence uint8 9 bytes
        0x09, 0x00, 0x01,  // uint16_t (kServiceClassIdList)
        0x09, 0x00, 0x04,  // uint16_t (kProtocolDescriptorList)
        0x09, 0x00, 0x09,  // uint16_t (kBluetoothProfileDescriptorList)
        0x00               // No continuation state
    );

    uint32_t request_tid;
    bool success = false;

    fake_chan()->SetSendCallback(
        [&request_tid, &success, &kSearchExpectedParams](auto packet) {
          // First byte should be type.
          ASSERT_LE(3u, packet->size());
          ASSERT_EQ(kServiceSearchAttributeRequest, (*packet)[0]);
          ASSERT_EQ(kSearchExpectedParams, packet->view(5));
          request_tid = (*packet)[1] << 8 || (*packet)[2];
          success = true;
        },
        dispatcher());

    // Seartch for all A2DP sinks, get the:
    //  - Service Class ID list
    //  - Descriptor List
    //  - Bluetooth Profile Descriptor List
    client->ServiceSearchAttributes(
        {profile::kAudioSink},
        {kServiceClassIdList, kProtocolDescriptorList,
         kBluetoothProfileDescriptorList},
        result_cb, dispatcher());
    RunLoopUntilIdle();
    EXPECT_TRUE(success);

    // Receive the response
    // Record makes building the response easier.
    ServiceRecord rec;
    rec.AddProtocolDescriptor(ServiceRecord::kPrimaryProtocolList,
                              protocol::kL2CAP, DataElement(l2cap::kAVDTP));
    // The second element here indicates version 1.3 (specified in A2DP spec)
    rec.AddProtocolDescriptor(ServiceRecord::kPrimaryProtocolList,
                              protocol::kAVDTP, DataElement(uint16_t(0x0103)));
    rec.AddProfile(profile::kAudioSink, 1, 3);
    ServiceSearchAttributeResponse rsp;
    rsp.SetAttribute(0, kServiceClassIdList,
                     DataElement({DataElement(profile::kAudioSink)}));
    rsp.SetAttribute(0, kProtocolDescriptorList,
                     rec.GetAttribute(kProtocolDescriptorList).Clone());

    rsp.SetAttribute(1, kServiceClassIdList,
                     DataElement({DataElement(profile::kAudioSink)}));
    rsp.SetAttribute(1, kBluetoothProfileDescriptorList,
                     rec.GetAttribute(kBluetoothProfileDescriptorList).Clone());

    auto rsp_ptr = rsp.GetPDU(0xFFFF /* Max attribute bytes */, request_tid,
                              common::BufferView());
    fake_chan()->Receive(*rsp_ptr);

    RunLoopUntilIdle();

    EXPECT_EQ(3u, cb_count);
  }
  EXPECT_FALSE(fake_chan()->activated());
}

// Continuing response test:
//  - send correctly formatted request
//  - receives a response with a continuing response
//  - sends a second request to get the rest of the response
//  - receives the continued response
//  - responds with the results
//  - gives up when callback returns false
TEST_F(SDP_ClientTest, ContinuingResponseRequested) {
  auto client = Client::Create(channel());

  size_t cb_count = 0;
  auto result_cb = [&](auto status,
                       const std::map<AttributeId, DataElement> &attrs) {
    cb_count++;
    if (cb_count == 3) {
      EXPECT_FALSE(status);
      EXPECT_EQ(common::HostError::kNotFound, status.error());
      return true;
    }
    // All results should have the ServiceClassIdList.
    EXPECT_EQ(1u, attrs.count(kServiceClassIdList));
    EXPECT_EQ(1u, attrs.count(kProtocolDescriptorList));
    return true;
  };

  const auto kSearchExpectedParams = common::CreateStaticByteBuffer(
      // ServiceSearchPattern
      0x35, 0x03,        // Sequence uint8 3 bytes
      0x19, 0x11, 0x0B,  // UUID (kAudioSink)
      0xFF, 0xFF,        // MaxAttributeByteCount (no max)
      // Attribute ID list
      0x35, 0x06,        // Sequence uint8 6 bytes
      0x09, 0x00, 0x01,  // uint16_t (0x0001 = kServiceClassIdList)
      0x09, 0x00, 0x04   // uint16_t (0x0004 = kProtocolDescriptorList)
  );

  size_t requests_made = 0;

  // Record makes building the response easier.
  ServiceRecord rec;
  rec.AddProtocolDescriptor(ServiceRecord::kPrimaryProtocolList,
                            protocol::kL2CAP, DataElement(l2cap::kAVDTP));
  // The second element here indicates version 1.3 (specified in A2DP spec)
  rec.AddProtocolDescriptor(ServiceRecord::kPrimaryProtocolList,
                            protocol::kAVDTP, DataElement(uint16_t(0x0103)));
  rec.AddProfile(profile::kAudioSink, 1, 3);
  ServiceSearchAttributeResponse rsp;
  rsp.SetAttribute(0, kServiceClassIdList,
                   DataElement({DataElement(profile::kAudioSink)}));
  rsp.SetAttribute(0, kProtocolDescriptorList,
                   rec.GetAttribute(kProtocolDescriptorList).Clone());
  rsp.SetAttribute(1, kServiceClassIdList,
                   DataElement({DataElement(profile::kAudioSink)}));
  rsp.SetAttribute(1, kProtocolDescriptorList,
                   rec.GetAttribute(kProtocolDescriptorList).Clone());

  fake_chan()->SetSendCallback(
      [&](auto packet) {
        requests_made++;
        // First byte should be type.
        ASSERT_LE(5u, packet->size());
        ASSERT_EQ(kServiceSearchAttributeRequest, (*packet)[0]);
        uint16_t request_tid = (*packet)[1] << 8 || (*packet)[2];
        ASSERT_EQ(kSearchExpectedParams,
                  packet->view(5, kSearchExpectedParams.size()));
        // The stuff after the params is the continuation state.
        auto rsp_ptr =
            rsp.GetPDU(16 /* Max attribute bytes */, request_tid,
                       packet->view(5 + kSearchExpectedParams.size() + 1));
        fake_chan()->Receive(*rsp_ptr);
      },
      dispatcher());

  // Seartch for all A2DP sinks, get the:
  //  - Service Class ID list
  //  - Descriptor List
  //  - Bluetooth Profile Descriptor List
  client->ServiceSearchAttributes(
      {profile::kAudioSink}, {kServiceClassIdList, kProtocolDescriptorList},
      result_cb, dispatcher());
  RunLoopUntilIdle();
  EXPECT_EQ(3u, cb_count);
  EXPECT_EQ(4u, requests_made);
}

// No results test:
//  - send correctly formatted request
//  - receives response with no results
//  - callback with no results (kNotFound right away)
TEST_F(SDP_ClientTest, NoResults) {
  auto client = Client::Create(channel());

  size_t cb_count = 0;
  auto result_cb = [&](auto status,
                       const std::map<AttributeId, DataElement> &attrs) {
    cb_count++;
    EXPECT_FALSE(status);
    EXPECT_EQ(common::HostError::kNotFound, status.error());
    return true;
  };

  const auto kSearchExpectedParams = CreateStaticByteBuffer(
      // ServiceSearchPattern
      0x35, 0x03,        // Sequence uint8 3 bytes
      0x19, 0x11, 0x0B,  // UUID (kAudioSink)
      0xFF, 0xFF,        // MaxAttributeByteCount (no max)
      // Attribute ID list
      0x35, 0x06,        // Sequence uint8 6 bytes
      0x09, 0x00, 0x01,  // uint16_t (0x0001 = kServiceClassIdList)
      0x09, 0x00, 0x04,  // uint16_t (0x0004 = kProtocolDescriptorList)
      0x00               // No continuation state
  );

  uint32_t request_tid;
  bool success = false;

  fake_chan()->SetSendCallback(
      [&request_tid, &success, &kSearchExpectedParams](auto packet) {
        // First byte should be type.
        ASSERT_LE(3u, packet->size());
        ASSERT_EQ(kServiceSearchAttributeRequest, (*packet)[0]);
        ASSERT_EQ(kSearchExpectedParams, packet->view(5));
        request_tid = (*packet)[1] << 8 || (*packet)[2];
        success = true;
      },
      dispatcher());

  // Seartch for all A2DP sinks, get the:
  //  - Service Class ID list
  //  - Descriptor List
  //  - Bluetooth Profile Descriptor List
  client->ServiceSearchAttributes(
      {profile::kAudioSink}, {kServiceClassIdList, kProtocolDescriptorList},
      result_cb, dispatcher());
  RunLoopUntilIdle();
  EXPECT_TRUE(success);

  // Receive an empty response
  ServiceSearchAttributeResponse rsp;
  auto rsp_ptr = rsp.GetPDU(0xFFFF /* Max attribute bytes */, request_tid,
                            common::BufferView());
  fake_chan()->Receive(*rsp_ptr);

  RunLoopUntilIdle();

  EXPECT_EQ(1u, cb_count);
}

// Disconnect early test:
//  - send request
//  - remote end disconnects
//  - result should be called with kLinkDisconnected
TEST_F(SDP_ClientTest, Disconnected) {
  auto client = Client::Create(channel());

  size_t cb_count = 0;
  auto result_cb = [&](auto status,
                       const std::map<AttributeId, DataElement> &attrs) {
    cb_count++;
    EXPECT_FALSE(status);
    EXPECT_EQ(common::HostError::kLinkDisconnected, status.error());
    return true;
  };

  const auto kSearchExpectedParams = CreateStaticByteBuffer(
      // ServiceSearchPattern
      0x35, 0x03,        // Sequence uint8 3 bytes
      0x19, 0x11, 0x0B,  // UUID (kAudioSink)
      0xFF, 0xFF,        // MaxAttributeByteCount (no max)
      // Attribute ID list
      0x35, 0x06,        // Sequence uint8 6 bytes
      0x09, 0x00, 0x01,  // uint16_t (0x0001 = kServiceClassIdList)
      0x09, 0x00, 0x04,  // uint16_t (0x0004 = kProtocolDescriptorList)
      0x00               // No continuation state
  );

  bool requested = false;

  fake_chan()->SetSendCallback(
      [&](auto packet) {
        // First byte should be type.
        ASSERT_LE(3u, packet->size());
        ASSERT_EQ(kServiceSearchAttributeRequest, (*packet)[0]);
        ASSERT_EQ(kSearchExpectedParams, packet->view(5));
        requested = true;
      },
      dispatcher());

  // Seartch for all A2DP sinks, get the:
  //  - Service Class ID list
  //  - Descriptor List
  //  - Bluetooth Profile Descriptor List
  client->ServiceSearchAttributes(
      {profile::kAudioSink}, {kServiceClassIdList, kProtocolDescriptorList},
      result_cb, dispatcher());
  RunLoopUntilIdle();
  EXPECT_TRUE(requested);
  EXPECT_EQ(0u, cb_count);

  // Remote end closes the channel.
  fake_chan()->Close();

  RunLoopUntilIdle();

  EXPECT_EQ(1u, cb_count);
}

// Malformed reply test:
//  - remote end sends wrong packet type in response (dropped)
//  - remote end sends invalid response
//  - callback receives no response with a malformed packet error
TEST_F(SDP_ClientTest, InvalidResponse) {
  auto client = Client::Create(channel());

  size_t cb_count = 0;
  auto result_cb = [&](auto status,
                       const std::map<AttributeId, DataElement> &attrs) {
    cb_count++;
    EXPECT_FALSE(status);
    EXPECT_EQ(common::HostError::kPacketMalformed, status.error());
    return true;
  };

  const auto kSearchExpectedParams = CreateStaticByteBuffer(
      // ServiceSearchPattern
      0x35, 0x03,        // Sequence uint8 3 bytes
      0x19, 0x11, 0x0B,  // UUID (kAudioSink)
      0xFF, 0xFF,        // MaxAttributeByteCount (no max)
      // Attribute ID list
      0x35, 0x06,        // Sequence uint8 6 bytes
      0x09, 0x00, 0x01,  // uint16_t (0x0001 = kServiceClassIdList)
      0x09, 0x00, 0x04,  // uint16_t (0x0004 = kProtocolDescriptorList)
      0x00               // No continuation state
  );

  uint32_t request_tid;
  bool requested = false;

  fake_chan()->SetSendCallback(
      [&](auto packet) {
        // First byte should be type.
        ASSERT_LE(3u, packet->size());
        ASSERT_EQ(kServiceSearchAttributeRequest, (*packet)[0]);
        ASSERT_EQ(kSearchExpectedParams, packet->view(5));
        request_tid = (*packet)[1] << 8 || (*packet)[2];
        requested = true;
      },
      dispatcher());

  // Seartch for all A2DP sinks, get the:
  //  - Service Class ID list
  //  - Descriptor List
  //  - Bluetooth Profile Descriptor List
  client->ServiceSearchAttributes(
      {profile::kAudioSink}, {kServiceClassIdList, kProtocolDescriptorList},
      result_cb, dispatcher());
  RunLoopUntilIdle();
  EXPECT_TRUE(requested);
  EXPECT_EQ(0u, cb_count);

  // Remote end sends some unparseable stuff for the packet.
  fake_chan()->Receive(CreateStaticByteBuffer(0x07, UpperBits(request_tid),
                                              LowerBits(request_tid), 0x00,
                                              0x03, 0x05, 0x06, 0x07));

  RunLoopUntilIdle();

  EXPECT_EQ(1u, cb_count);
}

// Time out (or possibly dropped packets that were malformed)
TEST_F(SDP_ClientTest, Timeout) {
  constexpr uint32_t kTimeoutMs = 5000;
  auto client = Client::Create(channel());

  size_t cb_count = 0;
  auto result_cb = [&](auto status,
                       const std::map<AttributeId, DataElement> &attrs) {
    cb_count++;
    EXPECT_FALSE(status);
    EXPECT_EQ(common::HostError::kTimedOut, status.error());
    return true;
  };

  const auto kSearchExpectedParams = CreateStaticByteBuffer(
      // ServiceSearchPattern
      0x35, 0x03,        // Sequence uint8 3 bytes
      0x19, 0x11, 0x0B,  // UUID (kAudioSink)
      0xFF, 0xFF,        // MaxAttributeByteCount (no max)
      // Attribute ID list
      0x35, 0x06,        // Sequence uint8 6 bytes
      0x09, 0x00, 0x01,  // uint16_t (0x0001 = kServiceClassIdList)
      0x09, 0x00, 0x04,  // uint16_t (0x0004 = kProtocolDescriptorList)
      0x00               // No continuation state
  );

  bool requested = false;

  fake_chan()->SetSendCallback(
      [&](auto packet) {
        // First byte should be type.
        ASSERT_LE(3u, packet->size());
        ASSERT_EQ(kServiceSearchAttributeRequest, (*packet)[0]);
        ASSERT_EQ(kSearchExpectedParams, packet->view(5));
        requested = true;
      },
      dispatcher());

  // Seartch for all A2DP sinks, get the:
  //  - Service Class ID list
  //  - Descriptor List
  //  - Bluetooth Profile Descriptor List
  client->ServiceSearchAttributes(
      {profile::kAudioSink}, {kServiceClassIdList, kProtocolDescriptorList},
      result_cb, dispatcher());
  RunLoopUntilIdle();
  EXPECT_TRUE(requested);
  EXPECT_EQ(0u, cb_count);

  // Wait until the timeout happens
  RunLoopFor(zx::msec(kTimeoutMs + 1));

  EXPECT_EQ(1u, cb_count);
}

}  // namespace
}  // namespace sdp
}  // namespace bt
