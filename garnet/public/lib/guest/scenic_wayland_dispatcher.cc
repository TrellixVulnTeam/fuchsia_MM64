// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/guest/scenic_wayland_dispatcher.h>

#include <lib/fit/function.h>
#include <src/lib/fxl/logging.h>

static constexpr char kWaylandDispatcherPackage[] =
    "fuchsia-pkg://fuchsia.com/wayland_bridge#meta/wayland_bridge.cmx";

namespace guest {

void ScenicWaylandDispatcher::OnNewConnection(zx::channel channel) {
  GetOrStartBridge()->OnNewConnection(std::move(channel));
}

fuchsia::guest::WaylandDispatcher* ScenicWaylandDispatcher::GetOrStartBridge() {
  if (!dispatcher_) {
    // Launch the bridge process.
    component::Services services;
    fuchsia::sys::LaunchInfo launch_info{
        .url = kWaylandDispatcherPackage,
        .directory_request = services.NewRequest(),
    };
    ConnectToLauncher()->CreateComponent(std::move(launch_info), bridge_.NewRequest());
    // If we hit an error just close the bridge. It will get relaunched in
    // response to the next new connection.
    bridge_.set_error_handler(
        fit::bind_member(this, &ScenicWaylandDispatcher::Reset));
    dispatcher_.set_error_handler(
        fit::bind_member(this, &ScenicWaylandDispatcher::Reset));

    // Connect to the |WaylandDispatcher| FIDL interface and forward the
    // channel along.
    services.ConnectToService(dispatcher_.NewRequest());
    services.ConnectToService(view_producer_.NewRequest());
    view_producer_.events().OnNewView =
        fit::bind_member(this, &ScenicWaylandDispatcher::OnNewView);
  }

  return dispatcher_.get();
}

void ScenicWaylandDispatcher::Reset(zx_status_t status) {
  FXL_LOG(ERROR) << "Wayland bridge lost: " << status;
  if (bridge_) {
    bridge_.Unbind();
  }
  if (dispatcher_) {
    dispatcher_.Unbind();
  }
}

void ScenicWaylandDispatcher::OnNewView(
    fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> view) {
  listener_(std::move(view));
}

fuchsia::sys::LauncherPtr ScenicWaylandDispatcher::ConnectToLauncher() const {
  fuchsia::sys::LauncherPtr launcher;
  if (deprecated_context_) {
    deprecated_context_->ConnectToEnvironmentService(launcher.NewRequest());
  } else {
    context_->svc()->Connect(launcher.NewRequest());
  }
  return launcher;
}

};  // namespace guest
