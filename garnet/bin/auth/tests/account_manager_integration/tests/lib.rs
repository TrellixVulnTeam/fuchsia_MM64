// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate defines a set of hermetic integration tests for the AccountManager and
//! AccountHandler components.

#![cfg(test)]
#![deny(warnings)]
#![deny(missing_docs)]
#![feature(async_await, await_macro, futures_api)]

mod account;
