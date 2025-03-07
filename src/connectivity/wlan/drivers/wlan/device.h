// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_WLAN_DRIVERS_WLAN_DEVICE_H_
#define SRC_CONNECTIVITY_WLAN_DRIVERS_WLAN_DEVICE_H_

#include <ddk/driver.h>
#include <ddktl/protocol/ethernet.h>
#include <fbl/intrusive_double_list.h>
#include <fbl/ref_ptr.h>
#include <fbl/slab_allocator.h>
#include <fbl/unique_ptr.h>
#include <fuchsia/wlan/minstrel/cpp/fidl.h>
#include <lib/zx/channel.h>
#include <lib/zx/port.h>
#include <wlan/common/macaddr.h>
#include <wlan/mlme/device_interface.h>
#include <wlan/mlme/dispatcher.h>
#include <wlan/mlme/packet.h>
#include <wlan/mlme/timer.h>
#include <zircon/compiler.h>

#include <mutex>
#include <thread>
#include <tuple>
#include <unordered_set>

#include "lib/svc/cpp/services.h"
#include "minstrel.h"
#include "proxy_helpers.h"

typedef struct zx_port_packet zx_port_packet_t;

namespace wlan {

class Device : public DeviceInterface {
   public:
    Device(zx_device_t* device, wlanmac_protocol_t wlanmac_proto,
           std::shared_ptr<component::Services> services);
    ~Device();

    zx_status_t Bind();

    // ddk device methods
    void WlanUnbind();
    void WlanRelease();
    zx_status_t WlanIoctl(uint32_t op, const void* in_buf, size_t in_len, void* out_buf,
                          size_t out_len, size_t* out_actual);
    void EthUnbind();
    void EthRelease();

    // ddk wlanmac_ifc_t methods
    void WlanmacStatus(uint32_t status);
    void WlanmacRecv(uint32_t flags, const void* data, size_t length, wlan_rx_info_t* info);
    void WlanmacCompleteTx(wlan_tx_packet_t* pkt, zx_status_t status);
    void WlanmacIndication(uint32_t ind);
    void WlanmacReportTxStatus(const wlan_tx_status_t* tx_status);
    void WlanmacHwScanComplete(const wlan_hw_scan_result_t* result);

    // ddk ethmac_protocol_ops methods
    zx_status_t EthmacQuery(uint32_t options, ethmac_info_t* info);
    zx_status_t EthmacStart(const ethmac_ifc_protocol_t* ifc) __TA_EXCLUDES(lock_);
    void EthmacStop() __TA_EXCLUDES(lock_);
    zx_status_t EthmacQueueTx(uint32_t options, ethmac_netbuf_t* netbuf);
    zx_status_t EthmacSetParam(uint32_t param, int32_t value, const void* data, size_t data_size);

    // DeviceInterface methods
    zx_status_t GetTimer(uint64_t id, fbl::unique_ptr<Timer>* timer) override final;
    zx_status_t DeliverEthernet(Span<const uint8_t> eth_frame) override final;
    zx_status_t SendWlan(fbl::unique_ptr<Packet> packet, uint32_t flags) override final;
    zx_status_t SendService(Span<const uint8_t> span) override final;
    zx_status_t SetChannel(wlan_channel_t chan) override final;
    zx_status_t SetStatus(uint32_t status) override final;
    zx_status_t ConfigureBss(wlan_bss_config_t* cfg) override final;
    zx_status_t EnableBeaconing(wlan_bcn_config_t* bcn_cfg) override final;
    zx_status_t ConfigureBeacon(fbl::unique_ptr<Packet> beacon) override final;
    zx_status_t SetKey(wlan_key_config_t* key_config) override final;
    zx_status_t StartHwScan(const wlan_hw_scan_config_t* scan_config) override final;
    zx_status_t ConfigureAssoc(wlan_assoc_ctx_t* assoc_ctx) override final;
    fbl::RefPtr<DeviceState> GetState() override final;
    zx_status_t ClearAssoc(const common::MacAddr& peer_addr) override final;
    const wlanmac_info_t& GetWlanInfo() const override final;
    zx_status_t GetMinstrelPeers(::fuchsia::wlan::minstrel::Peers* peers_fidl) override final;
    zx_status_t GetMinstrelStats(const common::MacAddr& addr,
                                 ::fuchsia::wlan::minstrel::Peer* peer_fidl) override final;

   private:
    struct TimerSchedulerImpl : public TimerScheduler {
        Device* device_;
        std::unordered_set<uint64_t> scheduled_timers_ = {};

        explicit TimerSchedulerImpl(Device* device) : device_(device) {}

        zx_status_t Schedule(Timer* timer, zx::time deadline) override;
        zx_status_t Cancel(Timer*) override;
    };
    enum class DevicePacket : uint64_t {
        kShutdown,
        kPacketQueued,
        kIndication,
        kHwScanComplete,
    };

    zx_status_t AddWlanDevice();
    zx_status_t AddEthDevice(zx_device* parent);

    fbl::unique_ptr<Packet> PreparePacket(const void* data, size_t length, Packet::Peer peer);
    template <typename T>
    fbl::unique_ptr<Packet> PreparePacket(const void* data, size_t length, Packet::Peer peer,
                                          const T& ctrl_data) {
        auto packet = PreparePacket(data, length, peer);
        if (packet != nullptr) { packet->CopyCtrlFrom(ctrl_data); }
        return packet;
    }

    zx_status_t QueuePacket(fbl::unique_ptr<Packet> packet) __TA_EXCLUDES(packet_queue_lock_);

    // Waits the main loop to finish and frees itself afterwards.
    void DestroySelf();
    void MainLoop();
    // Informs the message loop to shut down. Calling this function more than once has no effect.
    void ShutdownMainLoop();
    void ProcessChannelPacketLocked(uint64_t signal_count) __TA_REQUIRES(lock_);
    zx_status_t RegisterChannelWaitLocked() __TA_REQUIRES(lock_);
    // Queue a packet that does not contain user data, either there is no user data or user data is
    // too large and needs to be enqueued into packet_queue_ separately.
    zx_status_t QueueDevicePortPacket(DevicePacket id, uint32_t status = 0);

    zx_status_t GetChannel(zx::channel* out) __TA_EXCLUDES(lock_);

    void SetStatusLocked(uint32_t status);
    bool ShouldEnableMinstrel();
    zx_status_t CreateMinstrel(uint32_t features);
    void AddMinstrelPeer(const wlan_assoc_ctx_t& assoc_ctx);

    zx_device_t* parent_ = nullptr;
    zx_device_t* zxdev_ = nullptr;
    zx_device_t* ethdev_ = nullptr;

    WlanmacProxy wlanmac_proxy_;
    fbl::unique_ptr<ddk::EthmacIfcProtocolClient> ethmac_proxy_;

    wlanmac_info_t wlanmac_info_ = {};
    fbl::RefPtr<DeviceState> state_;

    std::mutex lock_;
    std::thread work_thread_;
    zx::port port_;

    fbl::unique_ptr<MinstrelRateSelector> minstrel_;

    std::shared_ptr<component::Services> services_;

    fbl::unique_ptr<Dispatcher> dispatcher_ __TA_GUARDED(lock_);

    bool dead_ __TA_GUARDED(lock_) = false;
    zx::channel channel_ __TA_GUARDED(lock_);

    std::mutex packet_queue_lock_;
    PacketQueue packet_queue_ __TA_GUARDED(packet_queue_lock_);
    std::vector<uint8_t> fidl_msg_buf_ __TA_GUARDED(lock_);
    TimerSchedulerImpl timer_scheduler_;
};

zx_status_t ValidateWlanMacInfo(const wlanmac_info& wlanmac_info);

}  // namespace wlan

#endif  // SRC_CONNECTIVITY_WLAN_DRIVERS_WLAN_DEVICE_H_
