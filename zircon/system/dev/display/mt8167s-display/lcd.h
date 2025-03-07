// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <unistd.h>
#include <zircon/compiler.h>
#include <fbl/unique_ptr.h>
#include <hwreg/mmio.h>
#include <ddktl/protocol/gpio.h>
#include <ddktl/protocol/dsiimpl.h>

namespace mt8167s_display {

class Lcd {
public:
    Lcd(const ddk::DsiImplProtocolClient* dsi, const ddk::GpioProtocolClient* gpio,
        uint8_t panel_type)
        : dsiimpl_(*dsi), gpio_(*gpio), panel_type_(panel_type) {}

    zx_status_t Init();
    zx_status_t Enable();
    zx_status_t Disable();

private:
    zx_status_t LoadInitTable(const uint8_t* buffer, size_t size);
    zx_status_t GetDisplayId(uint16_t& id);

    const ddk::DsiImplProtocolClient            dsiimpl_;
    const ddk::GpioProtocolClient               gpio_;
    uint8_t                                     panel_type_;
    bool                                        initialized_ = false;
    bool                                        enabled_ =false;
};

} // namespace mt8167s_display
