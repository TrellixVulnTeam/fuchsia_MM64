// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api, async_await, await_macro)]

use {
    failure::{bail, Error, ResultExt},
    fidl_fuchsia_bluetooth_gatt as gatt,
    fidl_fuchsia_power::{
        PowerManagerMarker, PowerManagerWatcherMarker, PowerManagerWatcherRequest,
        PowerManagerWatcherRequestStream,
    },
    fuchsia_async::{
        self as fasync,
        futures::{select, FutureExt, TryStreamExt},
    },
    fuchsia_component::client::connect_to_service,
    parking_lot::Mutex,
    pin_utils::pin_mut,
    std::collections::HashSet,
};

const BATTERY_SERVICE_UUID: &str = "0000180f-0000-1000-8000-00805f9b34fb";
const BATTERY_LEVEL_UUID: &str = "00002A19-0000-1000-8000-00805f9b34fb";
const BATTERY_LEVEL_ID: u64 = 0;

/// Struct to manage all the shared state of the tool.
struct BatteryState {
    inner: Mutex<BatteryStateInner>,
}

impl BatteryState {
    pub fn new(service: gatt::LocalServiceProxy) -> BatteryState {
        BatteryState {
            inner: Mutex::new(BatteryStateInner { level: 0, service, peers: HashSet::new() }),
        }
    }

    /// Add a new peer to the set of peers interested in notifications
    /// on changes to the battery level.
    pub fn add_peer(&self, peer_id: String) {
        self.inner.lock().peers.insert(peer_id);
    }

    /// Remove a peer from the set of peers interested in notifications
    /// on changes to the battery level.
    pub fn remove_peer(&self, peer_id: &str) {
        self.inner.lock().peers.remove(peer_id);
    }

    /// Get the last reported level of the battery as a percentage.
    pub fn get_level(&self) -> u8 {
        self.inner.lock().level
    }

    /// Set the level to the given value and notify any interested peers
    /// of the change.
    pub fn set_level(&self, level: u8) -> Result<(), Error> {
        let mut inner = self.inner.lock();
        if inner.level != level {
            println!("Battery percentage changed ({}%)", level);
            for peer_id in inner.peers.iter() {
                inner.service.notify_value(
                    BATTERY_LEVEL_ID,
                    &peer_id,
                    &mut vec![level].into_iter(),
                    false,
                )?;
            }
        }
        inner.level = level;
        Ok(())
    }
}

/// Inner data fields used for the `BatteryState` struct.
struct BatteryStateInner {
    /// The current battery percentage.
    level: u8,

    /// The proxy we use to send GATT characteristic value notifications.
    service: gatt::LocalServiceProxy,

    /// A set of remote LE device IDs that have subscribed to battery level
    /// notifications.
    peers: HashSet<String>,
}

/// Handle a stream of incoming gatt battery service requests.
/// Returns when the channel backing the stream closes or an error occurs while handling requests.
async fn gatt_service_delegate(
    state: &BatteryState,
    mut stream: gatt::LocalServiceDelegateRequestStream,
) -> Result<(), Error> {
    while let Some(request) = await!(stream.try_next()).context("error running service delegate")? {
        use fidl_fuchsia_bluetooth_gatt::LocalServiceDelegateRequest::*;
        match request {
            OnCharacteristicConfiguration { peer_id, notify, indicate, .. } => {
                println!(
                    "Peer configured characteristic (notify: {}, indicate: {}, id: {})",
                    notify, indicate, peer_id
                );
                if notify {
                    state.add_peer(peer_id);
                } else {
                    state.remove_peer(&peer_id);
                }
            }
            OnReadValue { responder, .. } => {
                let mut value_iter = vec![state.get_level()].into_iter();
                responder.send(Some(&mut value_iter), gatt::ErrorCode::NoError)?;
            }
            OnWriteValue { responder, .. } => {
                // Writing to the battery level characteristic is not permitted.
                responder.send(gatt::ErrorCode::NotPermitted)?;
            }
            OnWriteWithoutResponse { .. } => {}
        }
    }
    Ok(())
}

/// Handle a stream of incoming power notifications, updating state.
/// Returns when the channel backing the stream closes or an error occurs while handling requests.
async fn power_manager_watcher(
    state: &BatteryState,
    mut stream: PowerManagerWatcherRequestStream,
) -> Result<(), Error> {
    while let Some(PowerManagerWatcherRequest::OnChangeBatteryStatus { battery_status, .. }) =
        await!(stream.try_next()).context("error running power manager watcher")?
    {
        let level = battery_status.level.round() as u8;
        state.set_level(level)?;
    }
    Ok(())
}

fn main() -> Result<(), Error> {
    let mut exec = fasync::Executor::new()?;

    // Create endpoints for the required services.
    let (power_watcher_client, power_watcher_request_stream) =
        fidl::endpoints::create_request_stream::<PowerManagerWatcherMarker>()?;
    let (delegate_client, delegate_request_stream) =
        fidl::endpoints::create_request_stream::<gatt::LocalServiceDelegateMarker>()?;
    let (service_proxy, service_server) = fidl::endpoints::create_proxy()?;

    let gatt_server = connect_to_service::<gatt::Server_Marker>()?;
    let power_manager_server = connect_to_service::<PowerManagerMarker>()?;

    // Initialize internal state.
    let state = BatteryState::new(service_proxy);

    // Require encryption to access the battery level.
    let read_sec = Box::new(gatt::SecurityRequirements {
        encryption_required: true,
        authentication_required: false,
        authorization_required: false,
    });
    let update_sec = Box::new(gatt::SecurityRequirements {
        encryption_required: true,
        authentication_required: false,
        authorization_required: false,
    });

    // Build a GATT Battery service.
    let characteristic = gatt::Characteristic {
        id: BATTERY_LEVEL_ID,
        type_: BATTERY_LEVEL_UUID.to_string(),
        properties: gatt::PROPERTY_READ | gatt::PROPERTY_NOTIFY,
        permissions: Some(Box::new(gatt::AttributePermissions {
            read: Some(read_sec),
            write: None,
            update: Some(update_sec),
        })),
        descriptors: None,
    };
    let mut service_info = gatt::ServiceInfo {
        id: 0,
        primary: true,
        type_: BATTERY_SERVICE_UUID.to_string(),
        characteristics: Some(vec![characteristic]),
        includes: None,
    };

    // Register the local power watcher with the power manager service.
    power_manager_server.watch(power_watcher_client)?;

    let main_fut = async move {
        // Publish the local gatt service delegate with the gatt service.
        let status = await!(gatt_server.publish_service(
            &mut service_info,
            delegate_client,
            service_server
        ))?;
        if let Some(error) = status.error {
            bail!("Failed to publish battery service to gatt server: {:?}", error);
        }
        println!("Published Battery Service to local device database.");

        // Start the gatt service delegate and power watcher server.
        let service_delegate = gatt_service_delegate(&state, delegate_request_stream);
        let power_watcher = power_manager_watcher(&state, power_watcher_request_stream);
        pin_mut!(service_delegate);
        pin_mut!(power_watcher);
        select! {
            res = service_delegate.fuse() => res?,
            res = power_watcher.fuse() => res?,
        };

        Ok(())
    };

    exec.run_singlethreaded(main_fut)
}
