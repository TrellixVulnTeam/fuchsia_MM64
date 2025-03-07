// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This is for Omaha client binaries written in Rust.

#![feature(async_await, await_macro, futures_api)]
#![recursion_limit = "128"]

pub mod common;
pub mod configuration;
pub mod http_request;
pub mod install_plan;
pub mod policy;
pub mod protocol;
pub mod requests;
