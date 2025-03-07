// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cerrno>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>

#include <fuchsia/hardware/skipblock/c/fidl.h>
#include <zircon/boot/image.h>
#include <zircon/process.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <bootdata/decompress.h>
#include <fbl/macros.h>
#include <fbl/unique_fd.h>
#include <fbl/vector.h>
#include <lib/bootfs/parser.h>
#include <lib/fzl/fdio.h>
#include <lib/fzl/vmo-mapper.h>
#include <lib/zx/channel.h>
#include <lib/zx/vmar.h>
#include <lib/zx/vmo.h>

#include <zbi-bootfs/zbi-bootfs.h>

namespace zbi_bootfs {

bool ZbiBootfsParser::IsSkipBlock(const char* path,
                                  fuchsia_hardware_skipblock_PartitionInfo* partition_info) {
    fbl::unique_fd fd(open(path, O_RDONLY));
    if (!fd) {
        return false;
    }

    fzl::FdioCaller caller(std::move(fd));

    // |status| is used for the status of the whole FIDL request. We expect
    // |status| to be ZX_OK if the channel connects to a skip-block driver.
    // |op_status| refers to the status of the underlying read/write operation
    // and will be ZX_OK only if the read/write succeeds. It is NOT set if
    // the channel is not connected to a skip-block driver.
    zx_status_t op_status;

    zx_status_t status = fuchsia_hardware_skipblock_SkipBlockGetPartitionInfo(
        caller.borrow_channel(), &op_status, partition_info);

    return status == ZX_OK;
}

zx_status_t ZbiBootfsParser::ProcessZbi(zx::vmo& vmo_out, const char* filename) {

    zbi_header_t hdr;
    zx::vmo bootfs_vmo;

    zx_status_t status = zbi_vmo.read(&hdr, 0, sizeof(hdr));
    if (status != ZX_OK) {
        fprintf(stderr, "VMO read error\n");
        return ZX_ERR_BAD_STATE;
    }
    printf("ZBI Container Header\n");
    printf("ZBI type   = %08x\n", hdr.type);
    printf("ZBI Magic  = %08x\n", hdr.magic);
    printf("ZBI extra  = %08x\n", hdr.extra);
    printf("ZBI Length = %u\n", hdr.length);
    printf("ZBI Flags  = %08x\n", hdr.flags);

    if ((hdr.type != ZBI_TYPE_CONTAINER) || (hdr.extra != ZBI_CONTAINER_MAGIC)) {
        printf("ZBI item does not have a container header\n");
        return ZX_ERR_BAD_STATE;
    }

    uint32_t len = hdr.length;
    uint32_t off = sizeof(zbi_header_t);

    while (len > sizeof(zbi_header_t)) {
        status = zbi_vmo.read(&hdr, off, sizeof(hdr));
        if (status != ZX_OK) {
            fprintf(stderr, "VMO read error\n");
            break;
        }
        printf("ZBI Payload Header\n");
        printf("ZBI type   = %08x\n", hdr.type);
        printf("ZBI Magic  = %08x\n", hdr.magic);
        printf("ZBI extra  = %08x\n", hdr.extra);
        printf("ZBI Length = %u\n", hdr.length);
        printf("ZBI Flags  = %08x\n", hdr.flags);

        uint32_t item_len = ZBI_ALIGN(static_cast<uint32_t>(sizeof(zbi_header_t)) + hdr.length);
        if (item_len > len) {
            fprintf(stderr, "ZBI item too large (%u > %u)\n", item_len, len);
            break;
        }
        switch (hdr.type) {
        case ZBI_TYPE_CONTAINER:
            fprintf(stderr, "Unexpected ZBI container header\n");
            break;
        case ZBI_TYPE_STORAGE_BOOTFS: {
            if (hdr.flags & ZBI_FLAG_STORAGE_COMPRESSED) {
                const char* err_msg;

                status = decompress_bootdata(zx_vmar_root_self(), zbi_vmo.get(), off,
                                             hdr.length + sizeof(zbi_header_t),
                                             bootfs_vmo.reset_and_get_address(), &err_msg);
                if (status != ZX_OK) {
                    fprintf(stderr, "Failed to decompress bootfs: %s\n", err_msg);
                    break;
                }

            } else {
                fprintf(stderr,
                        "Processing an uncompressed ZBI image is not currently supported\n");
                return ZX_ERR_NOT_SUPPORTED;
            }

            bootfs::Parser parser;
            status = parser.Init(zx::unowned_vmo(bootfs_vmo));
            if (status != ZX_OK) {
                return status;
            }

            // TODO(joeljacob): Consider making the vector a class member
            // This will prevent unnecessarily re-reading the VMO
            fbl::Vector<const bootfs_entry_t*> parsed_entries;
            parser.Parse([&](const bootfs_entry_t* entry) {
                parsed_entries.push_back(entry);

                return ZX_OK;
            });

            for (const auto& parsed_entry : parsed_entries) {
                printf("Entry = %s\n ", parsed_entry->name);
                if (!(strcmp(parsed_entry->name, filename))) {
                    printf("Filename = %s\n ", parsed_entry->name);
                    printf("File name length = %d\n", parsed_entry->name_len);
                    printf("File data length = %d\n", parsed_entry->data_len);
                    printf("File data offset = %d\n", parsed_entry->data_off);
                    auto buffer = std::make_unique<uint8_t[]>(parsed_entry->data_len);
                    bootfs_vmo.read(buffer.get(), parsed_entry->data_off, sizeof(buffer));
                    vmo_out.write(buffer.get(), 0, sizeof(buffer));
                }
            }

            status = ZX_OK;
            break;
        }
        default:
            printf("Unknown payload type, processing will stop\n");
            status = ZX_ERR_NOT_SUPPORTED;
            break;
        }
        off += item_len;
        len -= item_len;
    }
    return status;
}

zx_status_t ZbiBootfsParser::Init(const char* input, size_t byte_offset) {

    zx_status_t status = LoadZbi(input, byte_offset);
    if (status != ZX_OK) {
        fprintf(stderr, "Error loading ZBI. Error code: %d\n", status);
    }
    return status;
}

zx_status_t ZbiBootfsParser::LoadZbi(const char* input, size_t byte_offset) {

    // Logic for skip-block devices.
    fuchsia_hardware_skipblock_PartitionInfo partition_info = {};

    zx::vmo vmo;
    fzl::VmoMapper mapping;

    size_t buf_size = 0;
    size_t input_bs = 0;

    fbl::unique_fd fd(open(input, O_RDONLY));
    if (!fd) {
        fprintf(stderr, "Couldn't open input file %s : %d\n", input, errno);
        return ZX_ERR_IO;
    }

    if ((IsSkipBlock(input, &partition_info))) {

        // Grab Block size for the partition we'd like to access
        input_bs = partition_info.block_size_bytes;

        // Check byte_offset validity
        if ((byte_offset % input_bs) != 0) {
            fprintf(stderr, "Byte Offset must be a multiple of %lu (block-size)\n", input_bs);
            return ZX_ERR_INVALID_ARGS;
        }

        // Set buffer size
        buf_size = partition_info.block_size_bytes;

        if (buf_size == 0) {
            fprintf(stderr, "Buffer size must be greater than zero\n");
            return ZX_ERR_BUFFER_TOO_SMALL;
        }

        zx_status_t status = zx::vmo::create(buf_size, 0, &vmo);

        if (status != ZX_OK) {
            fprintf(stderr, "Error creating VMO\n");
            return status;
        }

        zx::vmo dup;
        status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
        if (status != ZX_OK) {
            fprintf(stderr, "Cannot duplicate handle\n");
            return status;
        }

        const uint32_t block_count =
            static_cast<uint32_t>(input_bs / partition_info.block_size_bytes);
        fuchsia_hardware_skipblock_ReadWriteOperation op = {
            .vmo = dup.release(),
            .vmo_offset = 0,
            .block = static_cast<uint32_t>((byte_offset / partition_info.block_size_bytes)),
            .block_count = block_count,
        };

        fzl::FdioCaller caller(std::move(fd));

        fuchsia_hardware_skipblock_SkipBlockRead(caller.borrow_channel(), &op, &status);

        if (status != ZX_OK) {
            fprintf(stderr, "Failed to read skip-block partition. Error code: %d\n", status);
            return status;
        }

        // Check ZBI header for content length and set buffer size
        // accordingly
        zbi_header_t hdr;
        status = vmo.read(&hdr, 0, sizeof(hdr));
        if (status != ZX_OK) {
            fprintf(stderr, "VMO read error\n");
            return status;
        }

        printf("ZBI container type = %08x\n", hdr.type);
        printf("ZBI payload length = %u\n", hdr.length);

        // Check if ZBI contents are larger than the size of one block
        // Resize the VMO accordingly
        if ((hdr.length + sizeof(zbi_header_t)) > buf_size) {

            vmo.set_size(buf_size + (hdr.length + sizeof(zbi_header_t)));

            uint64_t vmo_size;
            status = vmo.get_size(&vmo_size);
            if (status != ZX_OK || vmo_size == 0) {
                printf("Error resizing VMO\n");
                return status;
            }

            zx::vmo dup;
            status = vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &dup);
            if (status != ZX_OK) {
                fprintf(stderr, "Cannot duplicate handle\n");
                return status;
            }

            const uint32_t block_count =
            static_cast<uint32_t>(input_bs / partition_info.block_size_bytes);
            fuchsia_hardware_skipblock_ReadWriteOperation op = {
                .vmo = dup.release(),
                .vmo_offset = 0,
                .block = static_cast<uint32_t>((byte_offset / partition_info.block_size_bytes)),
                .block_count = block_count,
            };

            zx_status_t status;
            fuchsia_hardware_skipblock_SkipBlockRead(caller.borrow_channel(), &op, &status);

            if (status != ZX_OK) {
                fprintf(stderr, "Failed to read skip-block partition. Error code: %d\n", status);
                return status;
            }
        }

    } else {

        // Check ZBI header for content length and set buffer size
        // accordingly
        char buf[sizeof(zbi_header_t)];

        read(fd.get(), buf, sizeof(buf));
        zbi_header_t* hdr = reinterpret_cast<zbi_header_t*>(&buf);
        printf("ZBI container type = %08x\n", hdr->type);
        printf("ZBI payload length = %u\n", hdr->length);
        buf_size = hdr->length + sizeof(zbi_header_t);

        if (buf_size == 0) {
            fprintf(stderr, "Buffer size must be greater than zero\n");
            return ZX_ERR_BUFFER_TOO_SMALL;
        }

        zx_status_t status = mapping.CreateAndMap(buf_size, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE,
                                                  nullptr, &vmo, ZX_RIGHT_SAME_RIGHTS, 0);
        if (status != ZX_OK) {
            fprintf(stderr, "Error creating and mapping VMO\n");
            return status;
        }
        if (lseek(fd.get(), byte_offset, SEEK_SET) != static_cast<uint32_t>(byte_offset)) {
            fprintf(stderr, "Failed to read at offset = %zu\n", byte_offset);
            return ZX_ERR_IO;
        }

        // Read in input file (on disk) into buffer
        read(fd.get(), mapping.start(), mapping.size());
    }

    zbi_vmo = std::move(vmo);
    return ZX_OK;
}
} // namespace zbi_bootfs