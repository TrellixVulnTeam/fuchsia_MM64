// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <io-scheduler/stream.h>

namespace ioscheduler {

Stream::Stream(uint32_t id, uint32_t pri) : id_(id), priority_(pri) {}
Stream::~Stream() {}

void Stream::Close() {
    open_ = false;
}

} // namespace ioscheduler