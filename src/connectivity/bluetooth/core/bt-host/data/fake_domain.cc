// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fake_domain.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "src/connectivity/bluetooth/core/bt-host/l2cap/fake_channel.h"

namespace bt {

using l2cap::testing::FakeChannel;

namespace data {
namespace testing {

void FakeDomain::TriggerLEConnectionParameterUpdate(
    hci::ConnectionHandle handle,
    const hci::LEPreferredConnectionParameters& params) {
  ZX_DEBUG_ASSERT(initialized_);

  LinkData& link_data = ConnectedLinkData(handle);
  async::PostTask(
      link_data.dispatcher,
      [params, cb = link_data.le_conn_param_cb.share()] { cb(params); });
}

void FakeDomain::ExpectOutboundL2capChannel(hci::ConnectionHandle handle,
                                            l2cap::PSM psm, l2cap::ChannelId id,
                                            l2cap::ChannelId remote_id) {
  ZX_DEBUG_ASSERT(initialized_);
  LinkData& link_data = GetLinkData(handle);
  link_data.expected_outbound_conns[psm].emplace(id, remote_id);
}

void FakeDomain::TriggerInboundL2capChannel(hci::ConnectionHandle handle,
                                            l2cap::PSM psm, l2cap::ChannelId id,
                                            l2cap::ChannelId remote_id) {
  ZX_DEBUG_ASSERT(initialized_);

  LinkData& link_data = ConnectedLinkData(handle);
  auto cb_iter = inbound_conn_cbs_.find(psm);
  ZX_DEBUG_ASSERT_MSG(cb_iter != inbound_conn_cbs_.end(),
                      "no service registered for PSM %#.4x", psm);

  l2cap::ChannelCallback& cb = cb_iter->second.first;
  async_dispatcher_t* const dispatcher = cb_iter->second.second;
  auto chan = OpenFakeChannel(&link_data, id, remote_id);
  async::PostTask(dispatcher, [cb = cb.share(), chan = std::move(chan)] {
    cb(std::move(chan));
  });
}

void FakeDomain::TriggerLinkError(hci::ConnectionHandle handle) {
  ZX_DEBUG_ASSERT(initialized_);

  LinkData& link_data = ConnectedLinkData(handle);
  async::PostTask(link_data.dispatcher,
                  [cb = link_data.link_error_cb.share()] { cb(); });
}

void FakeDomain::Initialize() { initialized_ = true; }

void FakeDomain::ShutDown() { initialized_ = false; }

void FakeDomain::AddACLConnection(hci::ConnectionHandle handle,
                                  hci::Connection::Role role,
                                  l2cap::LinkErrorCallback link_error_cb,
                                  l2cap::SecurityUpgradeCallback security_cb,
                                  async_dispatcher_t* dispatcher) {
  if (!initialized_)
    return;

  RegisterInternal(handle, role, hci::Connection::LinkType::kACL,
                   std::move(link_error_cb), dispatcher);
}

void FakeDomain::AddLEConnection(
    hci::ConnectionHandle handle, hci::Connection::Role role,
    l2cap::LinkErrorCallback link_error_cb,
    l2cap::LEConnectionParameterUpdateCallback conn_param_cb,
    l2cap::LEFixedChannelsCallback channel_cb,
    l2cap::SecurityUpgradeCallback security_cb,
    async_dispatcher_t* dispatcher) {
  if (!initialized_)
    return;

  LinkData* data =
      RegisterInternal(handle, role, hci::Connection::LinkType::kLE,
                       std::move(link_error_cb), dispatcher);
  data->le_conn_param_cb = std::move(conn_param_cb);

  // Open the ATT and SMP fixed channels.
  auto att = OpenFakeFixedChannel(data, l2cap::kATTChannelId);
  auto smp = OpenFakeFixedChannel(data, l2cap::kLESMPChannelId);
  async::PostTask(dispatcher, [att = std::move(att), smp = std::move(smp),
                               cb = std::move(channel_cb)]() mutable {
    cb(std::move(att), std::move(smp));
  });
}

void FakeDomain::RemoveConnection(hci::ConnectionHandle handle) {
  links_.erase(handle);
}

void FakeDomain::AssignLinkSecurityProperties(hci::ConnectionHandle handle,
                                              sm::SecurityProperties security) {
  // TODO(armansito): implement
}

void FakeDomain::OpenL2capChannel(hci::ConnectionHandle handle, l2cap::PSM psm,
                                  l2cap::ChannelCallback cb,
                                  async_dispatcher_t* dispatcher) {
  ZX_DEBUG_ASSERT(initialized_);

  LinkData& link_data = ConnectedLinkData(handle);
  auto psm_it = link_data.expected_outbound_conns.find(psm);

  ZX_DEBUG_ASSERT_MSG(psm_it != link_data.expected_outbound_conns.end() &&
                          !psm_it->second.empty(),
                      "Unexpected outgoing L2CAP connection (PSM %#.4x)", psm);

  auto [id, remote_id] = psm_it->second.front();
  psm_it->second.pop();

  auto chan = OpenFakeChannel(&link_data, id, remote_id);

  async::PostTask(dispatcher, [cb = std::move(cb), chan = std::move(chan)]() {
    cb(std::move(chan));
  });
}

void FakeDomain::OpenL2capChannel(hci::ConnectionHandle handle, l2cap::PSM psm,
                                  SocketCallback socket_callback,
                                  async_dispatcher_t* cb_dispatcher) {
  ZX_DEBUG_ASSERT(cb_dispatcher);
  OpenL2capChannel(
      handle, psm,
      [this, cb = std::move(socket_callback),
       cb_dispatcher](auto channel) mutable {
        zx::socket s = socket_factory_.MakeSocketForChannel(channel);
        // Called every time the service is connected, cb must be shared.
        async::PostTask(cb_dispatcher,
                        [s = std::move(s), cb = cb.share(),
                         handle = channel->link_handle()]() mutable {
                          cb(std::move(s), handle);
                        });
      },
      async_get_default_dispatcher());
}

void FakeDomain::RegisterService(l2cap::PSM psm,
                                 l2cap::ChannelCallback channel_callback,
                                 async_dispatcher_t* dispatcher) {
  ZX_DEBUG_ASSERT(initialized_);
  ZX_DEBUG_ASSERT(inbound_conn_cbs_.count(psm) == 0);

  inbound_conn_cbs_.emplace(
      psm, std::make_pair(std::move(channel_callback), dispatcher));
}

void FakeDomain::RegisterService(l2cap::PSM psm, SocketCallback socket_callback,
                                 async_dispatcher_t* cb_dispatcher) {
  RegisterService(
      psm,
      [this, psm, cb = std::move(socket_callback),
       cb_dispatcher](auto channel) mutable {
        zx::socket s = socket_factory_.MakeSocketForChannel(channel);
        // Called every time the service is connected, cb must be shared.
        async::PostTask(cb_dispatcher,
                        [s = std::move(s), cb = cb.share(),
                         handle = channel->link_handle()]() mutable {
                          cb(std::move(s), handle);
                        });
      },
      async_get_default_dispatcher());
}

void FakeDomain::UnregisterService(l2cap::PSM psm) {
  ZX_DEBUG_ASSERT(initialized_);

  inbound_conn_cbs_.erase(psm);
}

FakeDomain::LinkData* FakeDomain::RegisterInternal(
    hci::ConnectionHandle handle, hci::Connection::Role role,
    hci::Connection::LinkType link_type, l2cap::LinkErrorCallback link_error_cb,
    async_dispatcher_t* dispatcher) {
  auto& data = GetLinkData(handle);
  ZX_DEBUG_ASSERT_MSG(!data.connected,
                      "connection handle re-used (handle: %#.4x)", handle);

  data.connected = true;
  data.role = role;
  data.type = link_type;
  data.link_error_cb = std::move(link_error_cb);
  data.dispatcher = dispatcher;

  return &data;
}

fbl::RefPtr<FakeChannel> FakeDomain::OpenFakeChannel(
    LinkData* link, l2cap::ChannelId id, l2cap::ChannelId remote_id) {
  auto chan =
      fbl::AdoptRef(new FakeChannel(id, remote_id, link->handle, link->type));
  chan->SetLinkErrorCallback(link->link_error_cb.share(), link->dispatcher);

  if (chan_cb_) {
    chan_cb_(chan);
  }

  return chan;
}

fbl::RefPtr<FakeChannel> FakeDomain::OpenFakeFixedChannel(LinkData* link,
                                                          l2cap::ChannelId id) {
  return OpenFakeChannel(link, id, id);
}

FakeDomain::LinkData& FakeDomain::GetLinkData(hci::ConnectionHandle handle) {
  auto [it, inserted] = links_.try_emplace(handle);
  auto& data = it->second;
  if (inserted) {
    data.connected = false;
    data.handle = handle;
  }
  return data;
}

FakeDomain::LinkData& FakeDomain::ConnectedLinkData(
    hci::ConnectionHandle handle) {
  auto link_iter = links_.find(handle);
  ZX_DEBUG_ASSERT_MSG(link_iter != links_.end(),
                      "fake link not found (handle: %#.4x)", handle);
  ZX_DEBUG_ASSERT_MSG(link_iter->second.connected,
                      "fake link not connected yet (handle: %#.4x)", handle);
  return link_iter->second;
}

}  // namespace testing
}  // namespace data
}  // namespace bt
