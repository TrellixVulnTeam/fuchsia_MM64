// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/manager/environment_controller_impl.h"

#include <lib/fit/function.h>
#include <src/lib/fxl/logging.h>

#include "garnet/bin/guest/manager/guest_services.h"

EnvironmentControllerImpl::EnvironmentControllerImpl(
    uint32_t id, const std::string& label, component::StartupContext* context,
    fidl::InterfaceRequest<fuchsia::guest::EnvironmentController> request)
    : id_(id),
      label_(label),
      host_vsock_endpoint_(
          fit::bind_member(this, &EnvironmentControllerImpl::GetAcceptor)) {
  // Create environment.
  context->environment()->CreateNestedEnvironment(
      env_.NewRequest(), env_controller_.NewRequest(), label,
      /*additional_services=*/nullptr, {.inherit_parent_services = true});
  env_->GetLauncher(launcher_.NewRequest());
  zx::channel h1, h2;
  FXL_CHECK(zx::channel::create(0, &h1, &h2) == ZX_OK);
  context->environment()->GetDirectory(std::move(h1));

  AddBinding(std::move(request));
}

void EnvironmentControllerImpl::set_unbound_handler(
    fit::function<void()> handler) {
  bindings_.set_empty_set_handler(std::move(handler));
}

void EnvironmentControllerImpl::AddBinding(
    fidl::InterfaceRequest<EnvironmentController> request) {
  bindings_.AddBinding(this, std::move(request));
}

void EnvironmentControllerImpl::LaunchInstance(
    fuchsia::guest::LaunchInfo launch_info,
    fidl::InterfaceRequest<fuchsia::guest::InstanceController> controller,
    LaunchInstanceCallback callback) {
  component::Services services;
  fuchsia::sys::ComponentControllerPtr component_controller;
  fuchsia::sys::LaunchInfo info;
  info.url = launch_info.url;
  info.arguments = std::move(launch_info.args);
  info.directory_request = services.NewRequest();
  info.flat_namespace = std::move(launch_info.flat_namespace);
  std::string label =
      launch_info.label ? launch_info.label.get() : launch_info.url;
  auto guest_services = std::make_unique<GuestServices>(std::move(launch_info));
  info.additional_services = guest_services->ServeDirectory();
  launcher_->CreateComponent(std::move(info),
                             component_controller.NewRequest());
  services.ConnectToService(std::move(controller));

  // Setup guest endpoint.
  const uint32_t cid = next_guest_cid_++;
  fuchsia::guest::GuestVsockEndpointPtr guest_endpoint;
  services.ConnectToService(guest_endpoint.NewRequest());
  guest_endpoint.events().OnShutdown =
      fit::bind_member(this, &EnvironmentControllerImpl::OnVsockShutdown);
  auto endpoint = std::make_unique<GuestVsockEndpoint>(
      cid, std::move(guest_endpoint), &host_vsock_endpoint_);

  component_controller.set_error_handler(
      [this, cid](zx_status_t status) { guests_.erase(cid); });
  auto component = std::make_unique<GuestComponent>(
      label, std::move(endpoint), std::move(services),
      std::move(guest_services), std::move(component_controller));

  bool inserted;
  std::tie(std::ignore, inserted) = guests_.insert({cid, std::move(component)});
  if (!inserted) {
    FXL_LOG(ERROR) << "Failed to allocate guest endpoint on CID " << cid;
    callback(0);
    return;
  }

  callback(cid);
}

void EnvironmentControllerImpl::OnVsockShutdown(uint32_t src_cid,
                                                uint32_t src_port,
                                                uint32_t dst_cid,
                                                uint32_t dst_port) {
  if (src_cid == fuchsia::guest::HOST_CID) {
    host_vsock_endpoint_.OnShutdown(src_port);
  }
}

void EnvironmentControllerImpl::GetHostVsockEndpoint(
    fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> request) {
  host_vsock_endpoint_.AddBinding(std::move(request));
}

fidl::VectorPtr<fuchsia::guest::InstanceInfo>
EnvironmentControllerImpl::ListGuests() {
  fidl::VectorPtr<fuchsia::guest::InstanceInfo> infos =
      fidl::VectorPtr<fuchsia::guest::InstanceInfo>::New(0);
  for (const auto& it : guests_) {
    infos.push_back(fuchsia::guest::InstanceInfo{
        .cid = it.first,
        .label = it.second->label(),
    });
  }
  return infos;
}

void EnvironmentControllerImpl::ListInstances(ListInstancesCallback callback) {
  callback(ListGuests());
}

void EnvironmentControllerImpl::ConnectToInstance(
    uint32_t id,
    fidl::InterfaceRequest<fuchsia::guest::InstanceController> request) {
  const auto& it = guests_.find(id);
  if (it != guests_.end()) {
    it->second->ConnectToInstance(std::move(request));
  }
}

void EnvironmentControllerImpl::ConnectToBalloon(
    uint32_t id,
    fidl::InterfaceRequest<fuchsia::guest::BalloonController> request) {
  const auto& it = guests_.find(id);
  if (it != guests_.end()) {
    it->second->ConnectToBalloon(std::move(request));
  }
}

fuchsia::guest::GuestVsockAcceptor* EnvironmentControllerImpl::GetAcceptor(
    uint32_t cid) {
  const auto& it = guests_.find(cid);
  return it == guests_.end() ? nullptr : it->second->endpoint();
}
