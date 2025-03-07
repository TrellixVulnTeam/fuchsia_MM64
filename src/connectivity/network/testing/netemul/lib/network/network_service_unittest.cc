// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/testing/test_with_environment.h>
#include <src/connectivity/network/testing/netemul/lib/network/ethernet_client.h>
#include <src/connectivity/network/testing/netemul/lib/network/fake_endpoint.h>
#include <src/connectivity/network/testing/netemul/lib/network/netdump.h>
#include <src/connectivity/network/testing/netemul/lib/network/netdump_parser.h>
#include <src/connectivity/network/testing/netemul/lib/network/network_context.h>

#include <unordered_set>

#define ASSERT_OK(st) ASSERT_EQ(ZX_OK, (st))
#define ASSERT_NOK(st) ASSERT_NE(ZX_OK, (st))

#define TEST_BUF_SIZE (512ul)
#define WAIT_FOR_OK(ok) \
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&ok]() { return ok; }, zx::sec(2)))
#define WAIT_FOR_OK_AND_RESET(ok) \
  WAIT_FOR_OK(ok);                \
  ok = false

namespace netemul {
namespace testing {

static const EthernetConfig TestEthBuffConfig = {.buff_size = 512, .nbufs = 10};

using sys::testing::EnclosingEnvironment;
using sys::testing::EnvironmentServices;
using sys::testing::TestWithEnvironment;
class NetworkServiceTest : public TestWithEnvironment {
 public:
  using FNetworkManager = NetworkManager::FNetworkManager;
  using FEndpointManager = EndpointManager::FEndpointManager;
  using FNetworkContext = NetworkContext::FNetworkContext;
  using FNetwork = Network::FNetwork;
  using FEndpoint = Endpoint::FEndpoint;
  using FFakeEndpoint = FakeEndpoint::FFakeEndpoint;
  using NetworkSetup = NetworkContext::NetworkSetup;
  using EndpointSetup = NetworkContext::EndpointSetup;
  using LossConfig = fuchsia::netemul::network::LossConfig;
  using ReorderConfig = fuchsia::netemul::network::ReorderConfig;
  using LatencyConfig = fuchsia::netemul::network::LatencyConfig;

 protected:
  void SetUp() override {
    fuchsia::sys::EnvironmentPtr parent_env;
    real_services()->Connect(parent_env.NewRequest());

    svc_loop_ =
        std::make_unique<async::Loop>(&kAsyncLoopConfigNoAttachToThread);
    ASSERT_OK(svc_loop_->StartThread("testloop"));
    svc_ = std::make_unique<NetworkContext>(svc_loop_->dispatcher());

    auto services =
        EnvironmentServices::Create(parent_env, svc_loop_->dispatcher());

    services->AddService(svc_->GetHandler());
    test_env_ = CreateNewEnclosingEnvironment("env", std::move(services));

    ASSERT_TRUE(WaitForEnclosingEnvToStart(test_env_.get()));
  }

  void GetNetworkManager(fidl::InterfaceRequest<FNetworkManager> nm) {
    fidl::InterfacePtr<FNetworkContext> netc;
    test_env_->ConnectToService(netc.NewRequest());
    netc->GetNetworkManager(std::move(nm));
  }

  void GetEndpointManager(fidl::InterfaceRequest<FEndpointManager> epm) {
    fidl::InterfacePtr<FNetworkContext> netc;
    test_env_->ConnectToService(netc.NewRequest());
    netc->GetEndpointManager(std::move(epm));
  }

  Endpoint::Config GetDefaultEndpointConfig() {
    Endpoint::Config ret;
    ret.mtu = 1500;
    ret.backing = fuchsia::netemul::network::EndpointBacking::ETHERTAP;
    return ret;
  }

  void GetServices(fidl::InterfaceRequest<FNetworkManager> nm,
                   fidl::InterfaceRequest<FEndpointManager> epm) {
    fidl::InterfacePtr<FNetworkContext> netc;
    GetNetworkContext(netc.NewRequest());
    netc->GetNetworkManager(std::move(nm));
    netc->GetEndpointManager(std::move(epm));
  }

  void StartServices() {
    GetServices(net_manager_.NewRequest(), endp_manager_.NewRequest());
  }

  void GetNetworkContext(
      fidl::InterfaceRequest<NetworkContext::FNetworkContext> req) {
    test_env_->ConnectToService(std::move(req));
  }

  void CreateNetwork(const char* name,
                     fidl::SynchronousInterfacePtr<FNetwork>* netout,
                     Network::Config config = Network::Config()) {
    ASSERT_TRUE(net_manager_.is_bound());

    zx_status_t status;
    fidl::InterfaceHandle<FNetwork> neth;
    ASSERT_OK(
        net_manager_->CreateNetwork(name, std::move(config), &status, &neth));
    ASSERT_OK(status);
    ASSERT_TRUE(neth.is_valid());

    *netout = neth.BindSync();
  }

  void CreateEndpoint(const char* name,
                      fidl::SynchronousInterfacePtr<FEndpoint>* netout,
                      Endpoint::Config config) {
    ASSERT_TRUE(net_manager_.is_bound());

    zx_status_t status;
    fidl::InterfaceHandle<FEndpoint> eph;
    ASSERT_OK(
        endp_manager_->CreateEndpoint(name, std::move(config), &status, &eph));
    ASSERT_OK(status);
    ASSERT_TRUE(eph.is_valid());

    *netout = eph.BindSync();
  }

  void CreateEndpoint(const char* name,
                      fidl::SynchronousInterfacePtr<FEndpoint>* netout) {
    CreateEndpoint(name, netout, GetDefaultEndpointConfig());
  }

  void CreateSimpleNetwork(
      Network::Config config,
      fidl::InterfaceHandle<NetworkContext::FSetupHandle>* setup_handle,
      std::unique_ptr<EthernetClient>* eth1,
      std::unique_ptr<EthernetClient>* eth2) {
    fidl::SynchronousInterfacePtr<FNetworkContext> context;
    GetNetworkContext(context.NewRequest());
    zx_status_t status;
    std::vector<NetworkSetup> net_setup;
    auto& net1 = net_setup.emplace_back();
    net1.name = "net";
    net1.config = std::move(config);
    auto& ep1_setup = net1.endpoints.emplace_back();
    ep1_setup.name = "ep1";
    ep1_setup.link_up = true;
    auto& ep2_setup = net1.endpoints.emplace_back();
    ep2_setup.name = "ep2";
    ep2_setup.link_up = true;

    ASSERT_OK(context->Setup(std::move(net_setup), &status, setup_handle));
    ASSERT_OK(status);
    fidl::InterfaceHandle<Endpoint::FEndpoint> ep1_handle, ep2_handle;
    ASSERT_OK(endp_manager_->GetEndpoint("ep1", &ep1_handle));
    ASSERT_OK(endp_manager_->GetEndpoint("ep2", &ep2_handle));
    ASSERT_TRUE(ep1_handle.is_valid());
    ASSERT_TRUE(ep2_handle.is_valid());

    auto ep1 = ep1_handle.BindSync();
    auto ep2 = ep2_handle.BindSync();
    // start ethernet clients on both endpoints:
    fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> eth1_h;
    fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> eth2_h;
    ASSERT_OK(ep1->GetEthernetDevice(&eth1_h));
    ASSERT_TRUE(eth1_h.is_valid());
    ASSERT_OK(ep2->GetEthernetDevice(&eth2_h));
    ASSERT_TRUE(eth2_h.is_valid());
    // create both ethernet clients
    *eth1 = std::make_unique<EthernetClient>(dispatcher(), eth1_h.Bind());
    *eth2 = std::make_unique<EthernetClient>(dispatcher(), eth2_h.Bind());

    bool eth_ready = false;
    // configure both ethernet clients:
    (*eth1)->Setup(TestEthBuffConfig, [&eth_ready](zx_status_t status) {
      ASSERT_OK(status);
      eth_ready = true;
    });
    WAIT_FOR_OK_AND_RESET(eth_ready);
    (*eth2)->Setup(TestEthBuffConfig, [&eth_ready](zx_status_t status) {
      ASSERT_OK(status);
      eth_ready = true;
    });
    WAIT_FOR_OK_AND_RESET(eth_ready);

    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&eth1, &eth2]() { return (*eth1)->online() && (*eth2)->online(); },
        zx::sec(2)));
  }

  void TearDown() override {
    async::PostTask(svc_loop_->dispatcher(), [this]() {
      svc_.reset();
      svc_loop_->Quit();
    });
    svc_loop_->JoinThreads();
  }

  std::unique_ptr<EnclosingEnvironment> test_env_;
  std::unique_ptr<async::Loop> svc_loop_;
  std::unique_ptr<NetworkContext> svc_;
  fidl::SynchronousInterfacePtr<FNetworkManager> net_manager_;
  fidl::SynchronousInterfacePtr<FEndpointManager> endp_manager_;
};

TEST_F(NetworkServiceTest, NetworkLifecycle) {
  fidl::SynchronousInterfacePtr<FNetworkManager> netm;
  GetNetworkManager(netm.NewRequest());

  const char* netname = "mynet";

  std::vector<std::string> nets;
  ASSERT_OK(netm->ListNetworks(&nets));
  ASSERT_EQ(0ul, nets.size());
  Network::Config config;
  zx_status_t status;
  fidl::InterfaceHandle<FNetwork> neth;
  // can create network ok
  ASSERT_OK(netm->CreateNetwork(fidl::StringPtr(netname), std::move(config),
                                &status, &neth));
  auto net = neth.BindSync();
  ASSERT_OK(status);
  ASSERT_TRUE(net.is_bound());

  // list nets again and make sure it's there:
  ASSERT_OK(netm->ListNetworks(&nets));
  ASSERT_EQ(1ul, nets.size());
  ASSERT_EQ(netname, nets.at(0));

  // check network name matches:
  std::string outname;
  ASSERT_OK(net->GetName(&outname));
  ASSERT_EQ(netname, outname);

  // check that we can fetch the network by name:
  fidl::InterfaceHandle<FNetwork> ohandle;
  ASSERT_OK(netm->GetNetwork(netname, &ohandle));
  ASSERT_TRUE(ohandle.is_valid());
  // dispose of second handle
  ohandle.TakeChannel().reset();

  // check that network still exists:
  ASSERT_OK(netm->ListNetworks(&nets));
  ASSERT_EQ(1ul, nets.size());

  // destroy original network handle:
  net.Unbind().TakeChannel().reset();
  // make sure network is deleted afterwards:
  ASSERT_OK(netm->ListNetworks(&nets));
  ASSERT_EQ(0ul, nets.size());

  // trying to get the network again without creating it fails:
  ASSERT_OK(netm->GetNetwork(netname, &ohandle));
  ASSERT_FALSE(ohandle.is_valid());
}

TEST_F(NetworkServiceTest, EndpointLifecycle) {
  fidl::SynchronousInterfacePtr<FEndpointManager> epm;
  GetEndpointManager(epm.NewRequest());

  const char* epname = "myendpoint";

  std::vector<std::string> eps;
  ASSERT_OK(epm->ListEndpoints(&eps));
  ASSERT_EQ(0ul, eps.size());
  auto config = GetDefaultEndpointConfig();
  zx_status_t status;
  fidl::InterfaceHandle<FEndpoint> eph;
  // can create endpoint ok
  ASSERT_OK(epm->CreateEndpoint(fidl::StringPtr(epname), std::move(config),
                                &status, &eph));
  auto ep = eph.BindSync();
  ASSERT_OK(status);
  ASSERT_TRUE(ep.is_bound());

  // list endpoints again and make sure it's there:
  ASSERT_OK(epm->ListEndpoints(&eps));
  ASSERT_EQ(1ul, eps.size());
  ASSERT_EQ(epname, eps.at(0));

  // check endpoint name matches:
  std::string outname;
  ASSERT_OK(ep->GetName(&outname));
  ASSERT_EQ(epname, outname);

  // check that we can fetch the endpoint by name:
  fidl::InterfaceHandle<FEndpoint> ohandle;
  ASSERT_OK(epm->GetEndpoint(epname, &ohandle));
  ASSERT_TRUE(ohandle.is_valid());
  // dispose of second handle
  ohandle.TakeChannel().reset();

  // check that endpoint still exists:
  ASSERT_OK(epm->ListEndpoints(&eps));
  ASSERT_EQ(1ul, eps.size());

  // destroy original endpoint handle:
  ep.Unbind().TakeChannel().reset();
  // make sure endpoint is deleted afterwards:
  ASSERT_OK(epm->ListEndpoints(&eps));
  ASSERT_EQ(0ul, eps.size());

  // trying to get the endpoint again without creating it fails:
  ASSERT_OK(epm->GetEndpoint(epname, &ohandle));
  ASSERT_FALSE(ohandle.is_valid());
}

TEST_F(NetworkServiceTest, BadEndpointConfigurations) {
  fidl::SynchronousInterfacePtr<FEndpointManager> epm;
  GetEndpointManager(epm.NewRequest());

  const char* epname = "myendpoint";

  zx_status_t status;
  fidl::InterfaceHandle<FEndpoint> eph;
  // can't create endpoint with empty name
  ASSERT_OK(epm->CreateEndpoint("", GetDefaultEndpointConfig(), &status, &eph));
  ASSERT_NOK(status);
  ASSERT_FALSE(eph.is_valid());

  // can't create endpoint with unexisting backing
  auto badBacking = GetDefaultEndpointConfig();
  badBacking.backing =
      static_cast<fuchsia::netemul::network::EndpointBacking>(-1);
  ASSERT_OK(epm->CreateEndpoint(epname, std::move(badBacking), &status, &eph));
  ASSERT_NOK(status);
  ASSERT_FALSE(eph.is_valid());

  // can't create endpoint which violates maximum MTU
  auto badMtu = GetDefaultEndpointConfig();
  badMtu.mtu = 65535;  // 65k too large
  ASSERT_OK(epm->CreateEndpoint(epname, std::move(badMtu), &status, &eph));
  ASSERT_NOK(status);
  ASSERT_FALSE(eph.is_valid());

  // create a good endpoint:
  fidl::InterfaceHandle<FEndpoint> good_eph;
  ASSERT_OK(epm->CreateEndpoint(epname, GetDefaultEndpointConfig(), &status,
                                &good_eph));
  ASSERT_OK(status);
  ASSERT_TRUE(good_eph.is_valid());
  // can't create another endpoint with same name:
  ASSERT_OK(
      epm->CreateEndpoint(epname, GetDefaultEndpointConfig(), &status, &eph));
  ASSERT_NOK(status);
  ASSERT_FALSE(eph.is_valid());
}

TEST_F(NetworkServiceTest, BadNetworkConfigurations) {
  fidl::SynchronousInterfacePtr<FNetworkManager> netm;
  GetNetworkManager(netm.NewRequest());

  zx_status_t status;
  fidl::InterfaceHandle<FNetwork> neth;
  // can't create network with empty name
  ASSERT_OK(netm->CreateNetwork("", Network::Config(), &status, &neth));
  ASSERT_NOK(status);
  ASSERT_FALSE(neth.is_valid());

  const char* netname = "mynet";

  // create a good network
  fidl::InterfaceHandle<FNetwork> good_neth;
  ASSERT_OK(
      netm->CreateNetwork(netname, Network::Config(), &status, &good_neth));
  ASSERT_OK(status);
  ASSERT_TRUE(good_neth.is_valid());

  // can't create another network with same name:
  ASSERT_OK(netm->CreateNetwork(netname, Network::Config(), &status, &neth));
  ASSERT_NOK(status);
  ASSERT_FALSE(neth.is_valid());
}

TEST_F(NetworkServiceTest, TransitData) {
  const char* netname = "mynet";
  const char* ep1name = "ep1";
  const char* ep2name = "ep2";
  StartServices();

  // create a network:
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // create first endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep1;
  CreateEndpoint(ep1name, &ep1);

  // create second endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep2;
  CreateEndpoint(ep2name, &ep2);
  ASSERT_OK(ep1->SetLinkUp(true));
  ASSERT_OK(ep2->SetLinkUp(true));

  // attach both endpoints:
  zx_status_t status;
  ASSERT_OK(net->AttachEndpoint(ep1name, &status));
  ASSERT_OK(status);
  ASSERT_OK(net->AttachEndpoint(ep2name, &status));
  ASSERT_OK(status);

  // start ethernet clients on both endpoints:
  fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> eth1_h;
  fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> eth2_h;
  ASSERT_OK(ep1->GetEthernetDevice(&eth1_h));
  ASSERT_TRUE(eth1_h.is_valid());
  ASSERT_OK(ep2->GetEthernetDevice(&eth2_h));
  ASSERT_TRUE(eth2_h.is_valid());
  // create both ethernet clients
  EthernetClient eth1(dispatcher(), eth1_h.Bind());
  EthernetClient eth2(dispatcher(), eth2_h.Bind());
  bool ok = false;

  // configure both ethernet clients:
  eth1.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  eth2.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  // wait for both ethernets to come online
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&eth1, &eth2]() { return eth1.online() && eth2.online(); }, zx::sec(2)));

  // create some test buffs
  uint8_t test_buff1[TEST_BUF_SIZE];
  uint8_t test_buff2[TEST_BUF_SIZE];
  for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
    test_buff1[i] = static_cast<uint8_t>(i);
    test_buff2[i] = ~static_cast<uint8_t>(i);
  }

  // install callbacks on the ethernet interfaces:
  eth1.SetDataCallback([&ok, &test_buff1](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff1, len));
    ok = true;
  });
  eth2.SetDataCallback([&ok, &test_buff2](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff2, len));
    ok = true;
  });

  // send data from eth2 to eth1
  ASSERT_OK(eth2.Send(test_buff1, TEST_BUF_SIZE));
  WAIT_FOR_OK_AND_RESET(ok);

  // send data from eth1 to eth2
  ASSERT_OK(eth1.Send(test_buff2, TEST_BUF_SIZE));
  WAIT_FOR_OK_AND_RESET(ok);

  // try removing an endpoint:
  ASSERT_OK(net->RemoveEndpoint(ep2name, &status));
  ASSERT_OK(status);
  // can still send, but should not trigger anything on the other side:
  ASSERT_OK(eth1.Send(test_buff1, TEST_BUF_SIZE));
  RunLoopUntilIdle();
  ASSERT_FALSE(ok);
}

TEST_F(NetworkServiceTest, Flooding) {
  const char* netname = "mynet";
  const char* ep1name = "ep1";
  const char* ep2name = "ep2";
  const char* ep3name = "ep3";
  StartServices();

  // create a network:
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // create first endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep1;
  CreateEndpoint(ep1name, &ep1);
  // create second endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep2;
  CreateEndpoint(ep2name, &ep2);
  // create a third:
  fidl::SynchronousInterfacePtr<FEndpoint> ep3;
  CreateEndpoint(ep3name, &ep3);
  ASSERT_OK(ep1->SetLinkUp(true));
  ASSERT_OK(ep2->SetLinkUp(true));
  ASSERT_OK(ep3->SetLinkUp(true));

  // attach all three endpoints:
  zx_status_t status;
  ASSERT_OK(net->AttachEndpoint(ep1name, &status));
  ASSERT_OK(status);
  ASSERT_OK(net->AttachEndpoint(ep2name, &status));
  ASSERT_OK(status);
  ASSERT_OK(net->AttachEndpoint(ep3name, &status));
  ASSERT_OK(status);

  // start ethernet clients on all endpoints:
  fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> eth1_h;
  fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> eth2_h;
  fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> eth3_h;
  ASSERT_OK(ep1->GetEthernetDevice(&eth1_h));
  ASSERT_TRUE(eth1_h.is_valid());
  ASSERT_OK(ep2->GetEthernetDevice(&eth2_h));
  ASSERT_TRUE(eth2_h.is_valid());
  ASSERT_OK(ep3->GetEthernetDevice(&eth3_h));
  ASSERT_TRUE(eth3_h.is_valid());
  // create all ethernet clients
  EthernetClient eth1(dispatcher(), eth1_h.Bind());
  EthernetClient eth2(dispatcher(), eth2_h.Bind());
  EthernetClient eth3(dispatcher(), eth3_h.Bind());
  bool ok = false;

  // configure all ethernet clients:
  eth1.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  eth2.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  eth3.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  // Wait for all ethernets to come online
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&eth1, &eth2, &eth3]() {
        return eth1.online() && eth2.online() && eth3.online();
      },
      zx::sec(2)));

  // create a test buff
  uint8_t test_buff[TEST_BUF_SIZE];
  for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
    test_buff[i] = static_cast<uint8_t>(i);
  }

  // install callbacks on the ethernet interfaces:
  bool ok_eth1 = false;
  bool ok_eth2 = false;
  bool ok_eth3 = false;
  eth1.SetDataCallback([&ok_eth1, &test_buff](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff, len));
    ok_eth1 = true;
  });
  eth2.SetDataCallback([&ok_eth2, &test_buff](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff, len));
    ok_eth2 = true;
  });
  eth3.SetDataCallback([&ok_eth3, &test_buff](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff, len));
    ok_eth3 = true;
  });

  for (int i = 0; i < 3; i++) {
    // flood network from eth1:
    ASSERT_OK(eth1.Send(test_buff, TEST_BUF_SIZE));
    // wait for corrrect data on both endpoints:
    WAIT_FOR_OK_AND_RESET(ok_eth2);
    WAIT_FOR_OK_AND_RESET(ok_eth3);
    // eth1 should have received NO data at this point:
    ASSERT_FALSE(ok_eth1);
    // now flood from eth2:
    ASSERT_OK(eth2.Send(test_buff, TEST_BUF_SIZE));
    // wait for corrrect data on both endpoints:
    WAIT_FOR_OK_AND_RESET(ok_eth1);
    WAIT_FOR_OK_AND_RESET(ok_eth3);
    ASSERT_FALSE(ok_eth2);
  }
}

TEST_F(NetworkServiceTest, AttachRemove) {
  const char* netname = "mynet";
  const char* epname = "ep1";
  StartServices();

  // create a network:
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // create an endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep1;
  CreateEndpoint(epname, &ep1);

  // attach endpoint:
  zx_status_t status;
  ASSERT_OK(net->AttachEndpoint(epname, &status));
  ASSERT_OK(status);
  // try to attach again:
  ASSERT_OK(net->AttachEndpoint(epname, &status));
  // should return not OK cause endpoint was already attached:
  ASSERT_NOK(status);

  // remove endpoint:
  ASSERT_OK(net->RemoveEndpoint(epname, &status));
  ASSERT_OK(status);
  // remove endpoint again:
  ASSERT_OK(net->RemoveEndpoint(epname, &status));
  // should return not OK cause endpoint was not attached
  ASSERT_NOK(status);
}

TEST_F(NetworkServiceTest, FakeEndpoints) {
  const char* netname = "mynet";
  const char* epname = "ep1";
  StartServices();

  // create a network:
  fidl::SynchronousInterfacePtr<FNetwork> net;
  CreateNetwork(netname, &net);

  // create first endpoint:
  fidl::SynchronousInterfacePtr<FEndpoint> ep1;
  CreateEndpoint(epname, &ep1);
  ep1->SetLinkUp(true);

  // attach endpoint:
  zx_status_t status;
  ASSERT_OK(net->AttachEndpoint(epname, &status));
  ASSERT_OK(status);

  // start ethernet clients on endpoint:
  fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> eth1_h;
  ASSERT_OK(ep1->GetEthernetDevice(&eth1_h));
  ASSERT_TRUE(eth1_h.is_valid());
  // create ethernet client
  EthernetClient eth1(dispatcher(), eth1_h.Bind());
  bool ok = false;

  // configure ethernet client:
  eth1.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
    ASSERT_OK(status);
    ok = true;
  });
  WAIT_FOR_OK_AND_RESET(ok);
  // and wait for it to come online:
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil([&eth1]() { return eth1.online(); },
                                        zx::sec(2)));

  // create some test buffs
  std::vector<uint8_t> test_buff1(TEST_BUF_SIZE);
  std::vector<uint8_t> test_buff2(TEST_BUF_SIZE);
  test_buff2.reserve(TEST_BUF_SIZE);
  for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
    test_buff1[i] = static_cast<uint8_t>(i);
    test_buff2[i] = ~static_cast<uint8_t>(i);
  }

  // install callbacks on the ethernet interface:
  eth1.SetDataCallback([&ok, &test_buff1](const void* data, size_t len) {
    ASSERT_EQ(TEST_BUF_SIZE, len);
    ASSERT_EQ(0, memcmp(data, test_buff1.data(), len));
    ok = true;
  });

  // create and inject a fake endpoint:
  fidl::InterfacePtr<FFakeEndpoint> fake_ep;
  ASSERT_OK(net->CreateFakeEndpoint(fake_ep.NewRequest()));
  ASSERT_TRUE(fake_ep.is_bound());
  // install on data callback:
  fake_ep.events().OnData = [&ok, &test_buff2](std::vector<uint8_t> data) {
    ASSERT_EQ(TEST_BUF_SIZE, data.size());
    ASSERT_EQ(0, memcmp(data.data(), test_buff2.data(), data.size()));
    ok = true;
  };
  for (int i = 0; i < 3; i++) {
    // send buff 2 from eth endpoint:
    eth1.Send(test_buff2.data(), test_buff2.size());
    WAIT_FOR_OK_AND_RESET(ok);
    // send buff 1 from fake endpoint:
    fake_ep->Write(fidl::VectorPtr<uint8_t>(test_buff1));
    WAIT_FOR_OK_AND_RESET(ok);
  }
}

TEST_F(NetworkServiceTest, NetworkContext) {
  StartServices();
  fidl::SynchronousInterfacePtr<FNetworkContext> context;
  GetNetworkContext(context.NewRequest());

  zx_status_t status;
  fidl::InterfaceHandle<NetworkContext::FSetupHandle> setup_handle;
  std::vector<NetworkSetup> net_setup;
  auto& net1 = net_setup.emplace_back();
  net1.name = "main_net";
  auto& ep1_setup = net1.endpoints.emplace_back();
  ep1_setup.name = "ep1";
  ep1_setup.link_up = true;
  auto& ep2_setup = net1.endpoints.emplace_back();
  ep2_setup.name = "ep2";
  ep2_setup.link_up = true;
  auto& alt_net_setup = net_setup.emplace_back();
  alt_net_setup.name = "alt_net";

  // create two nets and two endpoints:
  ASSERT_OK(context->Setup(std::move(net_setup), &status, &setup_handle));
  ASSERT_OK(status);
  ASSERT_TRUE(setup_handle.is_valid());

  // check that both networks and endpoints were created:
  fidl::InterfaceHandle<Network::FNetwork> network;
  ASSERT_OK(net_manager_->GetNetwork("main_net", &network));
  ASSERT_TRUE(network.is_valid());
  ASSERT_OK(net_manager_->GetNetwork("alt_net", &network));
  ASSERT_TRUE(network.is_valid());
  fidl::InterfaceHandle<Endpoint::FEndpoint> ep1_h, ep2_h;
  ASSERT_OK(endp_manager_->GetEndpoint("ep1", &ep1_h));
  ASSERT_TRUE(ep1_h.is_valid());
  ASSERT_OK(endp_manager_->GetEndpoint("ep2", &ep2_h));
  ASSERT_TRUE(ep2_h.is_valid());

  {
    // check that endpoints were attached to the same network:
    auto ep1 = ep1_h.BindSync();
    auto ep2 = ep2_h.BindSync();
    fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> eth1_h;
    fidl::InterfaceHandle<fuchsia::hardware::ethernet::Device> eth2_h;
    ASSERT_OK(ep1->GetEthernetDevice(&eth1_h));
    ASSERT_TRUE(eth1_h.is_valid());
    ASSERT_OK(ep2->GetEthernetDevice(&eth2_h));
    ASSERT_TRUE(eth2_h.is_valid());
    // create both ethernet clients
    EthernetClient eth1(dispatcher(), eth1_h.Bind());
    EthernetClient eth2(dispatcher(), eth2_h.Bind());
    bool ok = false;

    // configure both ethernet clients:
    eth1.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
      ASSERT_OK(status);
      ok = true;
    });
    WAIT_FOR_OK_AND_RESET(ok);
    eth2.Setup(TestEthBuffConfig, [&ok](zx_status_t status) {
      ASSERT_OK(status);
      ok = true;
    });
    WAIT_FOR_OK_AND_RESET(ok);
    // and wait for them to come online:
    ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
        [&eth1, &eth2]() { return eth1.online() && eth2.online(); },
        zx::sec(2)));

    // create some test buffs
    uint8_t test_buff[TEST_BUF_SIZE];
    for (size_t i = 0; i < TEST_BUF_SIZE; i++) {
      test_buff[i] = static_cast<uint8_t>(i);
    }
    // install callbacks on the ethernet interface:
    eth2.SetDataCallback([&ok, &test_buff](const void* data, size_t len) {
      ASSERT_EQ(TEST_BUF_SIZE, len);
      ASSERT_EQ(0, memcmp(data, test_buff, len));
      ok = true;
    });
    ASSERT_OK(eth1.Send(test_buff, TEST_BUF_SIZE));
    WAIT_FOR_OK_AND_RESET(ok);
  }  // test above performed in closed scope so all bindings are destroyed after
  // it's done

  // check that attempting to setup with repeated network name will fail:
  std::vector<NetworkSetup> repeated_net_name;
  fidl::InterfaceHandle<NetworkContext::FSetupHandle> dummy_handle;
  auto& repeated_cfg = repeated_net_name.emplace_back();
  repeated_cfg.name = "main_net";
  ASSERT_OK(
      context->Setup(std::move(repeated_net_name), &status, &dummy_handle));
  ASSERT_NOK(status);
  ASSERT_FALSE(dummy_handle.is_valid());

  // check that attempting to setup with invalid ep name will fail, and all
  // setup is discarded
  std::vector<NetworkSetup> repeated_ep_name;
  auto& good_net = repeated_ep_name.emplace_back();
  good_net.name = "good_net";
  auto& repeated_ep1_setup = good_net.endpoints.emplace_back();
  repeated_ep1_setup.name = "ep1";

  ASSERT_OK(
      context->Setup(std::move(repeated_ep_name), &status, &dummy_handle));
  ASSERT_NOK(status);
  ASSERT_FALSE(dummy_handle.is_valid());
  ASSERT_OK(net_manager_->GetNetwork("good_net", &network));
  ASSERT_FALSE(network.is_valid());

  // finally, destroy the setup_handle and verify that all the created networks
  // and endpoints go away:
  setup_handle.TakeChannel().reset();

  // wait until |main_net| disappears:
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [this]() {
        fidl::InterfaceHandle<Network::FNetwork> network;
        EXPECT_EQ(ZX_OK, net_manager_->GetNetwork("main_net", &network));
        return !network.is_valid();
      },
      zx::sec(2)));
  // assert that all other networks and endpoints also disappear:
  ASSERT_OK(net_manager_->GetNetwork("alt_net", &network));
  ASSERT_FALSE(network.is_valid());
  ASSERT_OK(endp_manager_->GetEndpoint("ep1", &ep1_h));
  ASSERT_FALSE(ep1_h.is_valid());
  ASSERT_OK(endp_manager_->GetEndpoint("ep2", &ep2_h));
  ASSERT_FALSE(ep2_h.is_valid());
}

TEST_F(NetworkServiceTest, CreateNetworkWithInvalidConfig) {
  StartServices();
  Network::Config config;
  LossConfig loss;
  loss.set_random_rate(101);
  config.set_packet_loss(std::move(loss));
  zx_status_t status;
  fidl::InterfaceHandle<Network::FNetwork> net;
  ASSERT_OK(
      net_manager_->CreateNetwork("net", std::move(config), &status, &net));
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
  ASSERT_FALSE(net.is_valid());
}

TEST_F(NetworkServiceTest, NetworkSetInvalidConfig) {
  StartServices();
  fidl::SynchronousInterfacePtr<Network::FNetwork> net;
  CreateNetwork("net", &net);

  Network::Config config;
  LossConfig loss;
  loss.set_random_rate(101);
  config.set_packet_loss(std::move(loss));
  zx_status_t status;
  ASSERT_OK(net->SetConfig(std::move(config), &status));
  ASSERT_EQ(status, ZX_ERR_INVALID_ARGS);
}

TEST_F(NetworkServiceTest, NetworkConfigChains) {
  StartServices();
  constexpr int packet_count = 3;
  std::unique_ptr<EthernetClient> eth1, eth2;
  fidl::InterfaceHandle<NetworkContext::FSetupHandle> setup_handle;
  Network::Config config;
  config.mutable_packet_loss()->set_random_rate(0);
  config.mutable_latency()->average = 5;
  config.mutable_latency()->std_dev = 0;
  config.mutable_reorder()->store_buff = packet_count;
  config.mutable_reorder()->tick = 0;

  CreateSimpleNetwork(std::move(config), &setup_handle, &eth1, &eth2);
  ASSERT_TRUE(eth1 && eth2);

  std::unordered_set<uint8_t> received;
  zx::time after;
  eth2->SetDataCallback([&received, &after](const void* data, size_t len) {
    EXPECT_EQ(len, 1ul);
    received.insert(*reinterpret_cast<const uint8_t*>(data));
    after = zx::clock::get_monotonic();
  });

  auto bef = zx::clock::get_monotonic();
  for (uint8_t i = 0; i < packet_count; i++) {
    ASSERT_OK(eth1->Send(&i, 1));
  }

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&received]() { return received.size() == packet_count; }, zx::sec(2)));
  for (uint8_t i = 0; i < packet_count; i++) {
    EXPECT_TRUE(received.find(i) != received.end());
  }
  auto diff = (after - bef).to_msecs();
  // Check that measured latency is at least greater than the configured
  // one.
  // We don't do upper bound checking because it's not very CQ friendly.
  EXPECT_TRUE(diff >= 5)
      << "Total latency should be greater than configured latency, but got "
      << diff;
}

TEST_F(NetworkServiceTest, NetworkConfigChanges) {
  StartServices();
  constexpr int reorder_threshold = 3;
  constexpr int packet_count = 5;
  std::unique_ptr<EthernetClient> eth1, eth2;
  fidl::InterfaceHandle<NetworkContext::FSetupHandle> setup_handle;
  Network::Config config;
  config.mutable_reorder()->store_buff = reorder_threshold;
  config.mutable_reorder()->tick = 0;

  // start with creating a network with a reorder threshold lower than the sent
  // packet count
  CreateSimpleNetwork(std::move(config), &setup_handle, &eth1, &eth2);
  ASSERT_TRUE(eth1 && eth2 && setup_handle.is_valid());

  std::unordered_set<uint8_t> received;
  eth2->SetDataCallback([&received](const void* data, size_t len) {
    EXPECT_EQ(len, 1ul);
    received.insert(*reinterpret_cast<const uint8_t*>(data));
  });

  for (uint8_t i = 0; i < packet_count; i++) {
    ASSERT_OK(eth1->Send(&i, 1));
  }

  // wait until |reorder_threshold| is hit
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&received]() { return received.size() == reorder_threshold; },
      zx::sec(2)));
  for (uint8_t i = 0; i < reorder_threshold; i++) {
    EXPECT_TRUE(received.find(i) != received.end());
  }
  received.clear();

  // change the configuration to packet loss 0:
  Network::Config config_packet_loss;
  config_packet_loss.mutable_packet_loss()->set_random_rate(0);
  fidl::InterfaceHandle<Network::FNetwork> net_handle;
  ASSERT_OK(net_manager_->GetNetwork("net", &net_handle));
  ASSERT_TRUE(net_handle.is_valid());
  auto net = net_handle.BindSync();
  zx_status_t status;
  ASSERT_OK(net->SetConfig(std::move(config_packet_loss), &status));
  ASSERT_OK(status);
  // upon changing the configuration, all other remaining packets should've been
  // flushed check that by waiting for the remaining packets: wait until
  // |reorder_threshold| is hit
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&received]() {
        return received.size() == packet_count - reorder_threshold;
      },
      zx::sec(2)));
  for (uint8_t i = reorder_threshold; i < packet_count; i++) {
    EXPECT_TRUE(received.find(i) != received.end());
  }

  received.clear();
  // go again to verify that the configuration changed into packet loss with 0%
  // loss:
  for (uint8_t i = 0; i < packet_count; i++) {
    ASSERT_OK(eth1->Send(&i, 1));
  }
  // wait until |packet_count| is hit
  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&received]() { return received.size() == packet_count; }, zx::sec(2)));
  for (uint8_t i = 0; i < packet_count; i++) {
    EXPECT_TRUE(received.find(i) != received.end());
  }
  received.clear();
}

TEST_F(NetworkServiceTest, NetWatcher) {
  StartServices();

  fidl::SynchronousInterfacePtr<Network::FNetwork> net;
  CreateNetwork("net", &net);

  fidl::InterfacePtr<FakeEndpoint::FFakeEndpoint> fe;
  ASSERT_OK(net->CreateFakeEndpoint(fe.NewRequest()));
  // create net watcher first, so we're guaranteed it'll be there before:
  NetWatcher<InMemoryDump> watcher;
  watcher.Watch("net", std::move(fe));

  fidl::InterfacePtr<FakeEndpoint::FFakeEndpoint> fe_in;
  ASSERT_OK(net->CreateFakeEndpoint(fe_in.NewRequest()));

  constexpr uint32_t packet_count = 10;
  for (uint32_t i = 0; i < packet_count; i++) {
    auto ptr = reinterpret_cast<const uint8_t*>(&i);
    fe_in->Write(std::vector<uint8_t>(ptr, ptr + sizeof(i)));
  }

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&watcher]() { return watcher.dump().packet_count() == packet_count; },
      zx::sec(5)));

  // check that all the saved data is correct:
  NetDumpParser parser;
  auto dump_bytes = watcher.dump().CopyBytes();
  ASSERT_TRUE(parser.Parse(&dump_bytes[0], dump_bytes.size()));
  ASSERT_EQ(parser.interfaces().size(), 1ul);
  ASSERT_EQ(parser.packets().size(), packet_count);

  EXPECT_EQ(parser.interfaces()[0], "net");
  for (uint32_t i = 0; i < packet_count; i++) {
    auto& pkt = parser.packets()[i];
    EXPECT_EQ(pkt.len, sizeof(uint32_t));
    EXPECT_EQ(pkt.interface, 0u);
    EXPECT_EQ(memcmp(pkt.data, &i, sizeof(i)), 0);
  }
}

}  // namespace testing
}  // namespace netemul
