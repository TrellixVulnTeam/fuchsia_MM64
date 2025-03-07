// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/audio_core/audio_plug_detector.h"

#include <dirent.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fcntl.h>
#include <fuchsia/hardware/audio/cpp/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/fit/defer.h>
#include <lib/zx/channel.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/device/audio.h>
#include <zircon/device/vfs.h>

#include "garnet/bin/media/audio_core/audio_device_manager.h"
#include "garnet/bin/media/audio_core/audio_input.h"
#include "garnet/bin/media/audio_core/audio_output.h"
#include "garnet/bin/media/audio_core/driver_output.h"
#include "src/lib/files/unique_fd.h"

namespace media::audio {

static const struct {
  const char* path;
  bool is_input;
} AUDIO_DEVNODES[] = {
    {.path = "/dev/class/audio-output", .is_input = false},
    {.path = "/dev/class/audio-input", .is_input = true},
};

AudioPlugDetector::~AudioPlugDetector() { FXL_DCHECK(manager_ == nullptr); }

zx_status_t AudioPlugDetector::Start(AudioDeviceManager* manager) {
  FXL_DCHECK(manager != nullptr);

  // If we fail to set up monitoring for any of our target directories,
  // automatically stop monitoring all sources of device nodes.
  auto error_cleanup = fit::defer([this]() { Stop(); });

  // If we are already running, we cannot start again.  Cancel the cleanup
  // operation and report that things are successfully started.
  if (manager_ != nullptr) {
    FXL_DLOG(WARNING) << "Attempted to start the AudioPlugDetector twice!";
    error_cleanup.cancel();
    return ZX_OK;
  }

  // Record our new manager
  manager_ = manager;

  // Create our watchers.
  for (const auto& devnode : AUDIO_DEVNODES) {
    auto watcher = fsl::DeviceWatcher::Create(
        devnode.path, [this, is_input = devnode.is_input](
                          int dir_fd, const std::string& filename) {
          AddAudioDevice(dir_fd, filename, is_input);
        });

    if (watcher == nullptr) {
      FXL_LOG(ERROR)
          << "AudioPlugDetector failed to create DeviceWatcher for \""
          << devnode.path << "\".";
      return ZX_ERR_NO_MEMORY;
    }

    watchers_.emplace_back(std::move(watcher));
  }

  error_cleanup.cancel();

  return ZX_OK;
}

void AudioPlugDetector::Stop() {
  manager_ = nullptr;
  watchers_.clear();
}

void AudioPlugDetector::AddAudioDevice(int dir_fd, const std::string& name,
                                       bool is_input) {
  if (manager_ == nullptr)
    return;

  // Open the device node.
  fbl::unique_fd dev_node(::openat(dir_fd, name.c_str(), O_RDONLY));
  if (!dev_node.is_valid()) {
    FXL_LOG(WARNING) << "AudioPlugDetector failed to open device node at \""
                     << name << "\". (" << strerror(errno) << " : " << errno
                     << ")";
    return;
  }

  // Obtain the FDIO device channel, then wrap it in a synchronous proxy and use
  // it to get the stream channel.
  zx_status_t res;
  zx::channel dev_channel;
  res = fdio_get_service_handle(dev_node.release(),
                                dev_channel.reset_and_get_address());
  if (res != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to obtain FDIO service channel to AudioOutput "
                     << "(res " << res << ")";
    return;
  }

  // Obtain the stream channel
  zx::channel channel;
  ::fuchsia::hardware::audio::Device_SyncProxy dev(std::move(dev_channel));

  res = dev.GetChannel(&channel);
  if (res != ZX_OK) {
    FXL_LOG(WARNING) << "Failed to open channel to AudioOutput (res " << res
                     << ")";
    return;
  }

  // Hand the stream off to the proper type of class to manage.
  fbl::RefPtr<AudioDevice> new_device;
  if (is_input) {
    new_device = AudioInput::Create(std::move(channel), manager_);
  } else {
    new_device = DriverOutput::Create(std::move(channel), manager_);
  }

  if (new_device == nullptr) {
    FXL_LOG(WARNING) << "Failed to instantiate audio "
                     << (is_input ? "input" : "output") << " for \"" << name
                     << "\"";
  } else {
    manager_->AddDevice(new_device);
  }
}

}  // namespace media::audio
