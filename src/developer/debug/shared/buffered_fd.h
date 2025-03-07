// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "src/developer/debug/shared/fd_watcher.h"
#include "src/developer/debug/shared/message_loop.h"
#include "src/developer/debug/shared/stream_buffer.h"
#include "src/lib/files/unique_fd.h"

namespace debug_ipc {

class BufferedFD final : public FDWatcher, public StreamBuffer::Writer {
 public:
  using DataAvailableCallback = std::function<void()>;
  using ErrorCallback = std::function<void()>;

  BufferedFD();
  ~BufferedFD();

  // A MessageLoop must already be set up on the current threa.
  //
  // Returns true on success.
  bool Init(fxl::UniqueFD fd);

  void set_data_available_callback(DataAvailableCallback cb) { callback_ = cb; }
  void set_error_callback(ErrorCallback cb) { error_callback_ = cb; }

  StreamBuffer& stream() { return stream_; }
  const StreamBuffer& stream() const { return stream_; }

 private:
  // FDWatcher implementation:
  void OnFDReady(int fd, bool read, bool write, bool err) override;

  // Error handler
  void OnFDError();

  // StreamBuffer::Writer implementation.
  size_t ConsumeStreamBufferData(const char* data, size_t len) override;

  fxl::UniqueFD fd_;
  StreamBuffer stream_;
  MessageLoop::WatchHandle watch_handle_;

  DataAvailableCallback callback_;
  ErrorCallback error_callback_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BufferedFD);
};

}  // namespace debug_ipc
