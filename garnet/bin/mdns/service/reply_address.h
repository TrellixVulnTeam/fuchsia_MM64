// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MDNS_SERVICE_REPLY_ADDRESS_H_
#define GARNET_BIN_MDNS_SERVICE_REPLY_ADDRESS_H_

#include <arpa/inet.h>
#include <sys/socket.h>
#include <ostream>
#include "garnet/lib/inet/socket_address.h"
#include "src/lib/fxl/logging.h"

namespace mdns {

// SocketAddress with interface address.
class ReplyAddress {
 public:
  // Creates a reply address from an |SocketAddress| and an interface
  // |IpAddress|.
  ReplyAddress(const inet::SocketAddress& socket_address,
               const inet::IpAddress& interface_address);

  // Creates a reply address from an |sockaddr_storage| struct and an interface
  // |IpAddress|.
  ReplyAddress(const sockaddr_storage& socket_address,
               const inet::IpAddress& interface_address);

  const inet::SocketAddress& socket_address() const { return socket_address_; }

  const inet::IpAddress& interface_address() const {
    return interface_address_;
  }

  bool operator==(const ReplyAddress& other) const {
    return socket_address_ == other.socket_address() &&
           interface_address_ == other.interface_address();
  }

  bool operator!=(const ReplyAddress& other) const { return !(*this == other); }

 private:
  inet::SocketAddress socket_address_;
  inet::IpAddress interface_address_;
};

std::ostream& operator<<(std::ostream& os, const ReplyAddress& value);

}  // namespace mdns

#endif  // GARNET_BIN_MDNS_SERVICE_REPLY_ADDRESS_H_
