// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_IQUERY_MODES_H_
#define GARNET_BIN_IQUERY_MODES_H_

#include <vector>

#include <fuchsia/io/cpp/fidl.h>
#include <lib/fit/promise.h>
#include <lib/inspect/reader.h>
#include "garnet/bin/iquery/options.h"
#include "garnet/bin/iquery/utils.h"
#include "garnet/public/lib/inspect/discovery/object_source.h"

namespace iquery {

fit::promise<std::vector<inspect::ObjectSource>> RunCat(const Options*);
fit::promise<std::vector<inspect::ObjectSource>> RunFind(const Options*);
fit::promise<std::vector<inspect::ObjectSource>> RunLs(const Options*);

}  // namespace iquery

#endif  // GARNET_BIN_IQUERY_MODES_H_
