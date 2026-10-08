# Porozit Watch firmware – agent notes

This ESP-IDF repository is the device half of the HELLO project. The Android app lives in the sibling repository `../HELLO`. These are separate Git repositories; check and commit them separately. `BLE_JSON_PROTOCOL.md` describes the currently implemented wire format, but verify protocol changes against `main/ble_service.c`, `main/measure.c`, and `main/watch_settings.c`.

## BLE contract with the app

- The device advertises as `Porozit Watch` with service `0xFFF0` and read/write/notify characteristic `0xFFF1`. Notifications require an active connection and CCCD subscription. `ble_service_notify()` rejects payloads over 128 bytes.
- Measurement notifications have `version:1`, `type:"measurement"`, and `alert:"started"|"done"|"save"|"delete"`. `done.time` is a string, `save.time` is a number; both are seconds. `timestamp` is uptime milliseconds, not wall-clock time. `done` is a preview in the app; `save` is the explicit ESP save event.
- The heartbeat is `{"command":"heartbeat","battery":N,"charging":BOOLEAN}` and has no version field.
- The firmware accepts one setting per write: `{"version":1,"type":"config",KEY:VALUE}`. Valid keys and ranges are in `BLE_JSON_PROTOCOL.md` and `main/watch_settings.c`. `volume` and `brightness` are native levels 0–5; timers are in seconds.
- A successful settings write produces `OK` only after validation, NVS persistence, and applying the setting. Failure produces `ERROR`. Keep that acknowledgment contract when changing settings code.
- The current firmware does not implement `get_config` or send a config snapshot. The app can now merge incoming flat config notifications into its Settings screen, but sending a six-field snapshot in one message would exceed the current 128-byte limit. If adding snapshot support, account for payload size and coordinate the format with the app.

## Where to work and verify

- GATT service and notifications: `main/ble_service.c`.
- Measurement events: `main/measure.c`.
- Settings validation, NVS, and runtime application: `main/watch_settings.c` and `main/nvs_storage.c`.
- Build with an ESP-IDF v5.x environment using `idf.py build` from this repository. Flashing and on-device behavior require the correct connected ESP32-S3 and port. Follow the user's current testing preference for optional tests.
