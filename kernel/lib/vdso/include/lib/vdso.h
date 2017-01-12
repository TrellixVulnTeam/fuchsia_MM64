// Copyright 2016 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <lib/rodso.h>
#include <magenta/vm_address_region_dispatcher.h>

class VDso : public RoDso {
public:
    VDso();
};
