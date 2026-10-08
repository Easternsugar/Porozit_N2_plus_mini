# Porozit project: a full summary for the next session

> This file collects the context of the previous (cloud) Claude Code session.
> It lives in all three repositories (`docs/HANDOFF.md`); `CLAUDE.md` loads it automatically.
> Last updated: 2026-10-08.

## 1. Who we are and what the product is

- **The user:** Levente Sellei (levente@sellei.hu), GitHub: `Easternsugar`. Talks in **Hungarian**, so answer in Hungarian.
  Code, comments and commit messages are in English.
- **The product:** **Porozit N2+** porosity tester for paraglider fabric (Hello Ltd., porosimeter.hu).
  A measuring head goes on the fabric and a weight (the "lift") drops. The device measures how long it takes:
  the result is in **seconds**, or converted to **l/m²/min**.
- **Conversion:** `l/m²/min = 7500 / sec` (in the firmware: `#define PMA 7500`).
- **Rating:** > 20 s good (green), 14–20 s acceptable (orange), ≤ 14 s fail (red)
  (`redStage 14.0`, `orangeStage 20.0`).
- Two devices:
  - **Porozit N2+**: the big touchscreen unit (formerly "ESP32_sarkany"). **We're testing this one now.**
  - **Porozit N2+ mini**: watch-style unit (formerly "Sárkány Watch"). **On hold**: the user will design it later.
- The app is called **"Porozit app"**. The previous developer's Kotlin app (HELLO-main.zip) was replaced by a
  Flutter app written from scratch. **From now on we develop everything ourselves.** We don't touch the
  old developer's repo.
- The word "Sarkany"/"Sárkány" doesn't appear anywhere any more; everything is "Porozit".
- Figma (device design): https://www.figma.com/design/FkbEMqLgzqsoTPYQz9t4EO/Porozit_Device_2
  The user is **making a Figma design for the app**, and we'll rebuild the UI from it.

## 2. Repositories

| Repo | Contents | Branch |
|---|---|---|
| https://github.com/Easternsugar/Hello_App | Flutter app (Porozit app) | `claude/festive-mayer-pyslfg` (all the work is here; no PR yet) |
| https://github.com/Easternsugar/Porozit_N2_plus | N2+ firmware (PlatformIO/Arduino) | `main` |
| https://github.com/Easternsugar/Porozit_N2_plus_mini | N2+ mini firmware (ESP-IDF) | `main` |

`PROTOCOL.md` is **identical in all three repos**; change it in all three together.
CI (GitHub Actions) is green in all three.

## 3. BLE protocol v2 (summary; details in PROTOCOL.md)

- Service `0xFFF0`, characteristic `0xFFF1` (read/write/notify). One JSON object per message, ≤ 240 bytes.
  The app requests MTU 247 on Android.
- Advertised name: `Porozit N2+ XXXX`, `Porozit N2+ mini XXXX` (last 4 hex digits of the MAC).
- Every message has `"version":2` and a `"type"`.
- Device → app: `info` (model `n2`/`mini`, fw, serial, maxSaved: N2+ = 24, mini = 0), `state` (idle/measuring/
  result/error, time, count, plugged), `measurement` (event: started/progress/done/save/delete/reset/error),
  `saved` (answer to `list`), `config`, `battery`, `result` (command answer, `status` OK/ERROR).
- App → device: `command` (action: status/save/delete/reset/list), `config` (unit "sec"/"pma", beep s,
  sleep s, volume %, brightness %, language). Config is all-or-nothing.
- Session start: app → `status`; device → `info`, `config`, `state`, `battery`.
- **The device is the source of truth for the measurement cycle**; the app only sends commands and follows events.
  The N2+ stores the results itself (EEPROM, 24 doubles); the app mirrors that list with `list`.
- **Saving a partial (interrupted) measurement is allowed on the N2+, and that's deliberate** (the user's
  decision: "it's fine that a partial measurement can be saved"). When the head is pulled out, the N2+ sends
  `error` + `reason:"unplugged"` + partial `time`; once the head is plugged back in, Save is allowed (on the
  device and from the app).
- The app also understands v1 (old watch firmware: `alert` events, `{"command":"heartbeat"}`).

## 4. Porozit app (Flutter): Hello_App

- Flutter 3.47.x / Dart 3.13. BLE: **universal_ble** (BSD-3). `flutter_blue_plus` is **deliberately avoided**:
  its license requires payment for commercial use.
  Other packages: shared_preferences, intl, flutter_localizations, gen-l10n (ARB: `lib/l10n/app_en.arb`, `app_hu.arb`).
- Structure:
  - `lib/core/`: `units.dart` (conversion, MeasurementUnit), `rating.dart`, `session.dart`
    (MeasurementSession, pending result, encode/decode), `protocol.dart` (v2 parsing, `PorozitCommands`, `DeviceConfig`).
  - `lib/device/`: `porozit_connection.dart` (abstract), `ble_connection.dart`, `device_scanner.dart`
    (name prefix `porozit`/`sarkany` or service match), `simulated_connection.dart` (demo N2+ speaking v2).
  - `lib/state/porozit_controller.dart`: ChangeNotifier, MeasurePhase {idle, measuring, result, error},
    canSave/canDelete logic (including the partial-save rule above).
  - `lib/ui/`: theme (PorozitColors), home_shell (bottom navigation), screens: measure, average, settings
    (device settings: unit, beep, sleep, language, volume, brightness, firmware version), connect.
- Demo mode: the app works without a device (simulated N2+, "trigger" button).
- Tests: `flutter test` (32 tests, passing). Format check in CI:
  `dart format --output=none --set-exit-if-changed -l 120 $(find lib test -name '*.dart' -not -path 'lib/l10n/*')`
  (the generated `lib/l10n/*.dart` is committed exactly as the tool emits it, and excluded from the format check).
- CI jobs: test, android (APK), web, ios (manual).

## 5. Porozit N2+ firmware: Porozit_N2_plus

- Hardware: **WT32-SC01 Plus** (ESP32-S3, 3.5" 480×320, 8 MB flash, PSRAM).
- PlatformIO: `espressif32@6.10.0` (Arduino core 2.0.17), env `wt32-sc01-plus`, partitions `default_8MB.csv`, SPIFFS.
- Libraries: LVGL 8.3.6 (UI from SquareLine Studio: `SquareLine/` → `src/ui/`), LovyanGFX, ArduinoNvs,
  ESP32-audioI2S, ArduinoJson 7, NimBLE-Arduino ^2.3.0.
- GPIO: 10 = trigger (3.5 mm jack), 13 = head plugged in, 11 = battery, 12 = charging, 14 = button, 21 = power hold.
- Storage: results in EEPROM (24 doubles), settings in NVS, SPIFFS: `data/lang` (translations from `lang.xlsx`), `data/song` (sounds).
- New/changed code (FW_VERSION **2.0.0**):
  - `src/PorozitBle.h/.cpp`: NimBLE GATT server. Incoming messages are queued and handled in `loop()` (`poll()`);
    `send()` checks the MTU.
  - `src/AppLink.h/.cpp`: protocol v2 (appLinkBegin/Loop/Event/Progress/SendState, commands, config).
    Volume levels {0,4,8,12,16,21}, brightness {25,51,102,153,204,255}.
  - `src/main.cpp`: hooks (started, progress, done, error with partial time, save/delete/reset, plug state).
    Fixes: NVS zero values weren't restored (`NVS.getInt(key, default)`); one beep option was unreachable
    (index wrap); `#include <Esp.h>` (with that exact case, otherwise it fails on Linux/CI).
- Build size (CI): flash 58.6% (1,959,465 B), RAM 63.6%.
- `Serial.begin(9600)`; it's native USB-CDC, so the baud rate doesn't matter.
- **`tools/backup_and_flash.sh`**: saves the whole 8 MB flash to `backup/porozit_n2_flash_<date>.bin`
  (+ sha256; `backup/` is in .gitignore), checks the size, then `pio run -t upload`, `pio run -t uploadfs`,
  `pio device monitor`. `--backup` only saves. Restore:
  `esptool.py --chip esp32s3 --port <port> write_flash 0 backup/<file>.bin`.
  Needs: `brew install platformio esptool`. If it can't connect: hold BOOT while plugging in.

## 6. Porozit N2+ mini firmware: Porozit_N2_plus_mini (on hold)

- Hardware: Waveshare **ESP32-S3-Touch-LCD-1.69**. ESP-IDF **v6.0.1**, NimBLE, cJSON, LVGL 8.4.0 + esp_lvgl_port 2.9.0
  (both **vendored** in `components/`, because the registry lvgl 8.4.0 manifest broke the build with IDF 6:
  "Missing required kconfig option").
- `main/`: watch_settings (single settings path, v2 units), ble_protocol (v2, model "mini"), measure (v2 events,
  progress every 500 ms), ble_service (name "Porozit N2+ mini XXXX", advertises 0xFFF0), main.c (battery heartbeat).
- Project: `porozit_n2_mini`, PROJECT_VER 2.0.0. CI: `espressif/esp-idf-ci-action` v6.0.1.
- The mini keeps no results (`maxSaved: 0`); the app keeps them. A partial result can't be saved on it.

## 7. Where we are right now (NEXT STEP)

The user has the **big N2+** connected to their MacBook over USB-C, and has a **trigger cable**
(3.5 mm mono jack + pushbutton: it stands in for the lift being released/arriving, i.e. it starts and stops a measurement).
The device still runs the **old (original) firmware** (old UI: "Measurement | 1", 0.0 sec).

The task in this (local) session:
1. `brew install platformio esptool` (if not installed yet), clone/open `Porozit_N2_plus`.
2. Run `tools/backup_and_flash.sh`: **back up the old firmware first** (the user asked for this explicitly), then flash 2.0.0 + SPIFFS.
3. Read the serial log and check:
   - the boot is clean, the UI comes up, languages and sounds load;
   - the trigger pushbutton starts and stops a measurement, and the time is correct;
   - pull out the head mid-measurement → error + partial time; plug it back in → Save works;
   - BLE advertising "Porozit N2+ XXXX".
4. Test with the app (Android APK from the CI artifact, or `flutter run`): connect, live time, save/delete/reset
   both ways, list sync, device settings (unit, beep, sleep, language, volume, brightness).
5. Fix any bugs you find in the relevant repo, push, keep CI green.

## 8. Later tasks

- Rebuild the app UI from the user's Figma design.
- **PDF test report / certificate**: owner, glider make/model/size/serial number/year, measurement results
  (seconds and l/m²/min, average, rating), logo (the user has the logo/tokens as SVG/JSON).
- N2+ mini: UI design (the user will design it), then hardware test.
- Eventually: a PR from the Hello_App branch to the default branch (only if the user asks).

## 9. Working rules / preferences

- Commits: clear English messages; don't put the model name in commits.
- Don't open a PR unless the user asks.
- The user prefers proposals and execution to long questions; they decide on product questions themselves
  (e.g. the partial save stays).
