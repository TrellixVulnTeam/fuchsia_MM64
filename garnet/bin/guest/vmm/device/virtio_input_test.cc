// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <virtio/input.h>

#include "garnet/bin/guest/vmm/device/input.h"
#include "garnet/bin/guest/vmm/device/test_with_device.h"
#include "garnet/bin/guest/vmm/device/virtio_queue_fake.h"

static constexpr char kVirtioInputUrl[] =
    "fuchsia-pkg://fuchsia.com/virtio_input#meta/virtio_input.cmx";
static constexpr uint16_t kNumQueues = 1;
static constexpr uint16_t kQueueSize = 16;

class VirtioInputTest : public TestWithDevice {
 protected:
  VirtioInputTest()
      : event_queue_(phys_mem_, PAGE_SIZE * kNumQueues, kQueueSize) {}

  void SetUp() override {
    // Launch device process.
    fuchsia::guest::device::StartInfo start_info;
    zx_status_t status =
        LaunchDevice(kVirtioInputUrl, event_queue_.end(), &start_info);
    ASSERT_EQ(ZX_OK, status);

    // Start device execution.
    services_->Connect(view_listener_.NewRequest());
    services_->Connect(input_.NewRequest());
    status = input_->Start(std::move(start_info));
    ASSERT_EQ(ZX_OK, status);

    // Configure device queues.
    VirtioQueueFake* queues[kNumQueues] = {&event_queue_};
    for (size_t i = 0; i < kNumQueues; i++) {
      auto q = queues[i];
      q->Configure(PAGE_SIZE * i, PAGE_SIZE);
      status = input_->ConfigureQueue(i, q->size(), q->desc(), q->avail(),
                                      q->used());
      ASSERT_EQ(ZX_OK, status);
    }
  }

  fuchsia::guest::device::VirtioInputSyncPtr input_;
  fuchsia::guest::device::ViewListenerSyncPtr view_listener_;
  VirtioQueueFake event_queue_;
};

TEST_F(VirtioInputTest, Keyboard) {
  fuchsia::ui::input::InputEvent fuchsia_event;
  fuchsia_event.set_keyboard({
      .phase = fuchsia::ui::input::KeyboardEventPhase::PRESSED,
      .hid_usage = 4,
  });
  view_listener_->OnInputEvent(std::move(fuchsia_event));

  virtio_input_event_t* event_1;
  virtio_input_event_t* event_2;
  zx_status_t status = DescriptorChainBuilder(event_queue_)
                           .AppendWritableDescriptor(&event_1, sizeof(*event_1))
                           .AppendWritableDescriptor(&event_2, sizeof(*event_2))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = input_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_INPUT_EV_KEY, event_1->type);
  EXPECT_EQ(30, event_1->code);
  EXPECT_EQ(VIRTIO_INPUT_EV_KEY_PRESSED, event_1->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_SYN, event_2->type);
}

TEST_F(VirtioInputTest, PointerMove) {
  view_listener_->OnSizeChanged({1, 1});
  fuchsia::ui::input::InputEvent fuchsia_event;
  fuchsia::ui::input::PointerEvent pointer = {
      .phase = fuchsia::ui::input::PointerEventPhase::MOVE,
      .x = 0.25,
      .y = 0.5,
  };
  fuchsia_event.set_pointer(pointer);
  view_listener_->OnInputEvent(std::move(fuchsia_event));

  virtio_input_event_t* event_1;
  virtio_input_event_t* event_2;
  virtio_input_event_t* event_3;
  zx_status_t status = DescriptorChainBuilder(event_queue_)
                           .AppendWritableDescriptor(&event_1, sizeof(*event_1))
                           .AppendWritableDescriptor(&event_2, sizeof(*event_2))
                           .AppendWritableDescriptor(&event_3, sizeof(*event_3))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = input_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_INPUT_EV_ABS, event_1->type);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS_X, event_1->code);
  EXPECT_EQ(std::ceil(kInputAbsMaxX * pointer.x), event_1->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS, event_2->type);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS_Y, event_2->code);
  EXPECT_EQ(std::ceil(kInputAbsMaxY * pointer.y), event_2->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_SYN, event_3->type);
}

TEST_F(VirtioInputTest, PointerUp) {
  view_listener_->OnSizeChanged({1, 1});
  fuchsia::ui::input::InputEvent fuchsia_event;
  fuchsia::ui::input::PointerEvent pointer = {
      .phase = fuchsia::ui::input::PointerEventPhase::UP,
      .x = 0.25,
      .y = 0.5,
  };
  fuchsia_event.set_pointer(pointer);
  view_listener_->OnInputEvent(std::move(fuchsia_event));

  virtio_input_event_t* event_1;
  virtio_input_event_t* event_2;
  virtio_input_event_t* event_3;
  virtio_input_event_t* event_4;
  zx_status_t status = DescriptorChainBuilder(event_queue_)
                           .AppendWritableDescriptor(&event_1, sizeof(*event_1))
                           .AppendWritableDescriptor(&event_2, sizeof(*event_2))
                           .AppendWritableDescriptor(&event_3, sizeof(*event_3))
                           .AppendWritableDescriptor(&event_4, sizeof(*event_4))
                           .Build();
  ASSERT_EQ(ZX_OK, status);

  status = input_->NotifyQueue(0);
  ASSERT_EQ(ZX_OK, status);
  status = WaitOnInterrupt();
  ASSERT_EQ(ZX_OK, status);

  EXPECT_EQ(VIRTIO_INPUT_EV_ABS, event_1->type);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS_X, event_1->code);
  EXPECT_EQ(std::ceil(kInputAbsMaxX * pointer.x), event_1->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS, event_2->type);
  EXPECT_EQ(VIRTIO_INPUT_EV_ABS_Y, event_2->code);
  EXPECT_EQ(std::ceil(kInputAbsMaxY * pointer.y), event_2->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_KEY, event_3->type);
  EXPECT_EQ(kButtonTouchCode, event_3->code);
  EXPECT_EQ(VIRTIO_INPUT_EV_KEY_RELEASED, event_3->value);
  EXPECT_EQ(VIRTIO_INPUT_EV_SYN, event_4->type);
}
