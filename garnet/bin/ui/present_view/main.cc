// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ui/app/cpp/fidl.h>
#include <fuchsia/ui/policy/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/svc/cpp/services.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/log_settings_command_line.h>
#include <src/lib/fxl/logging.h>

using fuchsia::ui::views::ViewConfig;
using scenic::ViewTokenPair;

namespace {

constexpr char kKeyLocale[] = "locale";

// Build a minimal |ViewConfig| using the given |locale_id|. This is needed for
// calls to |View::SetConfig|.
ViewConfig BuildSampleViewConfig(
    const std::string& locale_id,
    const std::string& timezone_id = "America/Los_Angeles",
    const std::string& calendar_id = "gregorian") {
  ViewConfig view_config;
  fuchsia::intl::Profile* intl_profile = view_config.mutable_intl_profile();
  intl_profile->mutable_locales()->push_back(
      fuchsia::intl::LocaleId{.id = locale_id});
  intl_profile->mutable_time_zones()->push_back(
      fuchsia::intl::TimeZoneId{.id = timezone_id});
  intl_profile->mutable_calendars()->push_back(
      fuchsia::intl::CalendarId{.id = calendar_id});
  intl_profile->set_temperature_unit(fuchsia::intl::TemperatureUnit::CELSIUS);
  return view_config;
}

}  // namespace

int main(int argc, const char** argv) {
  auto command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  if (!fxl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  FXL_LOG(WARNING) << "BE ADVISED: The present_view tool takes the URL to an "
                      "app that provided the ViewProvider interface and makes "
                      "its view the root view.";
  FXL_LOG(WARNING)
      << "This tool is intended for testing and debugging purposes "
         "only and may cause problems if invoked incorrectly.";
  FXL_LOG(WARNING)
      << "Do not invoke present_view if a view tree already exists "
         "(i.e. if any process that creates a view is already "
         "running).";
  FXL_LOG(WARNING)
      << "If scenic is already running on your system you "
         "will probably want to kill it before invoking this tool.";

  const auto& positional_args = command_line.positional_args();
  if (positional_args.empty()) {
    FXL_LOG(ERROR)
        << "present_view requires the url of a view provider application "
           "to present_view.";
    return 1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  auto startup_context_ = component::StartupContext::CreateFromStartupInfo();

  // Launch application.
  component::Services services;
  fuchsia::sys::LaunchInfo launch_info;
  launch_info.url = positional_args[0];
  for (size_t i = 1; i < positional_args.size(); ++i)
    launch_info.arguments.push_back(positional_args[i]);
  launch_info.directory_request = services.NewRequest();
  fuchsia::sys::ComponentControllerPtr controller;
  startup_context_->launcher()->CreateComponent(std::move(launch_info),
                                                controller.NewRequest());
  controller.set_error_handler([&loop](zx_status_t status) {
    FXL_LOG(INFO) << "Launched application terminated.";
    loop.Quit();
  });

  auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

  // Note: This instance must be retained for the lifetime of the UI, so it has
  // to be declared in the outer scope of |main| rather than inside the relevant
  // |if| branch.
  fidl::InterfacePtr<fuchsia::ui::views::View> view;
  // For now, use the presence of a locale option as an indication to use the
  // |fuchsia::ui::view::View| interface.
  if (command_line.HasOption(kKeyLocale)) {
    std::string locale_str;
    command_line.GetOptionValue(kKeyLocale, &locale_str);
    auto view_config = BuildSampleViewConfig(locale_str);

    // Create a view using the |fuchsia::ui::views::View| interface.
    services.ConnectToService<fuchsia::ui::views::View>(view.NewRequest());
    view->Present(std::move(view_token), std::move(view_config));
  } else {
    // Create the view using the |fuchsia::ui::app::ViewProvider| interface.
    fidl::InterfacePtr<::fuchsia::ui::app::ViewProvider> view_provider;
    services.ConnectToService(view_provider.NewRequest());
    view_provider->CreateView(std::move(view_token.value), nullptr, nullptr);
  }

  // Ask the presenter to display it.
  auto presenter =
      startup_context_
          ->ConnectToEnvironmentService<fuchsia::ui::policy::Presenter>();
  presenter->PresentView(std::move(view_holder_token), nullptr);

  // Done!
  loop.Run();
  return 0;
}
