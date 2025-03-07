// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_FIDL_HELPERS_H_
#define SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_FIDL_HELPERS_H_

#include <optional>

#include <fuchsia/bluetooth/control/cpp/fidl.h>
#include <fuchsia/bluetooth/cpp/fidl.h>
#include <fuchsia/bluetooth/gatt/cpp/fidl.h>
#include <fuchsia/bluetooth/host/cpp/fidl.h>
#include <fuchsia/bluetooth/le/cpp/fidl.h>

#include "lib/fidl/cpp/type_converter.h"
#include "lib/fidl/cpp/vector.h"

#include "src/connectivity/bluetooth/core/bt-host/common/byte_buffer.h"
#include "src/connectivity/bluetooth/core/bt-host/common/identifier.h"
#include "src/connectivity/bluetooth/core/bt-host/common/status.h"
#include "src/connectivity/bluetooth/core/bt-host/gap/adapter.h"
#include "src/connectivity/bluetooth/core/bt-host/gap/remote_device.h"

// Helpers for implementing the Bluetooth FIDL interfaces.

namespace bt {
namespace gap {

class DiscoveryFilter;

}  // namespace gap
}  // namespace bt

namespace bthost {
namespace fidl_helpers {

// TODO(BT-305): Temporary logic for converting between the stack identifier
// type (integer) and FIDL identifier type (string). Remove these once all FIDL
// interfaces have been converted to use integer IDs.
std::optional<bt::common::DeviceId> DeviceIdFromString(const std::string& id);

// Functions for generating a FIDL bluetooth::common::Status

fuchsia::bluetooth::ErrorCode HostErrorToFidl(bt::common::HostError host_error);

fuchsia::bluetooth::Status NewFidlError(
    fuchsia::bluetooth::ErrorCode error_code, std::string description);

template <typename ProtocolErrorCode>
fuchsia::bluetooth::Status StatusToFidl(
    const bt::common::Status<ProtocolErrorCode>& status, std::string msg = "") {
  fuchsia::bluetooth::Status fidl_status;
  if (status.is_success()) {
    return fidl_status;
  }

  auto error = fuchsia::bluetooth::Error::New();
  error->error_code = HostErrorToFidl(status.error());
  error->description = msg.empty() ? status.ToString() : std::move(msg);
  if (status.is_protocol_error()) {
    error->protocol_error_code = static_cast<uint32_t>(status.protocol_error());
  }

  fidl_status.error = std::move(error);
  return fidl_status;
}

// Functions that convert FIDL types to library objects
bt::sm::IOCapability IoCapabilityFromFidl(
    const fuchsia::bluetooth::control::InputCapabilityType,
    const fuchsia::bluetooth::control::OutputCapabilityType);

// Functions to construct FIDL control library objects from library objects.
fuchsia::bluetooth::control::AdapterInfo NewAdapterInfo(
    const bt::gap::Adapter& adapter);
fuchsia::bluetooth::control::RemoteDevice NewRemoteDevice(
    const bt::gap::RemoteDevice& device);
fuchsia::bluetooth::control::RemoteDevicePtr NewRemoteDevicePtr(
    const bt::gap::RemoteDevice& device);

// Functions to convert Host FIDL library objects.
bt::sm::PairingData PairingDataFromFidl(
    const fuchsia::bluetooth::host::LEData& data);
bt::common::UInt128 LocalKeyFromFidl(
    const fuchsia::bluetooth::host::LocalKey& key);
std::optional<bt::sm::LTK> BrEdrKeyFromFidl(
    const fuchsia::bluetooth::host::BREDRData& data);
fuchsia::bluetooth::host::BondingData NewBondingData(
    const bt::gap::Adapter& adapter, const bt::gap::RemoteDevice& device);

// Functions to construct FIDL LE library objects from library objects.
fuchsia::bluetooth::le::AdvertisingDataPtr NewAdvertisingData(
    const bt::common::ByteBuffer& advertising_data);
fuchsia::bluetooth::le::RemoteDevicePtr NewLERemoteDevice(
    const bt::gap::RemoteDevice& device);

// Validates the contents of a ScanFilter.
bool IsScanFilterValid(const fuchsia::bluetooth::le::ScanFilter& fidl_filter);

// Populates a library DiscoveryFilter based on a FIDL ScanFilter. Returns false
// if |fidl_filter| contains any malformed data and leaves |out_filter|
// unmodified.
bool PopulateDiscoveryFilter(
    const fuchsia::bluetooth::le::ScanFilter& fidl_filter,
    bt::gap::DiscoveryFilter* out_filter);

}  // namespace fidl_helpers
}  // namespace bthost

// fidl::TypeConverter specializations for common::ByteBuffer and friends.
template <>
struct fidl::TypeConverter<fidl::VectorPtr<uint8_t>, bt::common::ByteBuffer> {
  static fidl::VectorPtr<uint8_t> Convert(const bt::common::ByteBuffer& from);
};

#endif  // SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_FIDL_HELPERS_H_
