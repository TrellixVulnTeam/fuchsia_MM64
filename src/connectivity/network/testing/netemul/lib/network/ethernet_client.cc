// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ethernet_client.h"

#include <fbl/intrusive_single_list.h>
#include <fbl/unique_ptr.h>
#include <fcntl.h>
#include <fuchsia/hardware/ethernet/cpp/fidl.h>
#include <inttypes.h>
#include <lib/async/cpp/wait.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/fdio/watcher.h>
#include <lib/fzl/fifo.h>
#include <zircon/status.h>

#include <iostream>
#include <sstream>

namespace netemul {
using ZDevice = fuchsia::hardware::ethernet::Device;
using ZFifos = fuchsia::hardware::ethernet::Fifos;
using ZFifoEntry = eth_fifo_entry_t;
constexpr uint32_t ETH_SIGNAL_STATUS =
    fuchsia::hardware::ethernet::SIGNAL_STATUS;

// WatchCbArgs is intended to be used *only* to capture context for fdio dir
// watching. Note that base_dir and search_mac are references, not copies. That
// is only allowed because fdio dir watch callback is called synchronously.
struct WatchCbArgs {
  std::string result;
  const std::string& base_dir;
  const EthernetClient::Mac& search_mac;
};

class FifoHolder {
 public:
  struct LLFifoEntry
      : public fbl::SinglyLinkedListable<fbl::unique_ptr<LLFifoEntry>> {
    ZFifoEntry e;
  };

  explicit FifoHolder(async_dispatcher_t* dispatcher,
                      std::unique_ptr<ZFifos> fifos,
                      const EthernetConfig& config)
      : dispatcher_(dispatcher), buf_config_(config) {
    tx_.reset(fifos->tx.release());
    rx_.reset(fifos->rx.release());
  }

  ~FifoHolder() {
    if (mapped_ > 0) {
      zx::vmar::root_self()->unmap(mapped_, vmo_size_);
    }
  }

  void Startup(fidl::InterfacePtr<ZDevice>& device,
               fit::function<void(zx_status_t)> callback) {
    vmo_size_ = 2u * buf_config_.nbufs * buf_config_.buff_size;

    zx_status_t status =
        zx::vmo::create(vmo_size_, ZX_VMO_NON_RESIZABLE, &buf_);
    if (status != ZX_OK) {
      fprintf(stderr, "could not create a vmo of size %" PRIu64 ": %s\n",
              vmo_size_, zx_status_get_string(status));
      callback(status);
      return;
    }

    status = zx::vmar::root_self()->map(
        0, buf_, 0, vmo_size_, ZX_VM_PERM_READ | ZX_VM_PERM_WRITE, &mapped_);
    if (status != ZX_OK) {
      fprintf(stderr, "failed to map vmo: %s\n", zx_status_get_string(status));
      callback(status);
      return;
    }

    zx::vmo buf_copy;
    status = buf_.duplicate(ZX_RIGHT_SAME_RIGHTS, &buf_copy);
    if (status != ZX_OK) {
      fprintf(stderr, "failed to duplicate vmo: %s\n",
              zx_status_get_string(status));
      callback(status);
      return;
    }

    device->SetIOBuffer(
        std::move(buf_copy),
        [this, callback = std::move(callback)](zx_status_t status) {
          if (status != ZX_OK) {
            callback(status);
            return;
          }

          uint32_t idx = 0;
          for (; idx < buf_config_.nbufs; idx++) {
            ZFifoEntry entry = {
                .offset = idx * buf_config_.buff_size,
                .length = buf_config_.buff_size,
                .flags = 0,
                .cookie = 0,
            };
            status = rx_.write_one(entry);
            if (status != ZX_OK) {
              fprintf(stderr, "failed call to write(): %s\n",
                      zx_status_get_string(status));
              callback(status);
              return;
            }
          }

          for (; idx < 2 * buf_config_.nbufs; idx++) {
            auto entry = fbl::unique_ptr<LLFifoEntry>(new LLFifoEntry);
            entry->e.offset = idx * buf_config_.buff_size;
            entry->e.length = buf_config_.buff_size;
            entry->e.flags = 0;
            entry->e.cookie =
                reinterpret_cast<uintptr_t>(mapped_) + entry->e.offset;
            tx_available_.push_front(std::move(entry));
          }

          // register waiter when rx fifo is hit
          fifo_data_wait_.set_object(rx_.get_handle());
          fifo_data_wait_.set_trigger(ZX_FIFO_READABLE | ZX_FIFO_PEER_CLOSED |
                                      ETH_SIGNAL_STATUS);
          WaitOnFifoData();
          callback(ZX_OK);
        });
  }

  fzl::fifo<ZFifoEntry>& tx_fifo() { return tx_; }
  fzl::fifo<ZFifoEntry>& rx_fifo() { return rx_; }

  ZFifoEntry* GetTxBuffer() {
    auto ptr = tx_available_.pop_front();
    ZFifoEntry* entry = nullptr;
    if (ptr != nullptr) {
      entry = &ptr->e;
      tx_pending_.push_front(std::move(ptr));
      entry->length = buf_config_.buff_size;
    }
    return entry;
  }

  uint8_t* GetRxBuffer(uint32_t offset) {
    return reinterpret_cast<uint8_t*>(mapped_) + offset;
  }

  void ReturnTxBuffer(ZFifoEntry* entry) {
    auto ptr = tx_pending_.erase_if([entry](const LLFifoEntry& tx_entry) {
      return tx_entry.e.cookie == entry->cookie;
    });
    if (ptr != nullptr) {
      tx_available_.push_front(std::move(ptr));
    }
  }

  void ReturnSentTxBuffers() {
    zx_signals_t obs;
    while (tx_.wait_one(ZX_FIFO_READABLE, zx::time(), &obs) == ZX_OK) {
      if (!(obs & ZX_FIFO_READABLE)) {
        break;
      }
      ZFifoEntry return_entry;
      if (tx_.read_one(&return_entry) != ZX_OK) {
        break;
      }
      if (!(return_entry.flags & ETH_FIFO_TX_OK)) {
        break;
      }
      ReturnTxBuffer(&return_entry);
    }
  }

  void WaitOnFifoData() {
    zx_status_t status = fifo_data_wait_.Begin(dispatcher_);
    if (status != ZX_OK) {
      fprintf(stderr, "EthernetClient can't wait on fifo data: %s\n",
              zx_status_get_string(status));
    }
  }

  void OnRxData(async_dispatcher_t* dispatcher, async::WaitBase* wait,
                zx_status_t status, const zx_packet_signal_t* signal) {
    if (status != ZX_OK) {
      fprintf(stderr, "EthernetClient fifo rx failed: %s\n",
              zx_status_get_string(status));
      return;
    }

    if (link_signal_callback_ && (signal->observed & ETH_SIGNAL_STATUS)) {
      link_signal_callback_();
    }

    if (signal->observed & ZX_FIFO_READABLE) {
      ZFifoEntry entry;
      status = rx_.read_one(&entry);
      if (status != ZX_OK) {
        fprintf(stderr, "Ethernet client fifo rx read failed: %s\n",
                zx_status_get_string(status));
        return;
      }

      auto buf_data = GetRxBuffer(entry.offset);
      // call out to listener
      if (data_callback_) {
        data_callback_(buf_data, entry.length);
      }

      // return buffer to the driver
      entry.length = buf_config_.buff_size;
      status = rx_.write_one(entry);
      if (status != ZX_OK) {
        fprintf(stderr, "Ethernet client can't return rx buffer %s\n",
                zx_status_get_string(status));
      }
    }

    if (signal->observed & ZX_FIFO_PEER_CLOSED) {
      if (peer_closed_callback_) {
        peer_closed_callback_();
      }
    } else {
      WaitOnFifoData();
    }
  }

  void SetDataCallback(EthernetClient::DataCallback cb) {
    data_callback_ = std::move(cb);
  }

  void SetPeerClosedCallback(EthernetClient::PeerClosedCallback cb) {
    peer_closed_callback_ = std::move(cb);
  }

  void SetLinkSignalCallback(fit::closure cb) {
    link_signal_callback_ = std::move(cb);
  }

 private:
  async_dispatcher_t* dispatcher_;
  uint64_t vmo_size_ = 0;
  zx::vmo buf_;
  uintptr_t mapped_ = 0;
  EthernetConfig buf_config_{};
  EthernetClient::DataCallback data_callback_;
  EthernetClient::PeerClosedCallback peer_closed_callback_;
  fit::closure link_signal_callback_;
  fzl::fifo<ZFifoEntry> tx_;
  fzl::fifo<ZFifoEntry> rx_;
  fbl::SinglyLinkedList<fbl::unique_ptr<LLFifoEntry>> tx_available_;
  fbl::SinglyLinkedList<fbl::unique_ptr<LLFifoEntry>> tx_pending_;
  async::WaitMethod<FifoHolder, &FifoHolder::OnRxData> fifo_data_wait_{this};
};

static zx_status_t WatchCb(int dirfd, int event, const char* fn, void* cookie) {
  if (event != WATCH_EVENT_ADD_FILE) {
    return ZX_OK;
  } else if (!strcmp(fn, ".") || !strcmp(fn, "..")) {
    return ZX_OK;
  }

  auto args = reinterpret_cast<WatchCbArgs*>(cookie);

  zx::channel svc;
  {
    int devfd = openat(dirfd, fn, O_RDONLY);
    if (devfd < 0) {
      return ZX_OK;
    }

    zx_status_t status =
        fdio_get_service_handle(devfd, svc.reset_and_get_address());
    if (status != ZX_OK) {
      return status;
    }
  }

  // See if this device is our ethertap device

  fidl::InterfaceHandle<ZDevice> handle;
  handle.set_channel(std::move(svc));
  fidl::SynchronousInterfacePtr<ZDevice> iface = handle.BindSync();

  fuchsia::hardware::ethernet::Info info;
  zx_status_t status = iface->GetInfo(&info);
  if (status != ZX_OK) {
    fprintf(stderr, "could not get ethernet info for %s/%s: %s\n",
            args->base_dir.c_str(), fn, zx_status_get_string(status));
    // Return ZX_OK to keep watching for devices.
    return ZX_OK;
  }
  if (!(info.features & fuchsia::hardware::ethernet::INFO_FEATURE_SYNTH)) {
    // Not a match, keep looking.
    return ZX_OK;
  }
  if (memcmp(args->search_mac.octets.data(), info.mac.octets.data(),
             args->search_mac.octets.size()) != 0) {
    // not a match, keep looking
    return ZX_OK;
  }

  std::ostringstream ss;
  ss << args->base_dir << "/" << fn;
  args->result = ss.str();
  return ZX_ERR_STOP;
}

void EthernetClient::Setup(const EthernetConfig& config,
                           fit::function<void(zx_status_t)> callback) {
  device_->SetClientName("EthernetClient", [](zx_status_t stat) {});
  device_->GetFifos([this, callback = std::move(callback), config](
                        zx_status_t status,
                        std::unique_ptr<ZFifos> fifos) mutable {
    if (status != ZX_OK) {
      callback(status);
      return;
    }
    fifos_ =
        std::make_unique<FifoHolder>(dispatcher_, std::move(fifos), config);

    fifos_->SetPeerClosedCallback([this]() {
      if (peer_closed_callback_) {
        peer_closed_callback_();
      }
    });

    fifos_->SetLinkSignalCallback([this]() {
      device_->GetStatus([this](int32_t status) {
        set_online(
            (status & fuchsia::hardware::ethernet::DEVICE_STATUS_ONLINE) != 0);
      });
    });

    fifos_->Startup(this->device_, [this, callback = std::move(callback)](
                                       zx_status_t status) mutable {
      if (status != ZX_OK) {
        callback(status);
        return;
      }

      this->device_->Start(std::move(callback));
    });
  });
}

EthernetClient::EthernetClient(
    async_dispatcher_t* dispatcher,

    fidl::InterfacePtr<fuchsia::hardware::ethernet::Device> ptr)
    : dispatcher_(dispatcher), online_(false), device_(std::move(ptr)) {
  device_.set_error_handler([this](zx_status_t status) {
    fprintf(stderr, "EthernetClient error = %s\n",
            zx_status_get_string(status));
    if (peer_closed_callback_) {
      peer_closed_callback_();
    }
  });
}

EthernetClient::~EthernetClient() {
  if (device_) {
    device_.Unbind().BindSync()->Stop();
  }
}

zx_status_t EthernetClient::Send(const void* data, uint16_t len) {
  return AcquireAndSend([data, len](void* dst, uint16_t* dlen) {
    if (len < *dlen) {
      *dlen = len;
    }
    memcpy(dst, data, *dlen);
  });
}

zx_status_t EthernetClient::AcquireAndSend(
    fit::function<void(void*, uint16_t*)> writer) {
  fifos_->ReturnSentTxBuffers();
  auto txBuffer = fifos_->GetTxBuffer();
  if (!txBuffer) {
    return ZX_ERR_NO_RESOURCES;
  }
  writer(reinterpret_cast<void*>(txBuffer->cookie), &txBuffer->length);
  auto status = fifos_->tx_fifo().write_one(*txBuffer);
  if (status != ZX_OK) {
    return status;
  }
  return status;
}

void EthernetClient::SetDataCallback(EthernetClient::DataCallback cb) {
  fifos_->SetDataCallback(std::move(cb));
}

void EthernetClient::SetPeerClosedCallback(PeerClosedCallback cb) {
  peer_closed_callback_ = std::move(cb);
}

void EthernetClient::SetLinkStatusChangedCallback(
    LinkStatusChangedCallback cb) {
  link_status_changed_callback_ = std::move(cb);
}

bool EthernetClient::online() const { return online_; }

void EthernetClient::set_online(bool online) {
  if (online != online_) {
    online_ = online;
    if (link_status_changed_callback_) {
      link_status_changed_callback_(online_);
    }
  }
}

std::string EthernetClientFactory::MountPointWithMAC(
    const EthernetClient::Mac& mac, unsigned int deadline_ms) {
  WatchCbArgs args{.base_dir = base_dir_, .search_mac = mac};

  int ethdir = open(base_dir_.c_str(), O_RDONLY);
  if (ethdir < 0) {
    fprintf(stderr, "could not open %s: %s\n", base_dir_.c_str(),
            strerror(errno));
    return nullptr;
  }

  zx_status_t status;
  status = fdio_watch_directory(ethdir, WatchCb,
                                zx_deadline_after(ZX_MSEC(deadline_ms)),
                                reinterpret_cast<void*>(&args));
  close(ethdir);
  if (status == ZX_ERR_STOP) {
    return std::move(args.result);
  } else {
    return std::string();  // empty string if not found
  }
}

EthernetClient::Ptr EthernetClientFactory::RetrieveWithMAC(
    const EthernetClient::Mac& mac, unsigned int deadline_ms,
    async_dispatcher_t* dispatcher) {
  if (dispatcher == nullptr) {
    dispatcher = async_get_default_dispatcher();
  }

  auto mount = MountPointWithMAC(mac, deadline_ms);
  if (mount.empty()) {
    return nullptr;
  }

  int devfd = open(mount.c_str(), O_RDONLY);
  if (devfd < 0) {
    fprintf(stderr, "could not open %s: %s\n", mount.c_str(), strerror(errno));
    return nullptr;
  }

  zx::channel svc;
  {
    zx_status_t status =
        fdio_get_service_handle(devfd, svc.reset_and_get_address());
    if (status != ZX_OK) {
      return nullptr;
    }
  }

  fidl::InterfaceHandle<ZDevice> handle;
  handle.set_channel(std::move(svc));

  return std::make_unique<EthernetClient>(dispatcher, handle.Bind());
}

}  // namespace netemul
