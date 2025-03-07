// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_HCI_FAKE_LOCAL_ADDRESS_DELEGATE_H_
#define SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_HCI_FAKE_LOCAL_ADDRESS_DELEGATE_H_

#include "src/connectivity/bluetooth/core/bt-host/common/device_address.h"
#include "src/connectivity/bluetooth/core/bt-host/hci/local_address_delegate.h"

namespace bt {
namespace hci {

class FakeLocalAddressDelegate : public LocalAddressDelegate {
 public:
  FakeLocalAddressDelegate() = default;
  ~FakeLocalAddressDelegate() override = default;

  void EnsureLocalAddress(AddressCallback callback) override;

  // If set to true EnsureLocalAddress runs its callback asynchronously.
  void set_async(bool value) { async_ = value; }

  void set_local_address(const common::DeviceAddress& value) {
    local_address_ = value;
  }

 private:
  bool async_ = false;
  common::DeviceAddress local_address_;
};

}  // namespace hci
}  // namespace bt

#endif  // SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_HCI_FAKE_LOCAL_ADDRESS_DELEGATE_H_
