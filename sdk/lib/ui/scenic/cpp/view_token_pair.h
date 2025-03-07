// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_UI_SCENIC_CPP_VIEW_TOKEN_PAIR_H_
#define LIB_UI_SCENIC_CPP_VIEW_TOKEN_PAIR_H_

#include <fuchsia/ui/views/cpp/fidl.h>
#include <lib/zx/eventpair.h>
#include <utility>

namespace scenic {

using ViewTokenPair = std::pair<fuchsia::ui::views::ViewToken,
                                fuchsia::ui::views::ViewHolderToken>;

// Convenience function which allows clients to easily create a |ViewToken| /
// |ViewHolderToken| pair for use with |View| resources.
ViewTokenPair NewViewTokenPair();

// Convenience functions which allow converting from raw eventpair-based tokens
// easily.
// TEMPORARY; for transition purposes only.
// TODO(SCN-1287): Remove.
fuchsia::ui::views::ViewToken ToViewToken(zx::eventpair raw_token);
fuchsia::ui::views::ViewHolderToken ToViewHolderToken(zx::eventpair raw_token);

}  // namespace scenic

#endif  // LIB_UI_SCENIC_CPP_VIEW_TOKEN_PAIR_H_
