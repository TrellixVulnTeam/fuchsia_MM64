// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_MDNS_INTERFACE_TRANSCEIVER_H_
#define GARNET_BIN_MDNS_SERVICE_MDNS_INTERFACE_TRANSCEIVER_H_

#include <lib/fit/function.h>

#include <memory>
#include <vector>

#include "garnet/bin/mdns/service/dns_message.h"
#include "garnet/bin/mdns/service/reply_address.h"
#include "garnet/lib/inet/ip_address.h"
#include "garnet/lib/inet/socket_address.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "src/lib/files/unique_fd.h"
#include "src/lib/fxl/macros.h"

namespace mdns {

// Handles mDNS communication for a single NIC. This class is abstract and has
// two concrete implementations providing family-specific behavior:
// |MdnsInterfaceTransceiverV4| and |MdnsInterfaceTransceiverV6|.
class MdnsInterfaceTransceiver {
 public:
  // Callback to deliver inbound messages with reply address.
  using InboundMessageCallback =
      fit::function<void(std::unique_ptr<DnsMessage>, const ReplyAddress&)>;

  // Creates the variant of |MdnsInterfaceTransceiver| appropriate for the
  // address family specified in |address|. |name| is the name of the interface,
  // and |index| is its index.
  static std::unique_ptr<MdnsInterfaceTransceiver> Create(
      inet::IpAddress address, const std::string& name, uint32_t index);

  virtual ~MdnsInterfaceTransceiver();

  const inet::IpAddress& address() const { return address_; }

  const std::string& name() const { return name_; }

  uint32_t index() const { return index_; }

  // Starts the interface transceiver.
  bool Start(inet::IpPort mdns_port, InboundMessageCallback callback);

  // Stops the interface transceiver.
  void Stop();

  // Sets an alternate address for the interface.
  void SetAlternateAddress(const inet::IpAddress& alternate_address);

  // Sends a message to the specified address. A V6 interface will send to
  // |MdnsAddresses::V6Multicast| if |reply_address| is
  // |MdnsAddresses::V4Multicast|. This method expects there to be at most two
  // address records per record vector and, if there are two, that they are
  // adjacent. The same constraints will apply when this method returns.
  void SendMessage(DnsMessage* message, const inet::SocketAddress& address);

  // Sends a message containing only an address resource for this interface.
  void SendAddress(const std::string& host_full_name);

  // Sends a message containing only an address resource for this interface with
  // zero ttl, indicating that the address is no longer valid.
  void SendAddressGoodbye(const std::string& host_full_name);

  // Writes log messages describing lifetime traffic.
  void LogTraffic();

 protected:
  static constexpr int kTimeToLive_ = 255;
  static constexpr size_t kMaxPacketSize = 1500;

  MdnsInterfaceTransceiver(inet::IpAddress address, const std::string& name,
                           uint32_t index);

  const fxl::UniqueFD& socket_fd() const { return socket_fd_; }
  inet::IpPort mdns_port() const { return mdns_port_; }

  virtual int SetOptionDisableMulticastLoop() = 0;
  virtual int SetOptionJoinMulticastGroup() = 0;
  virtual int SetOptionOutboundInterface() = 0;
  virtual int SetOptionUnicastTtl() = 0;
  virtual int SetOptionMulticastTtl() = 0;
  virtual int SetOptionFamilySpecific() = 0;
  virtual int Bind() = 0;
  virtual int SendTo(const void* buffer, size_t size,
                     const inet::SocketAddress& address) = 0;

 private:
  int SetOptionSharePort();

  void WaitForInbound();

  void InboundReady(zx_status_t status, uint32_t events);

  // Returns an address resource (A/AAAA) record with the given name and the
  // address contained in |alternate_address_|, which must be valid.
  std::shared_ptr<DnsResource> GetAddressResource(
      const std::string& host_full_name);

  // Returns an address resource (A/AAAA) record with the given name and the
  // address contained in |address_|, which must be valid.
  std::shared_ptr<DnsResource> GetAlternateAddressResource(
      const std::string& host_full_name);

  // Makes an address resource (A/AAAA) record with the given name and address.
  std::shared_ptr<DnsResource> MakeAddressResource(
      const std::string& host_full_name, const inet::IpAddress& address);

  // Fixes up the address records in the vector. This method expects there to
  // be at most two address records in the vector and, if there are two, that
  // they are adjacent. The same constraints will apply when this method
  // returns.
  void FixUpAddresses(std::vector<std::shared_ptr<DnsResource>>* resources);

  inet::IpAddress address_;
  inet::IpAddress alternate_address_;
  std::string name_;
  uint32_t index_;
  fxl::UniqueFD socket_fd_;
  fsl::FDWaiter fd_waiter_;
  std::vector<uint8_t> inbound_buffer_;
  std::vector<uint8_t> outbound_buffer_;
  inet::IpPort mdns_port_;
  InboundMessageCallback inbound_message_callback_;
  std::shared_ptr<DnsResource> address_resource_;
  std::shared_ptr<DnsResource> alternate_address_resource_;
  uint64_t messages_received_ = 0;
  uint64_t bytes_received_ = 0;
  uint64_t messages_sent_ = 0;
  uint64_t bytes_sent_ = 0;

  FXL_DISALLOW_COPY_AND_ASSIGN(MdnsInterfaceTransceiver);
};

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_MDNS_INTERFACE_TRANSCEIVER_H_
