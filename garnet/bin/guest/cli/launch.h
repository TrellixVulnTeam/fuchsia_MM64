// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_CLI_LAUNCH_H_
#define GARNET_BIN_GUEST_CLI_LAUNCH_H_

#include <lib/async-loop/cpp/loop.h>
#include <lib/sys/cpp/component_context.h>

void handle_launch(int argc, const char* argv[], async::Loop* loop,
                   sys::ComponentContext* context);

#endif  // GARNET_BIN_GUEST_CLI_LAUNCH_H_
