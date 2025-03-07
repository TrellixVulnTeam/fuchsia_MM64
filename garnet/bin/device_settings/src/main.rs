// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use failure::{Error, ResultExt};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_syslog as syslog;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use fuchsia_zircon as zx;
use futures::{future, io, StreamExt, TryFutureExt, TryStreamExt};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::fs::{self, File};
use std::io::prelude::*;
use std::sync::Arc;

// Include the generated FIDL bindings for the `DeviceSetting` service.
use fidl_fuchsia_devicesettings::{
    DeviceSettingsManagerRequest, DeviceSettingsManagerRequestStream, DeviceSettingsWatcherProxy,
    Status, ValueType,
};

type Watchers = Arc<Mutex<HashMap<String, Vec<DeviceSettingsWatcherProxy>>>>;

struct DeviceSettingsManagerServer {
    setting_file_map: HashMap<String, String>,
    watchers: Watchers,
}

impl DeviceSettingsManagerServer {
    fn initialize_keys(&mut self, data_dir: &str, keys: &[&str]) {
        self.setting_file_map = keys
            .iter()
            .map(|k| (k.to_string(), format!("{}/{}", data_dir, k.to_lowercase())))
            .collect();
    }

    fn run_watchers(&mut self, key: &str, t: ValueType) {
        let mut map = self.watchers.lock();
        if let Some(m) = map.get_mut(key) {
            m.retain(|w| {
                if let Err(e) = w.on_change_settings(t) {
                    match e {
                        fidl::Error::ClientRead(zx::Status::PEER_CLOSED)
                        | fidl::Error::ClientWrite(zx::Status::PEER_CLOSED) => {
                            return false;
                        }
                        _ => {}
                    };
                    fx_log_err!("Error call watcher: {:?}", e);
                }
                return true;
            });
        }
    }

    fn set_key(&mut self, key: &str, buf: &[u8], t: ValueType) -> io::Result<bool> {
        match self.setting_file_map.get(key) {
            Some(file) => write_to_file(file, buf)?,
            None => return Ok(false),
        };

        self.run_watchers(&key, t);
        Ok(true)
    }
}

static DATA_DIR: &'static str = "/data/device-settings";

fn write_to_file(file: &str, buf: &[u8]) -> io::Result<()> {
    let mut f = File::create(file)?;
    f.write_all(buf)
}

fn read_file(file: &str) -> io::Result<String> {
    let mut f = File::open(file)?;
    let mut contents = String::new();
    if let Err(e) = f.read_to_string(&mut contents) {
        return Err(e);
    }
    Ok(contents)
}

fn spawn_device_settings_server(
    state: DeviceSettingsManagerServer,
    stream: DeviceSettingsManagerRequestStream,
) {
    let state = Arc::new(Mutex::new(state));
    fasync::spawn(
        stream
            .try_for_each(move |req| {
                let state = state.clone();
                let mut state = state.lock();
                future::ready(match req {
                    DeviceSettingsManagerRequest::GetInteger { key, responder } => {
                        let file = if let Some(f) = state.setting_file_map.get(&key) {
                            f
                        } else {
                            return future::ready(responder.send(0, Status::ErrInvalidSetting));
                        };
                        match read_file(file) {
                            Err(e) => {
                                if e.kind() == io::ErrorKind::NotFound {
                                    responder.send(0, Status::ErrNotSet)
                                } else {
                                    fx_log_err!("reading integer: {:?}", e);
                                    responder.send(0, Status::ErrRead)
                                }
                            }
                            Ok(str) => match str.parse::<i64>() {
                                Err(_e) => responder.send(0, Status::ErrIncorrectType),
                                Ok(i) => responder.send(i, Status::Ok),
                            },
                        }
                    }
                    DeviceSettingsManagerRequest::GetString { key, responder } => {
                        let file = if let Some(f) = state.setting_file_map.get(&key) {
                            f
                        } else {
                            return future::ready(responder.send("", Status::ErrInvalidSetting));
                        };
                        match read_file(file) {
                            Err(e) => {
                                if e.kind() == io::ErrorKind::NotFound {
                                    responder.send("", Status::ErrNotSet)
                                } else {
                                    fx_log_err!("reading string: {:?}", e);
                                    responder.send("", Status::ErrRead)
                                }
                            }
                            Ok(s) => responder.send(&*s, Status::Ok),
                        }
                    }
                    DeviceSettingsManagerRequest::SetInteger { key, val, responder } => {
                        match state.set_key(&key, val.to_string().as_bytes(), ValueType::Number) {
                            Ok(r) => responder.send(r),
                            Err(e) => {
                                fx_log_err!("setting integer: {:?}", e);
                                responder.send(false)
                            }
                        }
                    }
                    DeviceSettingsManagerRequest::SetString { key, val, responder } => {
                        fx_log_info!("setting string key: {:?}, val: {:?}", key, val);
                        match state.set_key(&key, val.as_bytes(), ValueType::Text) {
                            Ok(r) => responder.send(r),
                            Err(e) => {
                                fx_log_err!("setting string: {:?}", e);
                                responder.send(false)
                            }
                        }
                    }
                    DeviceSettingsManagerRequest::Watch { key, watcher, responder } => {
                        if !state.setting_file_map.contains_key(&key) {
                            return future::ready(responder.send(Status::ErrInvalidSetting));
                        }
                        match watcher.into_proxy() {
                            Err(e) => {
                                fx_log_err!("getting watcher proxy: {:?}", e);
                                responder.send(Status::ErrUnknown)
                            }
                            Ok(w) => {
                                let mut map = state.watchers.lock();
                                let mv = map.entry(key).or_insert(Vec::new());
                                mv.push(w);
                                responder.send(Status::Ok)
                            }
                        }
                    }
                })
            })
            .map_ok(|_| ())
            .unwrap_or_else(|e| eprintln!("error running device settings server: {:?}", e)),
    )
}

fn main() {
    if let Err(e) = main_ds() {
        fx_log_err!("{:?}", e);
    }
}

fn main_ds() -> Result<(), Error> {
    syslog::init_with_tags(&["device_settings"])?;
    let mut core = fasync::Executor::new().context("unable to create executor")?;

    let watchers = Arc::new(Mutex::new(HashMap::new()));
    // Attempt to create data directory
    fs::create_dir_all(DATA_DIR).context("creating directory")?;

    let mut fs = ServiceFs::new();
    fs.dir("public").add_fidl_service(move |stream| {
        let mut d = DeviceSettingsManagerServer {
            setting_file_map: HashMap::new(),
            watchers: watchers.clone(),
        };

        d.initialize_keys(
            DATA_DIR,
            &["DeviceName", "TestSetting", "Display.Brightness", "Audio", "FactoryReset"],
        );

        spawn_device_settings_server(d, stream)
    });
    fs.take_and_serve_directory_handle()?;

    Ok(core.run(fs.collect(), /* threads */ 2))
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl_fuchsia_devicesettings::{DeviceSettingsManagerMarker, DeviceSettingsManagerProxy};
    use futures::prelude::*;
    use tempfile::TempDir;

    fn async_test<F, Fut>(keys: &[&str], f: F)
    where
        F: FnOnce(DeviceSettingsManagerProxy) -> Fut,
        Fut: Future<Output = Result<(), fidl::Error>>,
    {
        let (mut exec, device_settings, _t) = setup(keys).expect("Setup should not have failed");

        let test_fut = f(device_settings);

        exec.run_singlethreaded(test_fut).expect("executor run failed");
    }

    fn setup(keys: &[&str]) -> Result<(fasync::Executor, DeviceSettingsManagerProxy, TempDir), ()> {
        let exec = fasync::Executor::new().unwrap();
        let mut device_settings = DeviceSettingsManagerServer {
            setting_file_map: HashMap::new(),
            watchers: Arc::new(Mutex::new(HashMap::new())),
        };
        let tmp_dir = TempDir::new().unwrap();

        device_settings.initialize_keys(tmp_dir.path().to_str().unwrap(), keys);

        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DeviceSettingsManagerMarker>().unwrap();
        spawn_device_settings_server(device_settings, stream);

        // return tmp_dir to keep it in scope
        return Ok((exec, proxy, tmp_dir));
    }

    #[test]
    fn test_int() {
        async_test(&["TestKey"], async move |device_settings| {
            let response = await!(device_settings.set_integer("TestKey", 18))?;
            assert!(response, "set_integer failed");
            let response = await!(device_settings.get_integer("TestKey"))?;
            assert_eq!(response, (18, Status::Ok));
            Ok(())
        });
    }

    #[test]
    fn test_string() {
        async_test(&["TestKey"], async move |device_settings| {
            let response = await!(device_settings.set_string("TestKey", "mystring"))?;
            assert!(response, "set_string failed");
            let response = await!(device_settings.get_string("TestKey"))?;
            assert_eq!(response, ("mystring".to_string(), Status::Ok));
            Ok(())
        });
    }

    #[test]
    fn test_invalid_key() {
        async_test(&[], async move |device_settings| {
            let response = await!(device_settings.get_string("TestKey"))?;
            assert_eq!(response, ("".to_string(), Status::ErrInvalidSetting));
            let response = await!(device_settings.get_integer("TestKey"))?;
            assert_eq!(response, (0, Status::ErrInvalidSetting));
            Ok(())
        });
    }

    #[test]
    fn test_incorrect_type() {
        async_test(&["TestKey"], async move |device_settings| {
            let response = await!(device_settings.set_string("TestKey", "mystring"))?;
            assert!(response, "set_string failed");
            let response = await!(device_settings.get_integer("TestKey"))?;
            assert_eq!(response, (0, Status::ErrIncorrectType));
            Ok(())
        });
    }

    #[test]
    fn test_not_set_err() {
        async_test(&["TestKey"], async move |device_settings| {
            let response = await!(device_settings.get_integer("TestKey"))?;
            assert_eq!(response, (0, Status::ErrNotSet));
            let response = await!(device_settings.get_string("TestKey"))?;
            assert_eq!(response, ("".to_string(), Status::ErrNotSet));
            Ok(())
        });
    }

    #[test]
    fn test_multiple_keys() {
        async_test(&["TestKey1", "TestKey2"], async move |device_settings| {
            let response = await!(device_settings.set_integer("TestKey1", 18))?;
            assert!(response, "set_integer failed");
            let response = await!(device_settings.set_string("TestKey2", "mystring"))?;
            assert!(response, "set_string failed");
            let response = await!(device_settings.get_integer("TestKey1"))?;
            assert_eq!(response, (18, Status::Ok));
            let response = await!(device_settings.get_string("TestKey2"))?;
            assert_eq!(response, ("mystring".to_string(), Status::Ok));
            Ok(())
        });
    }
}
