// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mdns/service/mdns_fidl_util.h"

#include "lib/fidl/cpp/type_converter.h"
#include "lib/fsl/types/type_converters.h"
#include "src/lib/fxl/logging.h"

namespace mdns {

// static
const std::string MdnsFidlUtil::kFuchsiaServiceName = "_fuchsia._tcp.";

// static
fuchsia::mdns::ServiceInstancePtr MdnsFidlUtil::CreateServiceInstance(
    const std::string& service_name, const std::string& instance_name,
    const inet::SocketAddress& v4_address,
    const inet::SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  fuchsia::mdns::ServiceInstancePtr service_instance =
      fuchsia::mdns::ServiceInstance::New();

  service_instance->service_name = service_name;
  service_instance->instance_name = instance_name;
  service_instance->text = fidl::To<fidl::VectorPtr<std::string>>(text);

  if (v4_address.is_valid()) {
    service_instance->v4_address = CreateSocketAddressIPv4(v4_address);
  }

  if (v6_address.is_valid()) {
    service_instance->v6_address = CreateSocketAddressIPv6(v6_address);
  }

  return service_instance;
}

// static
void MdnsFidlUtil::UpdateServiceInstance(
    const fuchsia::mdns::ServiceInstancePtr& service_instance,
    const inet::SocketAddress& v4_address,
    const inet::SocketAddress& v6_address,
    const std::vector<std::string>& text) {
  service_instance->text = fidl::To<fidl::VectorPtr<std::string>>(text);

  if (v4_address.is_valid()) {
    service_instance->v4_address = CreateSocketAddressIPv4(v4_address);
  } else {
    service_instance->v4_address.reset();
  }

  if (v6_address.is_valid()) {
    service_instance->v6_address = CreateSocketAddressIPv6(v6_address);
  } else {
    service_instance->v6_address.reset();
  }
}

// static
fuchsia::netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv4(
    const inet::IpAddress& ip_address) {
  if (!ip_address) {
    return nullptr;
  }

  FXL_DCHECK(ip_address.is_v4());

  fuchsia::net::Ipv4Address ipv4;
  FXL_DCHECK(ipv4.addr.size() == ip_address.byte_count());
  std::memcpy(ipv4.addr.data(), ip_address.as_bytes(), ipv4.addr.size());

  fuchsia::netstack::SocketAddressPtr result =
      fuchsia::netstack::SocketAddress::New();
  result->addr.set_ipv4(ipv4);

  return result;
}

// static
fuchsia::netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv6(
    const inet::IpAddress& ip_address) {
  if (!ip_address) {
    return nullptr;
  }

  FXL_DCHECK(ip_address.is_v6());

  fuchsia::net::Ipv6Address ipv6;
  FXL_DCHECK(ipv6.addr.size() == ip_address.byte_count());
  std::memcpy(ipv6.addr.data(), ip_address.as_bytes(), ipv6.addr.size());

  fuchsia::netstack::SocketAddressPtr result =
      fuchsia::netstack::SocketAddress::New();
  result->addr.set_ipv6(ipv6);

  return result;
}

// static
fuchsia::netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv4(
    const inet::SocketAddress& socket_address) {
  if (!socket_address) {
    return nullptr;
  }

  FXL_DCHECK(socket_address.is_v4());

  fuchsia::netstack::SocketAddressPtr result =
      CreateSocketAddressIPv4(socket_address.address());

  result->port = socket_address.port().as_uint16_t();

  return result;
}

// static
fuchsia::netstack::SocketAddressPtr MdnsFidlUtil::CreateSocketAddressIPv6(
    const inet::SocketAddress& socket_address) {
  if (!socket_address) {
    return nullptr;
  }

  FXL_DCHECK(socket_address.is_v6());

  fuchsia::netstack::SocketAddressPtr result =
      CreateSocketAddressIPv6(socket_address.address());

  result->port = socket_address.port().as_uint16_t();

  return result;
}

// static
inet::IpAddress MdnsFidlUtil::IpAddressFrom(
    const fuchsia::net::IpAddress* addr) {
  FXL_DCHECK(addr != nullptr);
  switch (addr->Which()) {
    case fuchsia::net::IpAddress::Tag::kIpv4:
      if (!addr->is_ipv4()) {
        return inet::IpAddress();
      }

      FXL_DCHECK(addr->ipv4().addr.size() == sizeof(in_addr));
      return inet::IpAddress(
          *reinterpret_cast<const in_addr*>(addr->ipv4().addr.data()));
    case fuchsia::net::IpAddress::Tag::kIpv6:
      if (!addr->is_ipv6()) {
        return inet::IpAddress();
      }

      FXL_DCHECK(addr->ipv6().addr.size() == sizeof(in6_addr));
      return inet::IpAddress(
          *reinterpret_cast<const in6_addr*>(addr->ipv6().addr.data()));
    default:
      return inet::IpAddress();
  }
}

// static
std::unique_ptr<Mdns::Publication> MdnsFidlUtil::Convert(
    const fuchsia::mdns::PublicationPtr& publication_ptr) {
  if (!publication_ptr) {
    return nullptr;
  }

  auto publication = Mdns::Publication::Create(
      inet::IpPort::From_uint16_t(publication_ptr->port),
      fidl::To<std::vector<std::string>>(publication_ptr->text));
  publication->ptr_ttl_seconds_ =
      zx::duration(publication_ptr->ptr_ttl).to_secs();
  publication->srv_ttl_seconds_ =
      zx::duration(publication_ptr->srv_ttl).to_secs();
  publication->txt_ttl_seconds_ =
      zx::duration(publication_ptr->txt_ttl).to_secs();

  return publication;
}

}  // namespace mdns
