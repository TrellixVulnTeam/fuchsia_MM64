// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/engine/gfx_command_applier.h"

#include <lib/async/default.h>
#include <lib/fostr/fidl/fuchsia/ui/gfx/formatting.h>
#include <trace/event.h>

#include "garnet/lib/ui/gfx/engine/hit_tester.h"
#include "garnet/lib/ui/gfx/engine/resource_linker.h"
#include "garnet/lib/ui/gfx/engine/session_handler.h"
#include "garnet/lib/ui/gfx/resources/buffer.h"
#include "garnet/lib/ui/gfx/resources/camera.h"
#include "garnet/lib/ui/gfx/resources/compositor/display_compositor.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer.h"
#include "garnet/lib/ui/gfx/resources/compositor/layer_stack.h"
#include "garnet/lib/ui/gfx/resources/image.h"
#include "garnet/lib/ui/gfx/resources/image_pipe.h"
#include "garnet/lib/ui/gfx/resources/image_pipe_handler.h"
#include "garnet/lib/ui/gfx/resources/lights/ambient_light.h"
#include "garnet/lib/ui/gfx/resources/lights/directional_light.h"
#include "garnet/lib/ui/gfx/resources/lights/point_light.h"
#include "garnet/lib/ui/gfx/resources/memory.h"
#include "garnet/lib/ui/gfx/resources/nodes/entity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/node.h"
#include "garnet/lib/ui/gfx/resources/nodes/opacity_node.h"
#include "garnet/lib/ui/gfx/resources/nodes/scene.h"
#include "garnet/lib/ui/gfx/resources/nodes/shape_node.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"
#include "garnet/lib/ui/gfx/resources/shapes/circle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/mesh_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/shapes/rounded_rectangle_shape.h"
#include "garnet/lib/ui/gfx/resources/snapshot/snapshotter.h"
#include "garnet/lib/ui/gfx/resources/stereo_camera.h"
#include "garnet/lib/ui/gfx/resources/variable.h"
#include "garnet/lib/ui/gfx/resources/view.h"
#include "garnet/lib/ui/gfx/resources/view_holder.h"
#include "garnet/lib/ui/gfx/swapchain/swapchain_factory.h"
#include "garnet/lib/ui/gfx/util/time.h"
#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "garnet/lib/ui/gfx/util/wrap.h"
#include "lib/escher/hmd/pose_buffer.h"
#include "lib/escher/renderer/batch_gpu_uploader.h"
#include "lib/escher/shape/mesh.h"
#include "lib/escher/shape/rounded_rect_factory.h"
#include "lib/escher/util/type_utils.h"

namespace scenic_impl {
namespace gfx {

namespace {

// Makes it convenient to check that a value is constant and of a specific type,
// or a variable.
// TODO: There should also be a convenient way of type-checking a variable;
// this will necessarily involve looking up the value in the ResourceMap.
constexpr std::array<fuchsia::ui::gfx::Value::Tag, 2> kFloatValueTypes{
    {fuchsia::ui::gfx::Value::Tag::kVector1,
     fuchsia::ui::gfx::Value::Tag::kVariableId}};

}  // anonymous namespace

bool GfxCommandApplier::AssertValueIsOfType(
    const fuchsia::ui::gfx::Value& value,
    const fuchsia::ui::gfx::Value::Tag* tags, size_t tag_count,
    Session* session) {
  FXL_DCHECK(tag_count > 0);
  for (size_t i = 0; i < tag_count; ++i) {
    if (value.Which() == tags[i]) {
      return true;
    }
  }
  std::ostringstream str;
  if (tag_count == 1) {
    str << ", which is not the expected type: " << tags[0] << ".";
  } else {
    str << ", which is not one of the expected types (" << tags[0];
    for (size_t i = 1; i < tag_count; ++i) {
      str << ", " << tags[i];
    }
    str << ").";
  }
  session->error_reporter()->ERROR()
      << "scenic_impl::gfx::Session: received value of type: " << value.Which()
      << str.str();
  return false;
}

bool GfxCommandApplier::ApplyCommand(Session* session,
                                     CommandContext* command_context,
                                     fuchsia::ui::gfx::Command command) {
  TRACE_DURATION("gfx", "GfxCommandApplier::ApplyCommand");

  switch (command.Which()) {
    case fuchsia::ui::gfx::Command::Tag::kCreateResource:
      return ApplyCreateResourceCmd(session, command_context,
                                    std::move(command.create_resource()));
    case fuchsia::ui::gfx::Command::Tag::kReleaseResource:
      return ApplyReleaseResourceCmd(session,
                                     std::move(command.release_resource()));
    case fuchsia::ui::gfx::Command::Tag::kExportResource:
      return ApplyExportResourceCmd(session,
                                    std::move(command.export_resource()));
    case fuchsia::ui::gfx::Command::Tag::kImportResource:
      return ApplyImportResourceCmd(session,
                                    std::move(command.import_resource()));
    case fuchsia::ui::gfx::Command::Tag::kSetImportFocus: {
      return false;
    }
    case fuchsia::ui::gfx::Command::Tag::kAddChild:
      return ApplyAddChildCmd(session, std::move(command.add_child()));
    case fuchsia::ui::gfx::Command::Tag::kAddPart:
      return ApplyAddPartCmd(session, std::move(command.add_part()));
    case fuchsia::ui::gfx::Command::Tag::kDetach:
      return ApplyDetachCmd(session, std::move(command.detach()));
    case fuchsia::ui::gfx::Command::Tag::kDetachChildren:
      return ApplyDetachChildrenCmd(session,
                                    std::move(command.detach_children()));
    case fuchsia::ui::gfx::Command::Tag::kSetTag:
      return ApplySetTagCmd(session, std::move(command.set_tag()));
    case fuchsia::ui::gfx::Command::Tag::kSetTranslation:
      return ApplySetTranslationCmd(session,
                                    std::move(command.set_translation()));
    case fuchsia::ui::gfx::Command::Tag::kSetScale:
      return ApplySetScaleCmd(session, std::move(command.set_scale()));
    case fuchsia::ui::gfx::Command::Tag::kSetRotation:
      return ApplySetRotationCmd(session, std::move(command.set_rotation()));
    case fuchsia::ui::gfx::Command::Tag::kSetAnchor:
      return ApplySetAnchorCmd(session, std::move(command.set_anchor()));
    case fuchsia::ui::gfx::Command::Tag::kSetSize:
      return ApplySetSizeCmd(session, std::move(command.set_size()));
    case fuchsia::ui::gfx::Command::Tag::kSetOpacity:
      return ApplySetOpacityCmd(session, command.set_opacity());
    case fuchsia::ui::gfx::Command::Tag::kSendSizeChangeHintHack:
      return ApplySendSizeChangeHintCmd(session,
                                        command.send_size_change_hint_hack());
    case fuchsia::ui::gfx::Command::Tag::kSetShape:
      return ApplySetShapeCmd(session, std::move(command.set_shape()));
    case fuchsia::ui::gfx::Command::Tag::kSetMaterial:
      return ApplySetMaterialCmd(session, std::move(command.set_material()));
    case fuchsia::ui::gfx::Command::Tag::kSetClip:
      return ApplySetClipCmd(session, std::move(command.set_clip()));
    case ::fuchsia::ui::gfx::Command::Tag::kSetClipPlanes:
      return ApplySetClipPlanesCmd(session,
                                   std::move(command.set_clip_planes()));
    case fuchsia::ui::gfx::Command::Tag::kSetHitTestBehavior:
      return ApplySetHitTestBehaviorCmd(
          session, std::move(command.set_hit_test_behavior()));
    case fuchsia::ui::gfx::Command::Tag::kSetViewProperties:
      return ApplySetViewPropertiesCmd(
          session, std::move(command.set_view_properties()));
    case fuchsia::ui::gfx::Command::Tag::kSetCamera:
      return ApplySetCameraCmd(session, std::move(command.set_camera()));
    case fuchsia::ui::gfx::Command::Tag::kSetCameraTransform:
      return ApplySetCameraTransformCmd(
          session, std::move(command.set_camera_transform()));
    case fuchsia::ui::gfx::Command::Tag::kSetCameraProjection:
      return ApplySetCameraProjectionCmd(
          session, std::move(command.set_camera_projection()));
    case fuchsia::ui::gfx::Command::Tag::kSetStereoCameraProjection:
      return ApplySetStereoCameraProjectionCmd(
          session, std::move(command.set_stereo_camera_projection()));
    case fuchsia::ui::gfx::Command::Tag::kSetCameraPoseBuffer:
      return ApplySetCameraPoseBufferCmd(
          session, std::move(command.set_camera_pose_buffer()));
    case fuchsia::ui::gfx::Command::Tag::kSetLightColor:
      return ApplySetLightColorCmd(session,
                                   std::move(command.set_light_color()));
    case fuchsia::ui::gfx::Command::Tag::kSetLightDirection:
      return ApplySetLightDirectionCmd(
          session, std::move(command.set_light_direction()));
    case fuchsia::ui::gfx::Command::Tag::kSetPointLightPosition:
      return ApplySetPointLightPositionCmd(
          session, std::move(command.set_point_light_position()));
    case fuchsia::ui::gfx::Command::Tag::kSetPointLightFalloff:
      return ApplySetPointLightFalloffCmd(
          session, std::move(command.set_point_light_falloff()));
    case fuchsia::ui::gfx::Command::Tag::kAddLight:
      return ApplyAddLightCmd(session, std::move(command.add_light()));
    case fuchsia::ui::gfx::Command::Tag::kScene_AddAmbientLight:
      return ApplySceneAddAmbientLightCmd(
          session, std::move(command.scene__add_ambient_light()));
    case fuchsia::ui::gfx::Command::Tag::kScene_AddDirectionalLight:
      return ApplySceneAddDirectionalLightCmd(
          session, std::move(command.scene__add_directional_light()));
    case fuchsia::ui::gfx::Command::Tag::kScene_AddPointLight:
      return ApplySceneAddPointLightCmd(
          session, std::move(command.scene__add_point_light()));
    case fuchsia::ui::gfx::Command::Tag::kDetachLight:
      return ApplyDetachLightCmd(session, std::move(command.detach_light()));
    case fuchsia::ui::gfx::Command::Tag::kDetachLights:
      return ApplyDetachLightsCmd(session, std::move(command.detach_lights()));
    case fuchsia::ui::gfx::Command::Tag::kSetTexture:
      return ApplySetTextureCmd(session, std::move(command.set_texture()));
    case fuchsia::ui::gfx::Command::Tag::kSetColor:
      return ApplySetColorCmd(session, std::move(command.set_color()));
    case fuchsia::ui::gfx::Command::Tag::kBindMeshBuffers:
      return ApplyBindMeshBuffersCmd(session,
                                     std::move(command.bind_mesh_buffers()));
    case fuchsia::ui::gfx::Command::Tag::kAddLayer:
      return ApplyAddLayerCmd(session, std::move(command.add_layer()));
    case fuchsia::ui::gfx::Command::Tag::kRemoveLayer:
      return ApplyRemoveLayerCmd(session, std::move(command.remove_layer()));
    case fuchsia::ui::gfx::Command::Tag::kRemoveAllLayers:
      return ApplyRemoveAllLayersCmd(session,
                                     std::move(command.remove_all_layers()));
    case fuchsia::ui::gfx::Command::Tag::kSetLayerStack:
      return ApplySetLayerStackCmd(session,
                                   std::move(command.set_layer_stack()));
    case fuchsia::ui::gfx::Command::Tag::kSetRenderer:
      return ApplySetRendererCmd(session, std::move(command.set_renderer()));
    case fuchsia::ui::gfx::Command::Tag::kSetRendererParam:
      return ApplySetRendererParamCmd(session,
                                      std::move(command.set_renderer_param()));
    case fuchsia::ui::gfx::Command::Tag::kSetEventMask:
      return ApplySetEventMaskCmd(session, std::move(command.set_event_mask()));
    case fuchsia::ui::gfx::Command::Tag::kSetLabel:
      return ApplySetLabelCmd(session, std::move(command.set_label()));
    case fuchsia::ui::gfx::Command::Tag::kSetDisableClipping:
      return ApplySetDisableClippingCmd(
          session, std::move(command.set_disable_clipping()));
    case fuchsia::ui::gfx::Command::Tag::kTakeSnapshotCmd:
      return ApplyTakeSnapshotCmdHACK(session,
                                      std::move(command.take_snapshot_cmd()));
    case fuchsia::ui::gfx::Command::Tag::kSetDisplayColorConversion:
      return ApplySetDisplayColorConversionCmd(
          session, std::move(command.set_display_color_conversion()));
    case fuchsia::ui::gfx::Command::Tag::Invalid:
      // FIDL validation should make this impossible.
      FXL_CHECK(false);
      return false;
  }
}

bool GfxCommandApplier::ApplyCreateResourceCmd(
    Session* session, CommandContext* command_context,
    fuchsia::ui::gfx::CreateResourceCmd command) {
  const ResourceId id = command.id;
  if (id == 0) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplyCreateResourceCmd(): invalid ID: "
        << command;
    return false;
  }

  switch (command.resource.Which()) {
    case fuchsia::ui::gfx::ResourceArgs::Tag::kMemory:
      return ApplyCreateMemory(session, id,
                               std::move(command.resource.memory()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kImage:
      return ApplyCreateImage(session, id, std::move(command.resource.image()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kImagePipe:
      return ApplyCreateImagePipe(session, id,
                                  std::move(command.resource.image_pipe()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kBuffer:
      return ApplyCreateBuffer(session, id,
                               std::move(command.resource.buffer()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kScene:
      return ApplyCreateScene(session, id, std::move(command.resource.scene()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kCamera:
      return ApplyCreateCamera(session, id,
                               std::move(command.resource.camera()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kStereoCamera:
      return ApplyCreateStereoCamera(
          session, id, std::move(command.resource.stereo_camera()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kRenderer:
      return ApplyCreateRenderer(session, id,
                                 std::move(command.resource.renderer()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kAmbientLight:
      return ApplyCreateAmbientLight(
          session, id, std::move(command.resource.ambient_light()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kDirectionalLight:
      return ApplyCreateDirectionalLight(
          session, id, std::move(command.resource.directional_light()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kPointLight:
      return ApplyCreatePointLight(session, id,
                                   std::move(command.resource.point_light()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kRectangle:
      return ApplyCreateRectangle(session, id,
                                  std::move(command.resource.rectangle()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kRoundedRectangle:
      return ApplyCreateRoundedRectangle(
          session, command_context, id,
          std::move(command.resource.rounded_rectangle()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kCircle:
      return ApplyCreateCircle(session, id,
                               std::move(command.resource.circle()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kMesh:
      return ApplyCreateMesh(session, id, std::move(command.resource.mesh()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kMaterial:
      return ApplyCreateMaterial(session, id,
                                 std::move(command.resource.material()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kView:
      return ApplyCreateView(
          session, id,
          fuchsia::ui::gfx::ViewArgs2({
              .token =
                  {
                      .value = std::move(command.resource.view().token),
                  },
              .debug_name = std::move(command.resource.view().debug_name),
          }));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kViewHolder:
      return ApplyCreateViewHolder(
          session, id,
          fuchsia::ui::gfx::ViewHolderArgs2({
              .token =
                  {
                      .value = std::move(command.resource.view_holder().token),
                  },
              .debug_name =
                  std::move(command.resource.view_holder().debug_name),
          }));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kView2:
      return ApplyCreateView(session, id, std::move(command.resource.view2()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kViewHolder2:
      return ApplyCreateViewHolder(session, id,
                                   std::move(command.resource.view_holder2()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kClipNode:
      return ApplyCreateClipNode(session, id,
                                 std::move(command.resource.clip_node()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kOpacityNode:
      return ApplyCreateOpacityNode(session, id,
                                    command.resource.opacity_node());
    case fuchsia::ui::gfx::ResourceArgs::Tag::kEntityNode:
      return ApplyCreateEntityNode(session, id,
                                   std::move(command.resource.entity_node()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kShapeNode:
      return ApplyCreateShapeNode(session, id,
                                  std::move(command.resource.shape_node()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kCompositor:
      return ApplyCreateCompositor(session, id,
                                   std::move(command.resource.compositor()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kDisplayCompositor:
      return ApplyCreateDisplayCompositor(
          session, id, std::move(command.resource.display_compositor()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kImagePipeCompositor:
      return ApplyCreateImagePipeCompositor(
          session, id, std::move(command.resource.image_pipe_compositor()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kLayerStack:
      return ApplyCreateLayerStack(session, id,
                                   std::move(command.resource.layer_stack()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kLayer:
      return ApplyCreateLayer(session, id, std::move(command.resource.layer()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::kVariable:
      return ApplyCreateVariable(session, id,
                                 std::move(command.resource.variable()));
    case fuchsia::ui::gfx::ResourceArgs::Tag::Invalid:
      // FIDL validation should make this impossible.
      FXL_CHECK(false);
      return false;
  }
}

bool GfxCommandApplier::ApplyReleaseResourceCmd(
    Session* session, fuchsia::ui::gfx::ReleaseResourceCmd command) {
  return session->resources()->RemoveResource(command.id);
}

bool GfxCommandApplier::ApplyExportResourceCmd(
    Session* session, fuchsia::ui::gfx::ExportResourceCmd command) {
  if (!command.token) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::ApplyExportResourceCmd(): "
           "no token provided.";
    return false;
  }
  if (auto resource =
          session->resources()->FindResource<Resource>(command.id)) {
    return session->session_context().resource_linker->ExportResource(
        resource.get(), std::move(command.token));
  }
  return false;
}

bool GfxCommandApplier::ApplyImportResourceCmd(
    Session* session, fuchsia::ui::gfx::ImportResourceCmd command) {
  if (!command.token) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::ApplyImportResourceCmd(): "
           "no token provided.";
    return false;
  }
  auto import = fxl::MakeRefCounted<Import>(
      session, command.id, command.spec,
      session->session_context().resource_linker->GetWeakPtr());
  return session->session_context().resource_linker->ImportResource(
             import.get(), command.spec, std::move(command.token)) &&
         session->resources()->AddResource(command.id, std::move(import));
}

bool GfxCommandApplier::ApplyAddChildCmd(
    Session* session, fuchsia::ui::gfx::AddChildCmd command) {
  // Find the parent and child nodes. We can add:
  // - Nodes to Nodes
  // - ViewHolders to Nodes
  // - Nodes to Views' ViewNodes
  // TODO(SCN-795): Split these out into separate commands? or just allow node
  // to handle these??
  auto child = session->resources()->FindResource<Node>(command.child_id);
  if (!child) {
    return false;
  }

  if (auto parent = session->resources()->FindResource<Node>(
          command.node_id, ResourceMap::ErrorBehavior::kDontReportErrors)) {
    return parent->AddChild(std::move(child));
  } else if (auto view = session->resources()->FindResource<View>(
                 command.node_id,
                 ResourceMap::ErrorBehavior::kDontReportErrors)) {
    // Children are added to a View. Add them the corresponding ViewNode.
    return view->GetViewNode()->AddChild(std::move(child));
  }
  session->error_reporter()->ERROR()
      << "No View or Node found with id " << command.node_id;
  return false;
}

bool GfxCommandApplier::ApplyAddPartCmd(Session* session,
                                        fuchsia::ui::gfx::AddPartCmd command) {
  // Find the parent and part nodes.
  if (auto parent_node =
          session->resources()->FindResource<Node>(command.node_id)) {
    if (auto part_node =
            session->resources()->FindResource<Node>(command.part_id)) {
      return parent_node->AddPart(std::move(part_node));
    }
  }
  return false;
}

bool GfxCommandApplier::ApplyTakeSnapshotCmdHACK(
    Session* session, fuchsia::ui::gfx::TakeSnapshotCmdHACK command) {
  async::PostTask(
      async_get_default_dispatcher(),
      [weak = session->GetWeakPtr(), command = std::move(command)]() mutable {
        if (!weak) {
          if (auto callback = command.callback.Bind()) {
            // TODO(SCN-978): Return an error to the caller for invalid data.
            callback->OnData(fuchsia::mem::Buffer{});
          }
          return;
        }

        const auto& context = weak->session_context();
        Resource* resource = nullptr;
        if (auto node =
                weak->resources()->FindResource<Node>(command.node_id)) {
          resource = node.get();
        } else if (command.node_id == 0) {
          // TODO(SCN-1170): get rid of SceneGraph::first_compositor().
          const auto& first_compositor_weak =
              context.scene_graph->first_compositor();
          if (first_compositor_weak) {
            resource = first_compositor_weak.get();
          }
        }

        if (resource == nullptr) {
          if (auto callback = command.callback.Bind()) {
            callback->OnData(fuchsia::mem::Buffer{});
          }
          return;
        }

        auto gpu_uploader =
            escher::BatchGpuUploader::New(context.escher->GetWeakPtr());
        Snapshotter snapshotter(std::move(gpu_uploader));
        // Take a snapshot and return the data in callback. The closure does
        // not need the snapshotter instance and is invoked after the instance
        // is destroyed.
        snapshotter.TakeSnapshot(resource, [callback = command.callback.Bind()](
                                               fuchsia::mem::Buffer snapshot) {
          callback->OnData(std::move(snapshot));
        });
      });
  return true;
}

bool GfxCommandApplier::ApplyDetachCmd(Session* session,
                                       fuchsia::ui::gfx::DetachCmd command) {
  if (auto resource =
          session->resources()->FindResource<Resource>(command.id)) {
    return resource->Detach();
  }
  return false;
}

bool GfxCommandApplier::ApplyDetachChildrenCmd(
    Session* session, fuchsia::ui::gfx::DetachChildrenCmd command) {
  if (auto node = session->resources()->FindResource<Node>(command.node_id)) {
    return node->DetachChildren();
  }
  return false;
}

bool GfxCommandApplier::ApplySetTagCmd(Session* session,
                                       fuchsia::ui::gfx::SetTagCmd command) {
  if (auto node = session->resources()->FindResource<Node>(command.node_id)) {
    return node->SetTagValue(command.tag_value);
  }
  return false;
}

bool GfxCommandApplier::ApplySetTranslationCmd(
    Session* session, fuchsia::ui::gfx::SetTranslationCmd command) {
  if (auto node = session->resources()->FindResource<Node>(command.id)) {
    if (IsVariable(command.value)) {
      if (auto variable =
              session->resources()->FindVariableResource<Vector3Variable>(
                  command.value.variable_id)) {
        return node->SetTranslation(variable);
      }
    } else {
      return node->SetTranslation(UnwrapVector3(command.value));
    }
  }
  return false;
}

bool GfxCommandApplier::ApplySetScaleCmd(
    Session* session, fuchsia::ui::gfx::SetScaleCmd command) {
  if (auto node = session->resources()->FindResource<Node>(command.id)) {
    if (IsVariable(command.value)) {
      if (auto variable =
              session->resources()->FindVariableResource<Vector3Variable>(
                  command.value.variable_id)) {
        return node->SetScale(variable);
      }
    } else {
      return node->SetScale(UnwrapVector3(command.value));
    }
  }
  return false;
}

bool GfxCommandApplier::ApplySetRotationCmd(
    Session* session, fuchsia::ui::gfx::SetRotationCmd command) {
  if (auto node = session->resources()->FindResource<Node>(command.id)) {
    if (IsVariable(command.value)) {
      if (auto variable =
              session->resources()->FindVariableResource<QuaternionVariable>(
                  command.value.variable_id)) {
        return node->SetRotation(variable);
      }
    } else {
      return node->SetRotation(UnwrapQuaternion(command.value));
    }
  }
  return false;
}

bool GfxCommandApplier::ApplySetAnchorCmd(
    Session* session, fuchsia::ui::gfx::SetAnchorCmd command) {
  if (auto node = session->resources()->FindResource<Node>(command.id)) {
    if (IsVariable(command.value)) {
      if (auto variable =
              session->resources()->FindVariableResource<Vector3Variable>(
                  command.value.variable_id)) {
        return node->SetAnchor(variable);
      }
    }
    return node->SetAnchor(UnwrapVector3(command.value));
  }
  return false;
}

bool GfxCommandApplier::ApplySetSizeCmd(Session* session,
                                        fuchsia::ui::gfx::SetSizeCmd command) {
  if (auto layer = session->resources()->FindResource<Layer>(command.id)) {
    if (IsVariable(command.value)) {
      session->error_reporter()->ERROR()
          << "scenic_impl::gfx::GfxCommandApplier::ApplySetSizeCmd(): "
             "unimplemented for variable value.";
      return false;
    }
    return layer->SetSize(UnwrapVector2(command.value));
  }
  return false;
}

bool GfxCommandApplier::ApplySetOpacityCmd(
    Session* session, fuchsia::ui::gfx::SetOpacityCmd command) {
  if (auto node =
          session->resources()->FindResource<OpacityNode>(command.node_id)) {
    node->SetOpacity(command.opacity);
    return true;
  }
  return false;
}

bool GfxCommandApplier::ApplySendSizeChangeHintCmd(
    Session* session, fuchsia::ui::gfx::SendSizeChangeHintCmdHACK command) {
  if (auto node = session->resources()->FindResource<Node>(command.node_id)) {
    return node->SendSizeChangeHint(command.width_change_factor,
                                    command.height_change_factor);
  }
  return false;
}

bool GfxCommandApplier::ApplySetShapeCmd(
    Session* session, fuchsia::ui::gfx::SetShapeCmd command) {
  if (auto node =
          session->resources()->FindResource<ShapeNode>(command.node_id)) {
    if (auto shape =
            session->resources()->FindResource<Shape>(command.shape_id)) {
      node->SetShape(std::move(shape));
      return true;
    }
  }
  return false;
}

bool GfxCommandApplier::ApplySetMaterialCmd(
    Session* session, fuchsia::ui::gfx::SetMaterialCmd command) {
  if (auto node =
          session->resources()->FindResource<ShapeNode>(command.node_id)) {
    if (auto material =
            session->resources()->FindResource<Material>(command.material_id)) {
      node->SetMaterial(std::move(material));
      return true;
    }
  }
  return false;
}

bool GfxCommandApplier::ApplySetClipCmd(Session* session,
                                        fuchsia::ui::gfx::SetClipCmd command) {
  if (command.clip_id != 0) {
    // TODO(SCN-167): Support non-zero clip_id.
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::ApplySetClipCmd(): only "
           "clip_to_self is implemented.";
    return false;
  }

  if (auto node = session->resources()->FindResource<Node>(command.node_id)) {
    return node->SetClipToSelf(command.clip_to_self);
  }

  return false;
}

bool GfxCommandApplier::ApplySetClipPlanesCmd(
    Session* session, fuchsia::ui::gfx::SetClipPlanesCmd command) {
  if (auto node = session->resources()->FindResource<Node>(command.node_id)) {
    std::vector<escher::plane3> clip_planes;
    clip_planes.reserve(command.clip_planes.size());
    for (auto& p : command.clip_planes) {
      clip_planes.emplace_back(Unwrap(p.dir), p.dist);
    }
    return node->SetClipPlanes(std::move(clip_planes));
  }

  return false;
}

bool GfxCommandApplier::ApplySetHitTestBehaviorCmd(
    Session* session, fuchsia::ui::gfx::SetHitTestBehaviorCmd command) {
  if (auto node = session->resources()->FindResource<Node>(command.node_id)) {
    return node->SetHitTestBehavior(command.hit_test_behavior);
  }

  return false;
}

bool GfxCommandApplier::ApplySetViewPropertiesCmd(
    Session* session, fuchsia::ui::gfx::SetViewPropertiesCmd command) {
  if (auto view_holder = session->resources()->FindResource<ViewHolder>(
          command.view_holder_id)) {
    view_holder->SetViewProperties(std::move(command.properties));
    return true;
  }
  return false;
}

bool GfxCommandApplier::ApplySetCameraCmd(
    Session* session, fuchsia::ui::gfx::SetCameraCmd command) {
  if (auto renderer =
          session->resources()->FindResource<Renderer>(command.renderer_id)) {
    if (command.camera_id == 0) {
      renderer->SetCamera(nullptr);
      return true;
    } else if (auto camera = session->resources()->FindResource<Camera>(
                   command.camera_id)) {
      renderer->SetCamera(std::move(camera));
      return true;
    }
  }
  return false;
}

bool GfxCommandApplier::ApplySetTextureCmd(
    Session* session, fuchsia::ui::gfx::SetTextureCmd command) {
  if (auto material =
          session->resources()->FindResource<Material>(command.material_id)) {
    if (command.texture_id == 0) {
      material->SetTexture(nullptr);
      return true;
    } else if (auto image = session->resources()->FindResource<ImageBase>(
                   command.texture_id)) {
      material->SetTexture(std::move(image));
      return true;
    }
  }
  return false;
}

bool GfxCommandApplier::ApplySetColorCmd(
    Session* session, fuchsia::ui::gfx::SetColorCmd command) {
  if (auto material =
          session->resources()->FindResource<Material>(command.material_id)) {
    if (IsVariable(command.color)) {
      session->error_reporter()->ERROR()
          << "scenic_impl::gfx::GfxCommandApplier::ApplySetColorCmd(): "
             "unimplemented for variable color.";
      return false;
    }

    auto& color = command.color.value;
    float red = static_cast<float>(color.red) / 255.f;
    float green = static_cast<float>(color.green) / 255.f;
    float blue = static_cast<float>(color.blue) / 255.f;
    float alpha = static_cast<float>(color.alpha) / 255.f;
    material->SetColor(red, green, blue, alpha);
    return true;
  }
  return false;
}

bool GfxCommandApplier::ApplyBindMeshBuffersCmd(
    Session* session, fuchsia::ui::gfx::BindMeshBuffersCmd command) {
  auto mesh = session->resources()->FindResource<MeshShape>(command.mesh_id);
  auto index_buffer =
      session->resources()->FindResource<Buffer>(command.index_buffer_id);
  auto vertex_buffer =
      session->resources()->FindResource<Buffer>(command.vertex_buffer_id);
  if (mesh && index_buffer && vertex_buffer) {
    return mesh->BindBuffers(std::move(index_buffer), command.index_format,
                             command.index_offset, command.index_count,
                             std::move(vertex_buffer), command.vertex_format,
                             command.vertex_offset, command.vertex_count,
                             Unwrap(command.bounding_box));
  }
  return false;
}

bool GfxCommandApplier::ApplyAddLayerCmd(
    Session* session, fuchsia::ui::gfx::AddLayerCmd command) {
  auto layer_stack =
      session->resources()->FindResource<LayerStack>(command.layer_stack_id);
  auto layer = session->resources()->FindResource<Layer>(command.layer_id);
  if (layer_stack && layer) {
    return layer_stack->AddLayer(std::move(layer));
  }
  return false;
}

bool GfxCommandApplier::ApplyRemoveLayerCmd(
    Session* session, fuchsia::ui::gfx::RemoveLayerCmd command) {
  auto layer_stack =
      session->resources()->FindResource<LayerStack>(command.layer_stack_id);
  auto layer = session->resources()->FindResource<Layer>(command.layer_id);
  if (layer_stack && layer) {
    return layer_stack->RemoveLayer(std::move(layer));
  }
  return false;
}

bool GfxCommandApplier::ApplyRemoveAllLayersCmd(
    Session* session, fuchsia::ui::gfx::RemoveAllLayersCmd command) {
  auto layer_stack =
      session->resources()->FindResource<LayerStack>(command.layer_stack_id);
  if (layer_stack) {
    return layer_stack->RemoveAllLayers();
  }
  return false;
}

bool GfxCommandApplier::ApplySetLayerStackCmd(
    Session* session, fuchsia::ui::gfx::SetLayerStackCmd command) {
  auto compositor =
      session->resources()->FindResource<Compositor>(command.compositor_id);
  auto layer_stack =
      session->resources()->FindResource<LayerStack>(command.layer_stack_id);
  if (compositor && layer_stack) {
    return compositor->SetLayerStack(std::move(layer_stack));
  }
  return false;
}

bool GfxCommandApplier::ApplySetRendererCmd(
    Session* session, fuchsia::ui::gfx::SetRendererCmd command) {
  auto layer = session->resources()->FindResource<Layer>(command.layer_id);
  auto renderer =
      session->resources()->FindResource<Renderer>(command.renderer_id);

  if (layer && renderer) {
    return layer->SetRenderer(std::move(renderer));
  }
  return false;
}

bool GfxCommandApplier::ApplySetRendererParamCmd(
    Session* session, fuchsia::ui::gfx::SetRendererParamCmd command) {
  auto renderer =
      session->resources()->FindResource<Renderer>(command.renderer_id);
  if (renderer) {
    switch (command.param.Which()) {
      case fuchsia::ui::gfx::RendererParam::Tag::kShadowTechnique:
        return renderer->SetShadowTechnique(command.param.shadow_technique());
      case fuchsia::ui::gfx::RendererParam::Tag::kRenderFrequency:
        // TODO(SCN-1169): SetRenderContinuously should only affect the
        // compositor that has the renderer attached to it.
        session->session_context().frame_scheduler->SetRenderContinuously(
            command.param.render_frequency() ==
            fuchsia::ui::gfx::RenderFrequency::CONTINUOUSLY);
        return true;
      case fuchsia::ui::gfx::RendererParam::Tag::kEnableDebugging:
        renderer->set_enable_debugging(command.param.enable_debugging());
        return true;
      case fuchsia::ui::gfx::RendererParam::Tag::Invalid:
        session->error_reporter()->ERROR()
            << "scenic_impl::gfx::GfxCommandApplier::"
               "ApplySetRendererParamCmd(): "
               "invalid param.";
    }
  }
  return false;
}

bool GfxCommandApplier::ApplySetEventMaskCmd(
    Session* session, fuchsia::ui::gfx::SetEventMaskCmd command) {
  if (auto r = session->resources()->FindResource<Resource>(command.id)) {
    return r->SetEventMask(command.event_mask);
  }
  return false;
}

bool GfxCommandApplier::ApplySetCameraTransformCmd(
    Session* session, fuchsia::ui::gfx::SetCameraTransformCmd command) {
  // TODO(SCN-123): support variables.
  if (IsVariable(command.eye_position) || IsVariable(command.eye_look_at) ||
      IsVariable(command.eye_up)) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplySetCameraTransformCmd(): "
           "unimplemented: variable properties.";
    return false;
  } else if (auto camera = session->resources()->FindResource<Camera>(
                 command.camera_id)) {
    camera->SetTransform(UnwrapVector3(command.eye_position),
                         UnwrapVector3(command.eye_look_at),
                         UnwrapVector3(command.eye_up));
    return true;
  }
  return false;
}

bool GfxCommandApplier::ApplySetCameraProjectionCmd(
    Session* session, fuchsia::ui::gfx::SetCameraProjectionCmd command) {
  // TODO(SCN-123): support variables.
  if (IsVariable(command.fovy)) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplySetCameraProjectionCmd(): "
           "unimplemented: variable properties.";
    return false;
  } else if (auto camera = session->resources()->FindResource<Camera>(
                 command.camera_id)) {
    camera->SetProjection(UnwrapFloat(command.fovy));
    return true;
  }
  return false;
}

bool GfxCommandApplier::ApplySetStereoCameraProjectionCmd(
    Session* session, fuchsia::ui::gfx::SetStereoCameraProjectionCmd command) {
  if (IsVariable(command.left_projection) ||
      IsVariable(command.right_projection)) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplySetStereoCameraProjectionOp(): "
           "unimplemented: variable properties.";
    return false;
  } else if (auto stereo_camera =
                 session->resources()->FindResource<StereoCamera>(
                     command.camera_id)) {
    stereo_camera->SetStereoProjection(Unwrap(command.left_projection.value),
                                       Unwrap(command.right_projection.value));
    return true;
  }
  return false;
}

bool GfxCommandApplier::ApplySetCameraPoseBufferCmd(
    Session* session, fuchsia::ui::gfx::SetCameraPoseBufferCmd command) {
  if (command.base_time > dispatcher_clock_now()) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplySetCameraPoseBufferCmd(): "
           "base time not in the past";
    return false;
  }

  auto buffer = session->resources()->FindResource<Buffer>(command.buffer_id);
  if (!buffer) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplySetCameraPoseBufferCmd(S): "
           "invalid buffer ID";
    return false;
  }

  if (command.num_entries < 1) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplySetCameraPoseBufferCmd(): "
           "must have at least one entry in the pose buffer";
    return false;
  }

  if (buffer->size() < command.num_entries * sizeof(escher::hmd::Pose)) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplySetCameraPoseBufferCmd(): "
           "buffer is not large enough";
    return false;
  }

  auto camera = session->resources()->FindResource<Camera>(command.camera_id);
  if (!camera) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplySetCameraPoseBufferCmd(): "
           "invalid camera ID";
    return false;
  }

  camera->SetPoseBuffer(buffer, command.num_entries, command.base_time,
                        command.time_interval);

  return true;
}

bool GfxCommandApplier::ApplySetLightColorCmd(
    Session* session, fuchsia::ui::gfx::SetLightColorCmd command) {
  // TODO(SCN-123): support variables.
  if (command.color.variable_id) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::ApplySetLightColorCmd(): "
           "unimplemented: variable color.";
    return false;
  } else if (auto light =
                 session->resources()->FindResource<Light>(command.light_id)) {
    return light->SetColor(Unwrap(command.color.value));
  }
  return false;
}

bool GfxCommandApplier::ApplySetLightDirectionCmd(
    Session* session, fuchsia::ui::gfx::SetLightDirectionCmd command) {
  // TODO(SCN-123): support variables.
  if (command.direction.variable_id) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplySetLightDirectionCmd(): "
           "unimplemented: variable direction.";
    return false;
  } else if (auto light = session->resources()->FindResource<DirectionalLight>(
                 command.light_id)) {
    return light->SetDirection(Unwrap(command.direction.value));
  }
  return false;
}

bool GfxCommandApplier::ApplySetPointLightPositionCmd(
    Session* session, fuchsia::ui::gfx::SetPointLightPositionCmd command) {
  // TODO(SCN-123): support variables.
  if (command.position.variable_id) {
    session->error_reporter()->ERROR() << "scenic_impl::gfx::GfxCommandApplier:"
                                          ":ApplySetPointLightPositionCmd(): "
                                          "unimplemented: variable position.";
    return false;
  } else if (auto light = session->resources()->FindResource<PointLight>(
                 command.light_id)) {
    return light->SetPosition(Unwrap(command.position.value));
  }
  return false;
}

bool GfxCommandApplier::ApplySetPointLightFalloffCmd(
    Session* session, fuchsia::ui::gfx::SetPointLightFalloffCmd command) {
  // TODO(SCN-123): support variables.
  if (command.falloff.variable_id) {
    session->error_reporter()->ERROR() << "scenic_impl::gfx::GfxCommandApplier:"
                                          ":ApplySetPointLightFalloffCmd(): "
                                          "unimplemented: variable falloff.";
    return false;
  } else if (auto light = session->resources()->FindResource<PointLight>(
                 command.light_id)) {
    return light->SetFalloff(command.falloff.value);
  }
  return false;
}

bool GfxCommandApplier::ApplyAddLightCmd(
    Session* session, fuchsia::ui::gfx::AddLightCmd command) {
  if (auto scene =
          session->resources()->FindResource<Scene>(command.scene_id)) {
    if (auto light =
            session->resources()->FindResource<Light>(command.light_id)) {
      return scene->AddLight(std::move(light));
    }
  }

  session->error_reporter()->ERROR() << "scenic_impl::gfx::GfxCommandApplier::"
                                        "ApplyAddLightCmd(): unimplemented.";
  return false;
}

bool GfxCommandApplier::ApplySceneAddAmbientLightCmd(
    Session* session, fuchsia::ui::gfx::SceneAddAmbientLightCmd command) {
  if (auto scene =
          session->resources()->FindResource<Scene>(command.scene_id)) {
    if (auto light = session->resources()->FindResource<AmbientLight>(
            command.light_id)) {
      return scene->AddAmbientLight(std::move(light));
    }
  }

  session->error_reporter()->ERROR()
      << "scenic_impl::gfx::GfxCommandApplier::"
         "ApplySceneAddAmbientLightCmd(): unimplemented.";
  return false;
}

bool GfxCommandApplier::ApplySceneAddDirectionalLightCmd(
    Session* session, fuchsia::ui::gfx::SceneAddDirectionalLightCmd command) {
  if (auto scene =
          session->resources()->FindResource<Scene>(command.scene_id)) {
    if (auto light = session->resources()->FindResource<DirectionalLight>(
            command.light_id)) {
      return scene->AddDirectionalLight(std::move(light));
    }
  }

  session->error_reporter()->ERROR()
      << "scenic_impl::gfx::GfxCommandApplier::"
         "ApplySceneAddDirectionalLightCmd(): unimplemented.";
  return false;
}

bool GfxCommandApplier::ApplySceneAddPointLightCmd(
    Session* session, fuchsia::ui::gfx::SceneAddPointLightCmd command) {
  if (auto scene =
          session->resources()->FindResource<Scene>(command.scene_id)) {
    if (auto light =
            session->resources()->FindResource<PointLight>(command.light_id)) {
      return scene->AddPointLight(std::move(light));
    }
  }

  session->error_reporter()->ERROR()
      << "scenic_impl::gfx::GfxCommandApplier::"
         "ApplySceneAddPointLightCmd(): unimplemented.";
  return false;
}

bool GfxCommandApplier::ApplyDetachLightCmd(
    Session* session, fuchsia::ui::gfx::DetachLightCmd command) {
  session->error_reporter()->ERROR() << "scenic_impl::gfx::GfxCommandApplier::"
                                        "ApplyDetachLightCmd(): unimplemented.";
  return false;
}

bool GfxCommandApplier::ApplyDetachLightsCmd(
    Session* session, fuchsia::ui::gfx::DetachLightsCmd command) {
  session->error_reporter()->ERROR()
      << "scenic_impl::gfx::GfxCommandApplier::"
         "ApplyDetachLightsCmd(): unimplemented.";
  return false;
}

bool GfxCommandApplier::ApplySetLabelCmd(
    Session* session, fuchsia::ui::gfx::SetLabelCmd command) {
  if (auto r = session->resources()->FindResource<Resource>(command.id)) {
    return r->SetLabel(command.label);
  }
  return false;
}

bool GfxCommandApplier::ApplySetDisableClippingCmd(
    Session* session, fuchsia::ui::gfx::SetDisableClippingCmd command) {
  if (auto r =
          session->resources()->FindResource<Renderer>(command.renderer_id)) {
    r->DisableClipping(command.disable_clipping);
    return true;
  }
  return false;
}

bool GfxCommandApplier::ApplyCreateMemory(Session* session, ResourceId id,
                                          fuchsia::ui::gfx::MemoryArgs args) {
  auto memory = CreateMemory(session, id, std::move(args));
  return memory ? session->resources()->AddResource(id, std::move(memory))
                : false;
}

bool GfxCommandApplier::ApplyCreateImage(Session* session, ResourceId id,
                                         fuchsia::ui::gfx::ImageArgs args) {
  if (auto memory =
          session->resources()->FindResource<Memory>(args.memory_id)) {
    if (auto image = CreateImage(session, id, std::move(memory), args)) {
      return session->resources()->AddResource(id, std::move(image));
    }
  }

  return false;
}

bool GfxCommandApplier::ApplyCreateImagePipe(
    Session* session, ResourceId id, fuchsia::ui::gfx::ImagePipeArgs args) {
  auto image_pipe = fxl::MakeRefCounted<ImagePipe>(
      session, id, std::move(args.image_pipe_request),
      session->session_context().frame_scheduler);
  return session->resources()->AddResource(id, image_pipe);
}

bool GfxCommandApplier::ApplyCreateBuffer(Session* session, ResourceId id,
                                          fuchsia::ui::gfx::BufferArgs args) {
  if (auto memory =
          session->resources()->FindResource<Memory>(args.memory_id)) {
    if (auto buffer = CreateBuffer(session, id, std::move(memory),
                                   args.memory_offset, args.num_bytes)) {
      return session->resources()->AddResource(id, std::move(buffer));
    }
  }
  return false;
}

bool GfxCommandApplier::ApplyCreateScene(Session* session, ResourceId id,
                                         fuchsia::ui::gfx::SceneArgs args) {
  auto scene = CreateScene(session, id, std::move(args));
  return scene ? session->resources()->AddResource(id, std::move(scene))
               : false;
}

bool GfxCommandApplier::ApplyCreateCamera(Session* session, ResourceId id,
                                          fuchsia::ui::gfx::CameraArgs args) {
  auto camera = CreateCamera(session, id, std::move(args));
  return camera ? session->resources()->AddResource(id, std::move(camera))
                : false;
}

bool GfxCommandApplier::ApplyCreateStereoCamera(
    Session* session, ResourceId id, fuchsia::ui::gfx::StereoCameraArgs args) {
  auto camera = CreateStereoCamera(session, id, args);
  return camera ? session->resources()->AddResource(id, std::move(camera))
                : false;
}

bool GfxCommandApplier::ApplyCreateRenderer(
    Session* session, ResourceId id, fuchsia::ui::gfx::RendererArgs args) {
  auto renderer = CreateRenderer(session, id, std::move(args));
  return renderer ? session->resources()->AddResource(id, std::move(renderer))
                  : false;
}

bool GfxCommandApplier::ApplyCreateAmbientLight(
    Session* session, ResourceId id, fuchsia::ui::gfx::AmbientLightArgs args) {
  auto light = CreateAmbientLight(session, id);
  return light ? session->resources()->AddResource(id, std::move(light))
               : false;
}

bool GfxCommandApplier::ApplyCreateDirectionalLight(
    Session* session, ResourceId id,
    fuchsia::ui::gfx::DirectionalLightArgs args) {
  // TODO(SCN-1255): temporarily disable directional light creation ASAP, so
  // that people don't try to use them before we decide whether we want them.
  // They are currently only used by RootPresenter and example programs.
  // session->error_reporter()->ERROR()
  //     << "fuchsia.ui.gfx.CreateResourceCmd: DirectionalLights are
  //     disabled";
  // return false;
  auto light = CreateDirectionalLight(session, id);
  return light ? session->resources()->AddResource(id, std::move(light))
               : false;
}

bool GfxCommandApplier::ApplyCreatePointLight(
    Session* session, ResourceId id, fuchsia::ui::gfx::PointLightArgs args) {
  auto light = CreatePointLight(session, id);
  return light ? session->resources()->AddResource(id, std::move(light))
               : false;
}

bool GfxCommandApplier::ApplyCreateRectangle(
    Session* session, ResourceId id, fuchsia::ui::gfx::RectangleArgs args) {
  if (!AssertValueIsOfType(args.width, kFloatValueTypes, session) ||
      !AssertValueIsOfType(args.height, kFloatValueTypes, session)) {
    return false;
  }

  // TODO(SCN-123): support variables.
  if (IsVariable(args.width) || IsVariable(args.height)) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::ApplyCreateRectangle(): "
           "unimplemented: variable width/height.";
    return false;
  }

  auto rectangle =
      CreateRectangle(session, id, args.width.vector1(), args.height.vector1());
  return rectangle ? session->resources()->AddResource(id, std::move(rectangle))
                   : false;
}

bool GfxCommandApplier::ApplyCreateRoundedRectangle(
    Session* session, CommandContext* command_context, ResourceId id,
    fuchsia::ui::gfx::RoundedRectangleArgs args) {
  if (!AssertValueIsOfType(args.width, kFloatValueTypes, session) ||
      !AssertValueIsOfType(args.height, kFloatValueTypes, session) ||
      !AssertValueIsOfType(args.top_left_radius, kFloatValueTypes, session) ||
      !AssertValueIsOfType(args.top_right_radius, kFloatValueTypes, session) ||
      !AssertValueIsOfType(args.bottom_left_radius, kFloatValueTypes,
                           session) ||
      !AssertValueIsOfType(args.bottom_right_radius, kFloatValueTypes,
                           session)) {
    return false;
  }

  // TODO(SCN-123): support variables.
  if (IsVariable(args.width) || IsVariable(args.height) ||
      IsVariable(args.top_left_radius) || IsVariable(args.top_right_radius) ||
      IsVariable(args.bottom_left_radius) ||
      IsVariable(args.bottom_right_radius)) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::"
           "ApplyCreateRoundedRectangle(): "
           "unimplemented: variable width/height/radii.";
    return false;
  }

  const float width = args.width.vector1();
  const float height = args.height.vector1();
  const float top_left_radius = args.top_left_radius.vector1();
  const float top_right_radius = args.top_right_radius.vector1();
  const float bottom_right_radius = args.bottom_right_radius.vector1();
  const float bottom_left_radius = args.bottom_left_radius.vector1();

  auto rectangle = CreateRoundedRectangle(
      session, command_context, id, width, height, top_left_radius,
      top_right_radius, bottom_right_radius, bottom_left_radius);
  return rectangle ? session->resources()->AddResource(id, std::move(rectangle))
                   : false;
}

bool GfxCommandApplier::ApplyCreateCircle(Session* session, ResourceId id,
                                          fuchsia::ui::gfx::CircleArgs args) {
  if (!AssertValueIsOfType(args.radius, kFloatValueTypes, session)) {
    return false;
  }

  // TODO(SCN-123): support variables.
  if (IsVariable(args.radius)) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::ApplyCreateCircle(): "
           "unimplemented: variable radius.";
    return false;
  }

  auto circle = CreateCircle(session, id, args.radius.vector1());
  return circle ? session->resources()->AddResource(id, std::move(circle))
                : false;
}

bool GfxCommandApplier::ApplyCreateMesh(Session* session, ResourceId id,
                                        fuchsia::ui::gfx::MeshArgs args) {
  auto mesh = CreateMesh(session, id);
  return mesh ? session->resources()->AddResource(id, std::move(mesh)) : false;
}

bool GfxCommandApplier::ApplyCreateMaterial(
    Session* session, ResourceId id, fuchsia::ui::gfx::MaterialArgs args) {
  auto material = CreateMaterial(session, id);
  return material ? session->resources()->AddResource(id, std::move(material))
                  : false;
}

bool GfxCommandApplier::ApplyCreateView(Session* session, ResourceId id,
                                        fuchsia::ui::gfx::ViewArgs2 args) {
  // Sanity check.  We also rely on FIDL to enforce this for us, although it
  // does not at the moment.
  FXL_DCHECK(args.token.value) << "scenic_impl::gfx::GfxCommandApplier::"
                                  "ApplyCreateView(): no token provided.";
  if (auto view = CreateView(session, id, std::move(args))) {
    if (!(session->SetRootView(view->As<View>()->GetWeakPtr()))) {
      FXL_LOG(ERROR) << "Error: cannot set more than one root view in a"
                     << " session. This will soon become a session-terminating "
                     << "error. For more info, see [SCN-1249].";
      // TODO(SCN-1249) Return false and report the error in this case, and
      // shut down any sessions that violate the one-view-per-session contract.
      // return false;
    }
    view->As<View>()->Connect();  // Initiate the link.
    session->resources()->AddResource(id, std::move(view));
    return true;
  }
  return false;
}

bool GfxCommandApplier::ApplyCreateViewHolder(
    Session* session, ResourceId id, fuchsia::ui::gfx::ViewHolderArgs2 args) {
  // Sanity check.  We also rely on FIDL to enforce this for us, although it
  // does not at the moment
  FXL_DCHECK(args.token.value)
      << "scenic_impl::gfx::GfxCommandApplier::ApplyCreateViewHolder()"
         ": no token provided.";

  if (auto view_holder = CreateViewHolder(session, id, std::move(args))) {
    view_holder->As<ViewHolder>()->Connect();  // Initiate the ViewHolder link.
    session->resources()->AddResource(id, std::move(view_holder));
    return true;
  }
  return false;
}

bool GfxCommandApplier::ApplyCreateClipNode(
    Session* session, ResourceId id, fuchsia::ui::gfx::ClipNodeArgs args) {
  auto node = CreateClipNode(session, id, std::move(args));
  return node ? session->resources()->AddResource(id, std::move(node)) : false;
}

bool GfxCommandApplier::ApplyCreateEntityNode(
    Session* session, ResourceId id, fuchsia::ui::gfx::EntityNodeArgs args) {
  auto node = CreateEntityNode(session, id, std::move(args));
  return node ? session->resources()->AddResource(id, std::move(node)) : false;
}

bool GfxCommandApplier::ApplyCreateOpacityNode(
    Session* session, ResourceId id, fuchsia::ui::gfx::OpacityNodeArgs args) {
  auto node = CreateOpacityNode(session, id, args);
  return node ? session->resources()->AddResource(id, std::move(node)) : false;
}

bool GfxCommandApplier::ApplyCreateShapeNode(
    Session* session, ResourceId id, fuchsia::ui::gfx::ShapeNodeArgs args) {
  auto node = CreateShapeNode(session, id, std::move(args));
  return node ? session->resources()->AddResource(id, std::move(node)) : false;
}

bool GfxCommandApplier::ApplyCreateCompositor(
    Session* session, ResourceId id, fuchsia::ui::gfx::CompositorArgs args) {
  auto compositor = CreateCompositor(session, id, std::move(args));
  return compositor
             ? session->resources()->AddResource(id, std::move(compositor))
             : false;
}

bool GfxCommandApplier::ApplyCreateDisplayCompositor(
    Session* session, ResourceId id,
    fuchsia::ui::gfx::DisplayCompositorArgs args) {
  auto compositor = CreateDisplayCompositor(session, id, std::move(args));
  return compositor
             ? session->resources()->AddResource(id, std::move(compositor))
             : false;
}

bool GfxCommandApplier::ApplyCreateImagePipeCompositor(
    Session* session, ResourceId id,
    fuchsia::ui::gfx::ImagePipeCompositorArgs args) {
  auto compositor = CreateImagePipeCompositor(session, id, std::move(args));
  return compositor
             ? session->resources()->AddResource(id, std::move(compositor))
             : false;
}

bool GfxCommandApplier::ApplyCreateLayerStack(
    Session* session, ResourceId id, fuchsia::ui::gfx::LayerStackArgs args) {
  auto layer_stack = CreateLayerStack(session, id, std::move(args));
  return layer_stack
             ? session->resources()->AddResource(id, std::move(layer_stack))
             : false;
}

bool GfxCommandApplier::ApplyCreateLayer(Session* session, ResourceId id,
                                         fuchsia::ui::gfx::LayerArgs args) {
  auto layer = CreateLayer(session, id, std::move(args));
  return layer ? session->resources()->AddResource(id, std::move(layer))
               : false;
}

bool GfxCommandApplier::ApplyCreateVariable(
    Session* session, ResourceId id, fuchsia::ui::gfx::VariableArgs args) {
  auto variable = CreateVariable(session, id, std::move(args));
  return variable ? session->resources()->AddResource(id, std::move(variable))
                  : false;
}

bool GfxCommandApplier::ApplySetDisplayColorConversionCmd(
    Session* session,
    fuchsia::ui::gfx::SetDisplayColorConversionCmdHACK command) {
  if (auto compositor = session->resources()->FindResource<Compositor>(
          command.compositor_id)) {
    if (auto swapchain = compositor->swapchain()) {
      ColorTransform transform;
      transform.preoffsets = command.preoffsets;
      transform.matrix = command.matrix;
      transform.postoffsets = command.postoffsets;
      swapchain->SetDisplayColorConversion(transform);
      return true;
    }
  }
  return false;
}

ResourcePtr GfxCommandApplier::CreateMemory(Session* session, ResourceId id,
                                            fuchsia::ui::gfx::MemoryArgs args) {
  return Memory::New(session, id, std::move(args), session->error_reporter());
}

ResourcePtr GfxCommandApplier::CreateImage(Session* session, ResourceId id,
                                           MemoryPtr memory,
                                           fuchsia::ui::gfx::ImageArgs args) {
  return Image::New(session, id, memory, args.info, args.memory_offset,
                    session->error_reporter());
}

ResourcePtr GfxCommandApplier::CreateBuffer(Session* session, ResourceId id,
                                            MemoryPtr memory,
                                            uint32_t memory_offset,
                                            uint32_t num_bytes) {
  if (memory_offset + num_bytes > memory->size()) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::CreateBuffer(): "
           "buffer does not fit within memory (buffer "
           "offset: "
        << memory_offset << ", buffer size: " << num_bytes
        << ", memory size: " << memory->size() << ")";
    return ResourcePtr();
  }

  // Make a pointer to a subregion of the memory, if necessary.
  escher::GpuMemPtr gpu_mem =
      (memory_offset > 0 || num_bytes < memory->size())
          ? memory->GetGpuMem()->Suballocate(num_bytes, memory_offset)
          : memory->GetGpuMem();

  return fxl::MakeRefCounted<Buffer>(session, id, std::move(gpu_mem),
                                     std::move(memory));
}

ResourcePtr GfxCommandApplier::CreateScene(Session* session, ResourceId id,
                                           fuchsia::ui::gfx::SceneArgs args) {
  return fxl::MakeRefCounted<Scene>(session, id);
}

ResourcePtr GfxCommandApplier::CreateCamera(Session* session, ResourceId id,
                                            fuchsia::ui::gfx::CameraArgs args) {
  if (auto scene = session->resources()->FindResource<Scene>(args.scene_id)) {
    return fxl::MakeRefCounted<Camera>(session, id, std::move(scene));
  }
  return ResourcePtr();
}

ResourcePtr GfxCommandApplier::CreateStereoCamera(
    Session* session, ResourceId id,
    const fuchsia::ui::gfx::StereoCameraArgs args) {
  if (auto scene = session->resources()->FindResource<Scene>(args.scene_id)) {
    return fxl::MakeRefCounted<StereoCamera>(session, id, std::move(scene));
  }
  return ResourcePtr();
}

ResourcePtr GfxCommandApplier::CreateRenderer(
    Session* session, ResourceId id, fuchsia::ui::gfx::RendererArgs args) {
  return fxl::MakeRefCounted<Renderer>(session, id);
}

ResourcePtr GfxCommandApplier::CreateAmbientLight(Session* session,
                                                  ResourceId id) {
  return fxl::MakeRefCounted<AmbientLight>(session, id);
}

ResourcePtr GfxCommandApplier::CreateDirectionalLight(Session* session,
                                                      ResourceId id) {
  return fxl::MakeRefCounted<DirectionalLight>(session, id);
}

ResourcePtr GfxCommandApplier::CreatePointLight(Session* session,
                                                ResourceId id) {
  return fxl::MakeRefCounted<PointLight>(session, id);
}

ResourcePtr GfxCommandApplier::CreateView(Session* session, ResourceId id,
                                          fuchsia::ui::gfx::ViewArgs2 args) {
  ViewLinker* view_linker = session->session_context().view_linker;
  ViewLinker::ImportLink link = view_linker->CreateImport(
      std::move(args.token.value), session->error_reporter());

  // Create a View if the Link was successfully registered.
  if (link.valid()) {
    return fxl::MakeRefCounted<View>(session, id, std::move(link));
  }
  return nullptr;
}

ResourcePtr GfxCommandApplier::CreateViewHolder(
    Session* session, ResourceId id, fuchsia::ui::gfx::ViewHolderArgs2 args) {
  ViewLinker* view_linker = session->session_context().view_linker;
  ViewLinker::ExportLink link = view_linker->CreateExport(
      std::move(args.token.value), session->error_reporter());

  // Create a ViewHolder if the Link was successfully registered.
  if (link.valid()) {
    return fxl::MakeRefCounted<ViewHolder>(session, id, std::move(link));
  }
  return nullptr;
}

ResourcePtr GfxCommandApplier::CreateClipNode(
    Session* session, ResourceId id, fuchsia::ui::gfx::ClipNodeArgs args) {
  session->error_reporter()->ERROR()
      << "scenic_impl::gfx::GfxCommandApplier::CreateClipNode(): "
         "unimplemented.";
  return ResourcePtr();
}

ResourcePtr GfxCommandApplier::CreateEntityNode(
    Session* session, ResourceId id, fuchsia::ui::gfx::EntityNodeArgs args) {
  return fxl::MakeRefCounted<EntityNode>(session, id);
}

ResourcePtr GfxCommandApplier::CreateOpacityNode(
    Session* session, ResourceId id, fuchsia::ui::gfx::OpacityNodeArgs args) {
  return fxl::MakeRefCounted<OpacityNode>(session, id);
}

ResourcePtr GfxCommandApplier::CreateShapeNode(
    Session* session, ResourceId id, fuchsia::ui::gfx::ShapeNodeArgs args) {
  return fxl::MakeRefCounted<ShapeNode>(session, id);
}

ResourcePtr GfxCommandApplier::CreateCompositor(
    Session* session, ResourceId id, fuchsia::ui::gfx::CompositorArgs args) {
  return Compositor::New(session, id, session->session_context().scene_graph);
}

ResourcePtr GfxCommandApplier::CreateDisplayCompositor(
    Session* session, ResourceId id,
    fuchsia::ui::gfx::DisplayCompositorArgs args) {
  Display* display =
      session->session_context().display_manager->default_display();
  if (!display) {
    session->error_reporter()->ERROR()
        << "There is no default display available.";
    return nullptr;
  }

  if (display->is_claimed()) {
    session->error_reporter()->ERROR()
        << "The default display has already been claimed "
           "by another compositor.";
    return nullptr;
  }

  return fxl::AdoptRef(new DisplayCompositor(
      session, id, session->session_context().scene_graph, display,
      SwapchainFactory::CreateDisplaySwapchain(
          display, session->session_context().display_manager,
          session->session_context().event_timestamper,
          session->session_context().escher)));
}

ResourcePtr GfxCommandApplier::CreateImagePipeCompositor(
    Session* session, ResourceId id,
    fuchsia::ui::gfx::ImagePipeCompositorArgs args) {
  // TODO(SCN-179)
  session->error_reporter()->ERROR() << "scenic_impl::gfx::GfxCommandApplier::"
                                        "ApplyCreateImagePipeCompositor() "
                                        "is unimplemented (SCN-179)";
  return ResourcePtr();
}

ResourcePtr GfxCommandApplier::CreateLayerStack(
    Session* session, ResourceId id, fuchsia::ui::gfx::LayerStackArgs args) {
  return fxl::MakeRefCounted<LayerStack>(session, id);
}

ResourcePtr GfxCommandApplier::CreateVariable(
    Session* session, ResourceId id, fuchsia::ui::gfx::VariableArgs args) {
  fxl::RefPtr<Variable> variable;
  switch (args.type) {
    case fuchsia::ui::gfx::ValueType::kVector1:
      variable = fxl::MakeRefCounted<FloatVariable>(session, id);
      break;
    case fuchsia::ui::gfx::ValueType::kVector2:
      variable = fxl::MakeRefCounted<Vector2Variable>(session, id);
      break;
    case fuchsia::ui::gfx::ValueType::kVector3:
      variable = fxl::MakeRefCounted<Vector3Variable>(session, id);
      break;
    case fuchsia::ui::gfx::ValueType::kVector4:
      variable = fxl::MakeRefCounted<Vector4Variable>(session, id);
      break;
    case fuchsia::ui::gfx::ValueType::kMatrix4:
      variable = fxl::MakeRefCounted<Matrix4x4Variable>(session, id);
      break;
    case fuchsia::ui::gfx::ValueType::kColorRgb:
      // not yet supported
      variable = nullptr;
      break;
    case fuchsia::ui::gfx::ValueType::kColorRgba:
      // not yet supported
      variable = nullptr;
      break;
    case fuchsia::ui::gfx::ValueType::kQuaternion:
      variable = fxl::MakeRefCounted<QuaternionVariable>(session, id);
      break;
    case fuchsia::ui::gfx::ValueType::kFactoredTransform:
      /* variable = fxl::MakeRefCounted<TransformVariable>(session, id); */
      variable = nullptr;
      break;
    case fuchsia::ui::gfx::ValueType::kNone:
      break;
  }
  if (variable && variable->SetValue(args.initial_value)) {
    return variable;
  }
  return nullptr;
}

ResourcePtr GfxCommandApplier::CreateLayer(Session* session, ResourceId id,
                                           fuchsia::ui::gfx::LayerArgs args) {
  return fxl::MakeRefCounted<Layer>(session, id);
}

ResourcePtr GfxCommandApplier::CreateCircle(Session* session, ResourceId id,
                                            float initial_radius) {
  return fxl::MakeRefCounted<CircleShape>(session, id, initial_radius);
}

ResourcePtr GfxCommandApplier::CreateRectangle(Session* session, ResourceId id,
                                               float width, float height) {
  return fxl::MakeRefCounted<RectangleShape>(session, id, width, height);
}

ResourcePtr GfxCommandApplier::CreateRoundedRectangle(
    Session* session, CommandContext* command_context, ResourceId id,
    float width, float height, float top_left_radius, float top_right_radius,
    float bottom_right_radius, float bottom_left_radius) {
  auto factory = session->session_context().escher_rounded_rect_factory;
  if (!factory) {
    session->error_reporter()->ERROR()
        << "scenic_impl::gfx::GfxCommandApplier::CreateRoundedRectangle(): "
           "no RoundedRectFactory available.";
    return ResourcePtr();
  }

  // If radii sum exceeds width or height, scale them down.
  float top_radii_sum = top_left_radius + top_right_radius;
  float top_scale = std::min(width / top_radii_sum, 1.f);

  float bottom_radii_sum = bottom_left_radius + bottom_right_radius;
  float bottom_scale = std::min(width / bottom_radii_sum, 1.f);

  float left_radii_sum = top_left_radius + bottom_left_radius;
  float left_scale = std::min(height / left_radii_sum, 1.f);

  float right_radii_sum = top_right_radius + bottom_right_radius;
  float right_scale = std::min(height / right_radii_sum, 1.f);

  top_left_radius *= std::min(top_scale, left_scale);
  top_right_radius *= std::min(top_scale, right_scale);
  bottom_left_radius *= std::min(bottom_scale, left_scale);
  bottom_right_radius *= std::min(bottom_scale, right_scale);

  escher::RoundedRectSpec rect_spec(width, height, top_left_radius,
                                    top_right_radius, bottom_right_radius,
                                    bottom_left_radius);
  escher::MeshSpec mesh_spec{escher::MeshAttribute::kPosition2D |
                             escher::MeshAttribute::kUV};

  return fxl::MakeRefCounted<RoundedRectangleShape>(
      session, id, rect_spec,
      factory->NewRoundedRect(rect_spec, mesh_spec,
                              command_context->batch_gpu_uploader()));
}

ResourcePtr GfxCommandApplier::CreateMesh(Session* session, ResourceId id) {
  return fxl::MakeRefCounted<MeshShape>(session, id);
}

ResourcePtr GfxCommandApplier::CreateMaterial(Session* session, ResourceId id) {
  return fxl::MakeRefCounted<Material>(session, id);
}

}  // namespace gfx
}  // namespace scenic_impl
