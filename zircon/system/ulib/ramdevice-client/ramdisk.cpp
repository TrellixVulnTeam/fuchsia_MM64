// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/auto_call.h>
#include <fbl/string.h>
#include <fbl/string_printf.h>
#include <fbl/unique_fd.h>
#include <fuchsia/device/c/fidl.h>
#include <fuchsia/hardware/block/c/fidl.h>
#include <fuchsia/hardware/ramdisk/c/fidl.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/fdio.h>
#include <lib/zx/channel.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <zircon/boot/image.h>
#include <zircon/device/block.h>
#include <zircon/device/vfs.h>
#include <zircon/process.h>
#include <zircon/processargs.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <ramdevice-client/ramdisk.h>

#define RAMCTL_PATH "/dev/misc/ramctl"
#define BLOCK_EXTENSION "block"

static zx_status_t driver_watcher_cb(int dirfd, int event, const char* fn, void* cookie) {
    char* wanted = static_cast<char*>(cookie);
    if (event == WATCH_EVENT_ADD_FILE && strcmp(fn, wanted) == 0) {
        return ZX_ERR_STOP;
    }
    return ZX_OK;
}

static zx_status_t wait_for_device_impl(char* path, const zx::time& deadline) {
    zx_status_t rc;

    // Peel off last path segment
    char* sep = strrchr(path, '/');
    if (path[0] == '\0' || (!sep)) {
        fprintf(stderr, "invalid device path '%s'\n", path);
        return ZX_ERR_BAD_PATH;
    }
    char* last = sep + 1;

    *sep = '\0';
    auto restore_path = fbl::MakeAutoCall([sep] { *sep = '/'; });

    // Recursively check the path up to this point
    struct stat buf;
    if (stat(path, &buf) != 0 && (rc = wait_for_device_impl(path, deadline)) != ZX_OK) {
        fprintf(stderr, "failed to bind '%s': %s\n", path, zx_status_get_string(rc));
        return rc;
    }

    // Early exit if this segment is empty
    if (last[0] == '\0') {
        return ZX_OK;
    }

    // Open the parent directory
    DIR* dir = opendir(path);
    if (!dir) {
        fprintf(stderr, "unable to open '%s'\n", path);
        return ZX_ERR_NOT_FOUND;
    }
    auto close_dir = fbl::MakeAutoCall([&] { closedir(dir); });

    // Wait for the next path segment to show up
    rc = fdio_watch_directory(dirfd(dir), driver_watcher_cb, deadline.get(), last);
    if (rc != ZX_ERR_STOP) {
        fprintf(stderr, "error when waiting for '%s': %s\n", last, zx_status_get_string(rc));
        return rc;
    }

    return ZX_OK;
}

struct ramdisk_client {
public:
    DISALLOW_COPY_ASSIGN_AND_MOVE(ramdisk_client);

    static zx_status_t Create(const char* instance_name, zx::duration duration,
                              std::unique_ptr<ramdisk_client>* out) {
        fbl::String ramdisk_path = fbl::StringPrintf("%s/%s", RAMCTL_PATH, instance_name);
        fbl::unique_fd ramdisk_fd(open(ramdisk_path.c_str(), O_RDWR));
        if (!ramdisk_fd) {
            return ZX_ERR_BAD_STATE;
        }
        zx_handle_t ramdisk_interface_raw;
        zx_status_t status = fdio_get_service_handle(ramdisk_fd.release(), &ramdisk_interface_raw);
        if (status != ZX_OK) {
            return status;
        }
        zx::channel ramdisk_interface(ramdisk_interface_raw);

        // If binding to the block interface fails, ensure we still try to tear down the
        // ramdisk driver.
        auto cleanup = fbl::MakeAutoCall([&ramdisk_interface]() {
            ramdisk_client::DestroyByHandle(std::move(ramdisk_interface));
        });

        fbl::String path = fbl::String::Concat({ramdisk_path, "/", BLOCK_EXTENSION});
        status = wait_for_device(path.c_str(), duration.get());
        if (status != ZX_OK) {
            return status;
        }
        fbl::unique_fd block_fd(open(path.c_str(), O_RDWR));
        if (!block_fd) {
            return ZX_ERR_BAD_STATE;
        }

        cleanup.cancel();
        *out = std::unique_ptr<ramdisk_client>(
            new ramdisk_client(std::move(path), std::move(ramdisk_interface), std::move(block_fd)));
        return ZX_OK;
    }

    zx_status_t Rebind() {
        fzl::FdioCaller disk_client(std::move(block_fd_));
        zx_status_t io_status, status;
        io_status = fuchsia_hardware_block_BlockRebindDevice(disk_client.borrow_channel(),
                                                             &status);
        if (io_status != ZX_OK) {
            return io_status;
        } else if (status != ZX_OK) {
            return status;
        }
        ramdisk_interface_.reset();

        // Ramdisk paths have the form: /dev/.../ramctl/ramdisk-xxx/block.
        // To rebind successfully, first, we rebind the "ramdisk-xxx" path,
        // and then we wait for "block" to rebind.

        // Wait for the "ramdisk-xxx" path to rebind.
        const char* sep = strrchr(path_.c_str(), '/');
        char ramdisk_path[PATH_MAX];
        strlcpy(ramdisk_path, path_.c_str(), sep - path_.c_str() + 1);
        status = wait_for_device_impl(ramdisk_path, zx::deadline_after(zx::sec(3)));
        if (status != ZX_OK) {
            return status;
        }

        fbl::unique_fd ramdisk_fd(open(ramdisk_path, O_RDWR));
        if (!ramdisk_fd) {
            return ZX_ERR_BAD_STATE;
        }

        zx_handle_t ramdisk_interface;
        status = fdio_get_service_handle(ramdisk_fd.release(), &ramdisk_interface);
        if (status != ZX_OK) {
            return status;
        }
        ramdisk_interface_.reset(ramdisk_interface);

        // Wait for the "block" path to rebind.
        strlcpy(ramdisk_path, path_.c_str(), sizeof(ramdisk_path));
        status = wait_for_device_impl(ramdisk_path, zx::deadline_after(zx::sec(3)));
        if (status != ZX_OK) {
            return status;
        }
        block_fd_.reset(open(path_.c_str(), O_RDWR));
        if (!block_fd_) {
            return ZX_ERR_BAD_STATE;
        }
        return ZX_OK;
    }

    zx_status_t Destroy() {
        if (!ramdisk_interface_) {
            return ZX_ERR_BAD_STATE;
        }

        zx_status_t status = DestroyByHandle(std::move(ramdisk_interface_));
        if (status != ZX_OK) {
            return status;
        }
        block_fd_.reset();
        return ZX_OK;
    }

    const zx::channel& ramdisk_interface() const { return ramdisk_interface_; }

    const fbl::unique_fd& block_fd() const { return block_fd_; }

    const fbl::String& path() const { return path_; }

    ~ramdisk_client() { Destroy(); }

private:
    ramdisk_client(fbl::String path, zx::channel ramdisk_interface, fbl::unique_fd block_fd)
        : path_(std::move(path)), ramdisk_interface_(std::move(ramdisk_interface)),
          block_fd_(std::move(block_fd)) {}

    static zx_status_t DestroyByHandle(zx::channel ramdisk) {
        zx_status_t call_status;
        zx_status_t status = fuchsia_device_ControllerUnbind(ramdisk.get(), &call_status);
        if (status != ZX_OK) {
            return status;
        }
        return call_status;
    }

    fbl::String path_;
    zx::channel ramdisk_interface_;
    fbl::unique_fd block_fd_;
};

// TODO(aarongreen): This is more generic than just fs-management, or even block devices.  Move this
// (and its tests) out of ramdisk and to somewhere else?
zx_status_t wait_for_device(const char* path, zx_duration_t timeout) {
    if (!path || timeout == 0) {
        fprintf(stderr, "invalid args: path='%s', timeout=%" PRIu64 "\n", path, timeout);
        return ZX_ERR_INVALID_ARGS;
    }

    // Make a mutable copy
    char tmp[PATH_MAX];
    snprintf(tmp, sizeof(tmp), "%s", path);
    zx::time deadline = zx::deadline_after(zx::duration(timeout));
    return wait_for_device_impl(tmp, deadline);
}

static zx_status_t open_ramctl(zx::channel* out_ramctl) {
    fbl::unique_fd fd(open(RAMCTL_PATH, O_RDWR));
    if (!fd) {
        return ZX_ERR_BAD_STATE;
    }

    zx_handle_t ramctl_interface_raw;
    zx_status_t status = fdio_get_service_handle(fd.release(), &ramctl_interface_raw);
    if (status != ZX_OK) {
        return status;
    }

    out_ramctl->reset(ramctl_interface_raw);
    return ZX_OK;
}

static const fuchsia_hardware_ramdisk_GUID* fidl_guid(const uint8_t* type_guid) {
    static_assert(sizeof(fuchsia_hardware_ramdisk_GUID) == ZBI_PARTITION_GUID_LEN,
                  "Byte array cannot be reinterpreted as FIDL GUID");
    return reinterpret_cast<const fuchsia_hardware_ramdisk_GUID*>(type_guid);
}

static zx_status_t ramdisk_create_with_guid_internal(uint64_t blk_size, uint64_t blk_count,
                                                     const uint8_t* type_guid,
                                                     ramdisk_client** out) {
    zx::channel ramctl;
    zx_status_t status = open_ramctl(&ramctl);
    if (status != ZX_OK) {
        return status;
    }

    char name[fuchsia_hardware_ramdisk_MAX_NAME_LENGTH + 1];
    size_t name_len = 0;
    zx_status_t io_status = fuchsia_hardware_ramdisk_RamdiskControllerCreate(
        ramctl.get(), blk_size, blk_count, fidl_guid(type_guid), &status, name, sizeof(name) - 1,
        &name_len);
    if (io_status != ZX_OK) {
        return io_status;
    } else if (status != ZX_OK) {
        return status;
    }

    // Always force 'name' to be null-terminated.
    name[name_len] = '\0';

    std::unique_ptr<ramdisk_client> client;
    status = ramdisk_client::Create(name, zx::sec(3), &client);
    if (status != ZX_OK) {
        return status;
    }
    *out = client.release();
    return ZX_OK;
}

zx_status_t ramdisk_create(uint64_t blk_size, uint64_t blk_count, ramdisk_client** out) {
    return ramdisk_create_with_guid_internal(blk_size, blk_count, nullptr, out);
}

zx_status_t ramdisk_create_with_guid(uint64_t blk_size, uint64_t blk_count,
                                     const uint8_t* type_guid, size_t guid_len,
                                     ramdisk_client** out) {
    if (type_guid == nullptr || guid_len < ZBI_PARTITION_GUID_LEN) {
        return ZX_ERR_INVALID_ARGS;
    }
    return ramdisk_create_with_guid_internal(blk_size, blk_count, type_guid, out);
}

zx_status_t ramdisk_create_from_vmo(zx_handle_t raw_vmo, ramdisk_client** out) {
    zx::vmo vmo(raw_vmo);
    zx::channel ramctl;
    zx_status_t status = open_ramctl(&ramctl);
    if (status != ZX_OK) {
        return status;
    }

    char name[fuchsia_hardware_ramdisk_MAX_NAME_LENGTH + 1];
    size_t name_len = 0;
    zx_status_t io_status = fuchsia_hardware_ramdisk_RamdiskControllerCreateFromVmo(
        ramctl.get(), vmo.release(), &status, name, sizeof(name) - 1, &name_len);
    if (io_status != ZX_OK) {
        return io_status;
    } else if (status != ZX_OK) {
        return status;
    }

    // Always force 'name' to be null-terminated.
    name[name_len] = '\0';

    std::unique_ptr<ramdisk_client> client;
    status = ramdisk_client::Create(name, zx::sec(3), &client);
    if (status != ZX_OK) {
        return status;
    }
    *out = client.release();
    return ZX_OK;
}

int ramdisk_get_block_fd(const ramdisk_client_t* client) {
    return client->block_fd().get();
}

const char* ramdisk_get_path(const ramdisk_client_t* client) {
    return client->path().c_str();
}

zx_status_t ramdisk_sleep_after(const ramdisk_client* client, uint64_t block_count) {
    zx_status_t status;
    zx_status_t io_status = fuchsia_hardware_ramdisk_RamdiskSleepAfter(
        client->ramdisk_interface().get(), block_count, &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t ramdisk_wake(const ramdisk_client* client) {
    zx_status_t status;
    zx_status_t io_status =
        fuchsia_hardware_ramdisk_RamdiskWake(client->ramdisk_interface().get(), &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t ramdisk_grow(const ramdisk_client* client, uint64_t required_size) {
    zx_status_t status;
    zx_status_t io_status = fuchsia_hardware_ramdisk_RamdiskGrow(
        client->ramdisk_interface().get(), required_size, &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t ramdisk_set_flags(const ramdisk_client* client, uint32_t flags) {
    zx_status_t status;
    zx_status_t io_status =
        fuchsia_hardware_ramdisk_RamdiskSetFlags(client->ramdisk_interface().get(), flags, &status);
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t ramdisk_get_block_counts(const ramdisk_client* client,
                                     ramdisk_block_write_counts_t* out_counts) {
    static_assert(sizeof(ramdisk_block_write_counts_t) ==
                      sizeof(fuchsia_hardware_ramdisk_BlockWriteCounts),
                  "Cannot convert between C library / FIDL block counts");

    zx_status_t status;
    zx_status_t io_status = fuchsia_hardware_ramdisk_RamdiskGetBlockCounts(
        client->ramdisk_interface().get(), &status,
        reinterpret_cast<fuchsia_hardware_ramdisk_BlockWriteCounts*>(out_counts));
    if (io_status != ZX_OK) {
        return io_status;
    }
    return status;
}

zx_status_t ramdisk_rebind(ramdisk_client_t* client) {
    return client->Rebind();
}

zx_status_t ramdisk_destroy(ramdisk_client* client) {
    zx_status_t status = client->Destroy();
    delete client;
    return status;
}
