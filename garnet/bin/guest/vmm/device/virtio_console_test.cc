// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/device/test_with_device.h"
#include "garnet/bin/guest/vmm/device/virtio_queue_fake.h"

static constexpr char kVirtioConsoleUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_console#meta/virtio_console.cmx";
static constexpr uint16_t kNumQueues = 2;
static constexpr uint16_t kQueueSize = 16;

class VirtioConsoleTest : public TestWithDevice {
 protected:
  VirtioConsoleTest()
      : rx_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize),
        tx_queue_(phys_mem_, rx_queue_.end(), kQueueSize) {}

  void SetUp() override {
    // Launch device process.
    fuchsia::guest::device::StartInfo start_info;
    zx_status_t status =
        LaunchDevice(kVirtioConsoleUrl, tx_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    // Setup console socket.
    status = zx::socket::create(ZX_SOCKET_STREAM, &socket_, &remote_socket_);
    ASSERT_EQ(ZX_OK, status);

    // Start device execution.
    services_->Connect(console_.NewRequest());
    status = console_->Start(std::move(start_info), std::move(remote_socket_));
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&rx_queue_, &tx_queue_};
    for (size_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PAGE_SIZE * i, PAGE_SIZE);
      status = console_->ConfigureQueue(i, q->size(), q->desc(), q->avail(),
                                        q->used());
      ASSERT_EQ(ZX_OK, status);
    }
  }

  fuchsia::guest::device::VirtioConsoleSyncPtr console_;
  VirtioQueueFake rx_queue_;
  VirtioQueueFake tx_queue_;
  zx::socket socket_;
  zx::socket remote_socket_;
};

TEST_F(VirtioConsoleTest, Receive) {
  void* data_1;
  void* data_2;
  zx_status_t status = DescriptorChainBuilder(rx_queue_)
                           .AppendWritableDescriptor(&data_1, 6)
                           .AppendWritableDescriptor(&data_2, 6)
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  char input[] = "hello\0world";
  size_t actual;
  status = socket_.write(0, input, sizeof(input), &actual);
  ASSERT_EQ(ZX_OK, status);
  EXPECT_EQ(sizeof(input), actual);

  status = console_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_STREQ("hello", static_cast<char*>(data_1));
  EXPECT_STREQ("world", static_cast<char*>(data_2));
}

TEST_F(VirtioConsoleTest, Transmit) {
  zx_status_t status =
      DescriptorChainBuilder(tx_queue_)
          .AppendReadableDescriptor("hello ", sizeof("hello ") - 1)
          .AppendReadableDescriptor("world", sizeof("world"))
          .Build();
  ASSERT_EQ(ZX_OK, status);

  status = console_->NotifyQueue(1);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  char buf[16] = {};
  size_t actual;
  status = socket_.read(0, buf, sizeof(buf), &actual);
  ASSERT_EQ(ZX_OK, status);
  char output[] = "hello world";
  EXPECT_EQ(sizeof(output), actual);
  EXPECT_STREQ(output, buf);
}
