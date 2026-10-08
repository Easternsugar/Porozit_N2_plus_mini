# Porozit N2+ mini firmware – agent notes

This ESP-IDF repository is the Porozit N2+ mini (watch-style unit) firmware. The Porozit app (Flutter) and the Porozit N2+ firmware live in separate repositories. `PROTOCOL.md` is the wire format; verify changes against `main/ble_service.c`, `main/ble_protocol.c`, `main/measure.c` and `main/watch_settings.c`.

## BLE contract with the app

- The protocol is **PROTOCOL.md (version 2)**, shared word for word with the Porozit N2+ firmware and the
  Porozit app (Flutter, separate repositories). Change all three together.
- The device advertises as `Porozit N2+ mini XXXX` with service `0xFFF0` and read/write/notify characteristic `0xFFF1`.
  Notifications need a connection, a CCCD subscription and must fit the negotiated MTU (the app requests 247).
- `measure.c` sends the `measurement` events; `ble_protocol.c` handles commands/config and sends `info`,
  `config`, `state`, `battery` and `result`; `watch_settings.c` is the only place settings are validated,
  stored and applied (all or nothing).

## Where to work and verify

- GATT service and notifications: `main/ble_service.c`.
- Protocol messages: `main/ble_protocol.c`.
- Measurement events: `main/measure.c`.
- Settings validation, NVS, and runtime application: `main/watch_settings.c` and `main/nvs_storage.c`.
- Build with an ESP-IDF v5.x environment using `idf.py build` from this repository. Flashing and on-device behavior require the correct connected ESP32-S3 and port. Follow the user's current testing preference for optional tests.
