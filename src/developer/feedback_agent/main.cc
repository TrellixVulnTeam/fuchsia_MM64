// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/feedback/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/logger.h>
#include <stdlib.h>

#include "src/developer/feedback_agent/feedback_agent.h"

int main(int argc, const char** argv) {
  syslog::InitLogger({"feedback_agent"});

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto context = sys::ComponentContext::Create();
  fuchsia::feedback::FeedbackAgent feedback_agent(loop.dispatcher(),
                                                  context->svc());
  fidl::BindingSet<fuchsia::feedback::DataProvider> bindings;
  context->outgoing()->AddPublicService(bindings.GetHandler(&feedback_agent));

  loop.Run();

  return EXIT_SUCCESS;
}
