#!/usr/bin/env bash

set -euxo pipefail

if [[ ! -d "${FUCHSIA_DIR}" ]]; then
  echo "FUCHSIA_DIR environment variable not a directory"
  exit 1
fi

GOFMT="${FUCHSIA_DIR}/out/x64/tools/goroot/bin/gofmt"
if [ ! -x "${GOFMT}" ]; then
    echo "error: unable to find gofmt; did the path change?" 1>&2
    exit 1
fi

FIDLC="${FUCHSIA_DIR}/out/x64/host_x64/fidlc"
if [ ! -x "${FIDLC}" ]; then
    echo "error: fidlc missing; did you build?" 1>&2
    exit 1
fi

FIDLGEN="${FUCHSIA_DIR}/out/x64/host_x64/fidlgen"
if [ ! -x "${FIDLGEN}" ]; then
    echo "error: fidlgen missing; did you build?" 1>&2
    exit 1
fi

GIDL="${FUCHSIA_DIR}/out/default/tools/gidl"
if [ ! -x "${GIDL}" ]; then
    echo "error: gidl missing; did you build?" 1>&2
    exit 1
fi

EXAMPLE_DIR="${FUCHSIA_DIR}/tools/fidl/gidl-conformance-suite"
GO_LIB_DIR="${FUCHSIA_DIR}/third_party/go/src/syscall/zx/fidl/conformance"
GO_TEST_DIR="${FUCHSIA_DIR}/third_party/go/src/syscall/zx/fidl/fidl_test"
for src_path in `find "${EXAMPLE_DIR}" -name '*.gidl'`; do
    base=$( echo "${src_path}" | sed -e 's/\.gidl//' )
    gidl_path="${base}.gidl"
    fidl_path="${base}.test.fidl"
    tmpout=$( mktemp -d 2>/dev/null || mktemp -d -t 'tmpout' )
    json_path=$( mktemp ${tmpout}/tmp.XXXXXXXX )

    # need to have all .fidl files in one conformance library
    ${FIDLC} \
        --json "${json_path}" \
        --files "${fidl_path}"

    ${FIDLGEN} \
        --generators go \
        --json "${json_path}" \
        --output-base "${tmpout}" \
        --include-base "${tmpout}"
    cat << EOF > "${GO_LIB_DIR}/impl.go"
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Code generated by tools/fidl/gidl-conformance-suite/regen.sh; DO NOT EDIT.

// +build fuchsia

EOF
    cat "${tmpout}/impl.go" >> "${GO_LIB_DIR}/impl.go"
    ${GOFMT} -s -w "${GO_LIB_DIR}/impl.go"

    cat << EOF > "${GO_TEST_DIR}/conformance_test.go"
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Code generated by tools/fidl/gidl-conformance-suite/regen.sh; DO NOT EDIT.

// +build fuchsia

EOF
    ${GIDL} \
        --json "${json_path}" \
        --gidl "${gidl_path}" | ${GOFMT} >> "${GO_TEST_DIR}/conformance_test.go"
done
