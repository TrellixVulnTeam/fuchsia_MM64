// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "command_buffer.h"
#include "helper/command_buffer_helper.h"
#include "helper/platform_device_helper.h"
#include "magma_util/sleep.h"
#include "mock/mock_address_space.h"
#include "msd_intel_context.h"
#include "msd_intel_device.h"
#include "gtest/gtest.h"

class TestCommandBuffer {
public:
    static std::unique_ptr<TestCommandBuffer> Create()
    {
        return std::unique_ptr<TestCommandBuffer>(new TestCommandBuffer());
    }

    void TestMapUnmapResourcesGpu()
    {

        auto addr_space =
            std::unique_ptr<MockAddressSpace>(new MockAddressSpace(0, 1024 * PAGE_SIZE));

        std::vector<gpu_addr_t> addresses;
        EXPECT_TRUE(cmd_buf_->MapResourcesGpu(addr_space.get(), addresses));

        uint32_t i = 0;
        for (auto addr : addresses) {
            EXPECT_TRUE(addr_space->is_allocated(addr));
            EXPECT_FALSE(addr_space->is_clear(addr));
            EXPECT_GE(addr_space->allocated_size(addr), helper_->resources()[i++]->size());
        }

        cmd_buf_->UnmapResourcesGpu(addr_space.get());

        for (auto addr : addresses) {
            EXPECT_FALSE(addr_space->is_allocated(addr));
        }
    }

    void TestPatchRelocations()
    {
        auto addr_space =
            std::unique_ptr<MockAddressSpace>(new MockAddressSpace(0, 1024 * PAGE_SIZE));

        auto batch_buf_index = cmd_buf_->cmd_buf_->batch_buffer_resource_index; // dont judge
        auto batch_buf = cmd_buf_->exec_resources_[batch_buf_index];
        void* batch_buf_virt_addr = 0;
        ASSERT_TRUE(batch_buf->platform_buffer()->MapCpu(&batch_buf_virt_addr));
        auto batch_buf_data = (uint32_t*)batch_buf_virt_addr;

        // Clear the relocations to be sure
        auto batch_buf_resource = &cmd_buf_->cmd_buf_->resources[batch_buf_index];
        for (uint32_t i = 0; i < batch_buf_resource->num_relocations; i++) {
            auto relocation = &batch_buf_resource->relocations[i];

            uint32_t dword_offset = relocation->offset / sizeof(uint32_t);
            batch_buf_data[dword_offset] = 0;
            dword_offset++;
            batch_buf_data[dword_offset] = 0;
        }

        // do the relocation foo
        std::vector<gpu_addr_t> addresses;
        EXPECT_TRUE(cmd_buf_->MapResourcesGpu(addr_space.get(), addresses));
        EXPECT_TRUE(cmd_buf_->PatchRelocations(addresses));

        // check that we foo'd it correctly
        for (uint32_t i = 0; i < batch_buf_resource->num_relocations; i++) {
            auto relocation = &batch_buf_resource->relocations[i];
            auto target_gpu_address = addresses[relocation->target_resource_index];
            auto expected_gpu_addr = target_gpu_address + relocation->target_offset;

            uint32_t dword_offset = relocation->offset / sizeof(uint32_t);
            EXPECT_EQ(magma::lower_32_bits(expected_gpu_addr), batch_buf_data[dword_offset]);
            dword_offset++;
            EXPECT_EQ(magma::upper_32_bits(expected_gpu_addr), batch_buf_data[dword_offset]);
        }
    }

    void TestPrepareForExecution()
    {

        auto engine = MsdIntelDevice::cast(helper_->dev()->msd_dev())->render_engine_cs();
        EXPECT_TRUE(cmd_buf_->PrepareForExecution(engine));

        auto ctx = cmd_buf_->context();

        EXPECT_NE(ctx, nullptr);
        EXPECT_NE(cmd_buf_->batch_buffer_gpu_addr(), kInvalidGpuAddr);

        // Check that context is initialized correctly
        EXPECT_TRUE(ctx->IsInitializedForEngine(engine->id()));
        EXPECT_NE(ctx->get_ringbuffer(engine->id()), nullptr);
        EXPECT_NE(ctx->get_context_buffer(engine->id()), nullptr);

        // Check that context is mapped correctly
        gpu_addr_t addr;
        EXPECT_TRUE(ctx->GetGpuAddress(engine->id(), &addr));
        EXPECT_NE(addr, kInvalidGpuAddr);
        EXPECT_TRUE(ctx->GetRingbufferGpuAddress(engine->id(), &addr));
        EXPECT_NE(addr, kInvalidGpuAddr);
        cmd_buf_.reset();
    }

    void TestExecute()
    {
        auto target_buffer = MsdIntelBuffer::Create(PAGE_SIZE);
        ASSERT_NE(target_buffer, nullptr);

        void* target_cpu_addr;
        gpu_addr_t target_gpu_addr;

        auto device = MsdIntelDevice::cast(helper_->dev()->msd_dev());
        auto context = MsdIntelAbiContext::cast(helper_->ctx())->ptr();
        auto addr_space = context->exec_address_space();

        ASSERT_TRUE(target_buffer->platform_buffer()->MapCpu(&target_cpu_addr));
        ASSERT_TRUE(target_buffer->MapGpu(addr_space, PAGE_SIZE));
        ASSERT_TRUE(target_buffer->GetGpuAddress(addr_space->id(), &target_gpu_addr));
        *reinterpret_cast<uint32_t*>(target_cpu_addr) = 0;

        auto batch_buf_index = cmd_buf_->cmd_buf_->batch_buffer_resource_index;
        auto batch_buf = cmd_buf_->exec_resources_[batch_buf_index];
        void* batch_cpu_addr;

        ASSERT_TRUE(batch_buf->platform_buffer()->MapCpu(&batch_cpu_addr));
        uint32_t expected_val = 0xdeadbeef;
        uint32_t* batch_ptr = reinterpret_cast<uint32_t*>(batch_cpu_addr);

        static constexpr uint32_t kDwordCount = 4;
        static constexpr uint32_t kAddressSpaceGtt = 1 << 22;
        // store dword
        *batch_ptr++ = (0x20 << 23) | (kDwordCount - 2) | kAddressSpaceGtt;
        *batch_ptr++ = magma::lower_32_bits(target_gpu_addr);
        *batch_ptr++ = magma::upper_32_bits(target_gpu_addr);
        *batch_ptr++ = expected_val;

        // batch end
        *batch_ptr++ = (0xA << 23);

        EXPECT_TRUE(helper_->Execute());

        {
            std::string dump;
            device->DumpToString(dump);
            DLOG("dump: %s", dump.c_str());
        }

        magma::msleep(100);

        uint32_t target_val = *reinterpret_cast<uint32_t*>(target_cpu_addr);
        EXPECT_EQ(target_val, expected_val);
    }

private:
    TestCommandBuffer()
    {
        auto platform_device = TestPlatformDevice::GetInstance();
        if (!platform_device)
            DLOG("TestCommandBuffer: No platform device");
        DLOG("creating helper");
        helper_ = CommandBufferHelper::Create(platform_device);
        DLOG("creating command buffer");
        cmd_buf_ = CommandBuffer::Create(helper_->abi_cmd_buf(), helper_->msd_resources().data(),
                                         MsdIntelAbiContext::cast(helper_->ctx())->ptr());
        DLOG("command buffer created");
    }

    std::unique_ptr<CommandBuffer> cmd_buf_;
    std::unique_ptr<CommandBufferHelper> helper_;
};

TEST(CommandBuffer, MapUnmapResourcesGpu)
{
    TestCommandBuffer::Create()->TestMapUnmapResourcesGpu();
}

TEST(CommandBuffer, PatchRelocations) { TestCommandBuffer::Create()->TestPatchRelocations(); }

TEST(CommandBuffer, PrepareForExecution) { TestCommandBuffer::Create()->TestPrepareForExecution(); }

TEST(CommandBuffer, Execute) { TestCommandBuffer::Create()->TestExecute(); }