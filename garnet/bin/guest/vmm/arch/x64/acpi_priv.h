// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_ARCH_X64_ACPI_PRIV_H_
#define GARNET_BIN_GUEST_VMM_ARCH_X64_ACPI_PRIV_H_

#include <acpica/acpi.h>
#include <acpica/actypes.h>

/* PM register addresses. */
#define PM1A_REGISTER_STATUS 0
#define PM1A_REGISTER_ENABLE (ACPI_PM1_REGISTER_WIDTH / 8)

#endif  // GARNET_BIN_GUEST_VMM_ARCH_X64_ACPI_PRIV_H_
