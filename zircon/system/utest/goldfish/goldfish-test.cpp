// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <fuchsia/hardware/goldfish/pipe/c/fidl.h>
#include <lib/fdio/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmo.h>
#include <unistd.h>
#include <unittest/unittest.h>

static bool GoldfishPipeFdioTest() {
    BEGIN_TEST;

    int fd = open("/dev/class/goldfish-pipe/000", O_RDWR);
    EXPECT_GE(fd, 0);

    // Connect to pingpong service.
    constexpr char kPipeName[] = "pipe:pingpong";
    ssize_t bytes = strlen(kPipeName) + 1;
    EXPECT_EQ(write(fd, kPipeName, bytes), bytes);

    // Write 1 byte.
    const uint8_t kSentinel = 0xaa;
    EXPECT_EQ(write(fd, &kSentinel, 1), 1);

    // Read 1 byte result.
    uint8_t result = 0;
    EXPECT_EQ(read(fd, &result, 1), 1);

    // pingpong service should have returned the data received.
    EXPECT_EQ(result, kSentinel);

    // Write 3 * 4096 bytes.
    const size_t kSize = 3 * 4096;
    uint8_t send_buffer[kSize];
    memset(send_buffer, kSentinel, kSize);
    EXPECT_EQ(write(fd, send_buffer, kSize), kSize);

    // Read 3 * 4096 bytes.
    uint8_t recv_buffer[kSize];
    EXPECT_EQ(read(fd, recv_buffer, kSize), kSize);

    // pingpong service should have returned the data received.
    EXPECT_EQ(memcmp(send_buffer, recv_buffer, kSize), 0);

    close(fd);

    END_TEST;
}

static bool GoldfishPipeFidlTest() {
    BEGIN_TEST;

    int fd = open("/dev/class/goldfish-pipe/000", O_RDWR);
    EXPECT_GE(fd, 0);

    zx::channel channel;
    EXPECT_EQ(fdio_get_service_handle(fd, channel.reset_and_get_address()),
              ZX_OK);

    int32_t res;
    const size_t kSize = 3 * 4096;
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceSetBufferSize(channel.get(),
                                                                 kSize, &res),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);

    zx::vmo vmo;
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceGetBuffer(
                  channel.get(), &res, vmo.reset_and_get_address()),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);

    // Connect to pingpong service.
    constexpr char kPipeName[] = "pipe:pingpong";
    size_t bytes = strlen(kPipeName) + 1;
    EXPECT_EQ(vmo.write(kPipeName, 0, bytes), ZX_OK);
    uint64_t actual;
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceWrite(channel.get(), bytes,
                                                         0, &res, &actual),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_EQ(actual, bytes);

    // Write 1 byte.
    const uint8_t kSentinel = 0xaa;
    EXPECT_EQ(vmo.write(&kSentinel, 0, 1), ZX_OK);
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceWrite(channel.get(), 1, 0,
                                                         &res, &actual),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_EQ(actual, 1);

    // Read 1 byte result.
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceRead(channel.get(), 1, 0,
                                                        &res, &actual),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_EQ(actual, 1);
    uint8_t result = 0;
    EXPECT_EQ(vmo.read(&result, 0, 1), ZX_OK);

    // pingpong service should have returned the data received.
    EXPECT_EQ(result, kSentinel);

    // Write 3 * 4096 bytes.
    uint8_t send_buffer[kSize];
    memset(send_buffer, kSentinel, kSize);
    EXPECT_EQ(vmo.write(send_buffer, 0, kSize), ZX_OK);
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceWrite(channel.get(), kSize,
                                                         0, &res, &actual),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_EQ(actual, kSize);

    // Read 3 * 4096 bytes.
    EXPECT_EQ(fuchsia_hardware_goldfish_pipe_DeviceRead(channel.get(), kSize, 0,
                                                        &res, &actual),
              ZX_OK);
    EXPECT_EQ(res, ZX_OK);
    EXPECT_EQ(actual, kSize);
    uint8_t recv_buffer[kSize];
    EXPECT_EQ(vmo.read(recv_buffer, 0, kSize), ZX_OK);

    // pingpong service should have returned the data received.
    EXPECT_EQ(memcmp(send_buffer, recv_buffer, kSize), 0);

    END_TEST;
}

BEGIN_TEST_CASE(GoldfishPipeTests)
RUN_TEST(GoldfishPipeFdioTest)
RUN_TEST(GoldfishPipeFidlTest)
END_TEST_CASE(GoldfishPipeTests)

int main(int argc, char** argv) {
    if (access("/dev/sys/platform/acpi/goldfish", F_OK) != -1) {
        return unittest_run_all_tests(argc, argv) ? 0 : -1;
    }
    return 0;
}
