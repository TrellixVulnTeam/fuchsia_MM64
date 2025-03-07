// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/developer/tiles/tiles.h"

#include <fuchsia/developer/tiles/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <fuchsia/ui/scenic/cpp/fidl_test_base.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <gtest/gtest.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/component/cpp/testing/test_with_context.h>
#include <lib/ui/base_view/cpp/base_view.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>

namespace {

class FakeScenic : public fuchsia::ui::scenic::testing::Scenic_TestBase {
 public:
  void NotImplemented_(const std::string& name) final {}

  fidl::InterfaceRequestHandler<fuchsia::ui::scenic::Scenic> GetHandler(
      async_dispatcher_t* dispatcher = nullptr) {
    return [this, dispatcher](
               fidl::InterfaceRequest<fuchsia::ui::scenic::Scenic> request) {
      bindings_.AddBinding(this, std::move(request), dispatcher);
    };
  }

 private:
  fidl::BindingSet<fuchsia::ui::scenic::Scenic> bindings_;
};

class TilesTest : public component::testing::TestWithContext {
 public:
  void SetUp() final {
    // Register the fake Scenic service with the environment.  This must
    // happen before calling |TakeContext|.
    controller().AddService(fake_scenic_.GetHandler());

    auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

    auto startup_context = TakeContext();
    auto scenic =
        startup_context
            ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
    scenic::ViewContext view_context = {
        .session_and_listener_request =
            scenic::CreateScenicSessionPtrAndListenerRequest(scenic.get()),
        .view_token2 = std::move(view_token),
        .incoming_services = {},
        .outgoing_services = {},
        .startup_context = startup_context.get(),
    };

    view_holder_token_ = std::move(view_holder_token);
    startup_context_ = std::move(startup_context);
    tiles_ = std::make_unique<tiles::Tiles>(std::move(view_context),
                                            std::vector<std::string>(), 10);
  }

  fuchsia::developer::tiles::Controller* tiles() const { return tiles_.get(); }

 private:
  FakeScenic fake_scenic_;
  fuchsia::ui::views::ViewHolderToken view_holder_token_;
  std::unique_ptr<component::StartupContext> startup_context_;
  std::unique_ptr<tiles::Tiles> tiles_;
};

TEST_F(TilesTest, Trivial) {}

TEST_F(TilesTest, AddFromURL) {
  uint32_t key = 0;
  tiles()->AddTileFromURL("test_tile", /* allow_focus */ true, {},
                          [&key](uint32_t cb_key) {
                            EXPECT_NE(0u, cb_key) << "Key should be nonzero";
                            key = cb_key;
                          });

  ASSERT_NE(0u, key) << "Key should be nonzero";

  tiles()->ListTiles([key](std::vector<uint32_t> keys,
                           std::vector<std::string> urls,
                           std::vector<fuchsia::ui::gfx::vec3> sizes,
                           std::vector<bool> focusabilities) {
    ASSERT_EQ(1u, keys.size());
    EXPECT_EQ(1u, urls.size());
    EXPECT_EQ(1u, sizes.size());
    EXPECT_EQ(1u, focusabilities.size());
    EXPECT_EQ(key, keys.at(0));
    EXPECT_EQ("test_tile", urls.at(0));
    EXPECT_EQ(true, focusabilities.at(0));
  });

  tiles()->RemoveTile(key);

  tiles()->ListTiles([](std::vector<uint32_t> keys,
                        std::vector<std::string> urls,
                        std::vector<fuchsia::ui::gfx::vec3> sizes,
                        std::vector<bool> focusabilities) {
    EXPECT_EQ(0u, keys.size());
    EXPECT_EQ(0u, urls.size());
    EXPECT_EQ(0u, sizes.size());
    EXPECT_EQ(0u, focusabilities.size());
  });

  tiles()->AddTileFromURL("test_nofocus_tile", /* allow_focus */ false, {},
                          [&key](uint32_t _cb_key) {
                            // noop
                          });
  tiles()->ListTiles([](std::vector<uint32_t> keys,
                        std::vector<std::string> urls,
                        std::vector<fuchsia::ui::gfx::vec3> sizes,
                        std::vector<bool> focusabilities) {
    EXPECT_EQ(1u, keys.size());
    EXPECT_EQ(1u, urls.size());
    EXPECT_EQ(1u, sizes.size());
    EXPECT_EQ(1u, focusabilities.size());
    EXPECT_EQ(false, focusabilities.at(0));
  });
}

}  // anonymous namespace
