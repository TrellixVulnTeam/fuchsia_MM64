// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/app_driver/cpp/app_driver.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <trace-provider/provider.h>

#include "peridot/examples/swap_cpp/module.h"

int main(int /*argc*/, const char** /*argv*/) {
  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  trace::TraceProvider trace_provider(loop.dispatcher());

  auto context = component::StartupContext::CreateFromStartupInfo();
  modular::AppDriver<modular_example::ModuleApp> driver(
      context->outgoing().deprecated_services(),
      std::make_unique<modular_example::ModuleApp>(
          context.get(),
          [](scenic::ViewContext view_context) {
            return new modular_example::ModuleView(std::move(view_context),
                                                   0xFF00FFFF);
          }),
      [&loop] { loop.Quit(); });

  loop.Run();
  return 0;
}
