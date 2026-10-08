# Porozit project: everything so far (conversation, context, decisions, code)

> A complete summary of the work done with Claude Code in the cloud session on 2026-10-08, for the next
> (local) Claude session and for the user.
> **Language:** the user (Levente Sellei, GitHub: `Easternsugar`) talks in Hungarian, so answer in Hungarian.
> Code, comments and commit messages are in English.
>
> Structure:
> 1. The background: what this is about
> 2. The conversation, step by step (what was asked, what was answered, what got done)
> 3. Decisions and their reasons
> 4. Technical state: repos, protocol, app, firmwares
> 5. Where we are now, and what comes next
> 6. Open questions, ideas, later tasks
> 7. Appendix: the most important code in full

---

## 1. The background: what this is about

### The product
**Porozit N2+** is a **porosity tester for paraglider fabric** (Hello Ltd., webshop and blog: porosimeter.hu).
Paragliders go through a technical inspection every year or two, depending on the country. Among other
things (line lengths, fabric strength, etc.), the **porosity of the canopy fabric** is measured: how easily
air passes through the cloth. This is an important safety parameter; porous, worn cloth is dangerous.

How it measures: the measuring head is placed on the fabric, and a weight (the "lift") drops and pushes air
through the cloth. The device measures **how many seconds** this takes. The longer it takes, the better
(less porous) the fabric.

**Units:**
- Traditionally **seconds** (a historical, human-defined method: a set volume of air through a set area).
- A newer calculation method: **l/m²/min** (litres per square metre per minute). There's a blog post on
  porosimeter.hu about converting results (Claude helped write it earlier).
- Conversion: **`l/m²/min = 7500 / sec`**. It's in the firmware too (`#define PMA 7500`).
  E.g. 20 s ≙ 375 l/m²/min, 14 s ≙ 536 l/m²/min.

**Rating** (from the firmware: `redStage 14.0`, `orangeStage 20.0`):
- **> 20 s**: good (green)
- **14–20 s**: acceptable (orange/yellow)
- **≤ 14 s**: fail (red)

### The devices
- **Porozit N2+**: the "big" unit with a touchscreen (3.5", 480×320). The Figma design is for this one:
  https://www.figma.com/design/FkbEMqLgzqsoTPYQz9t4EO/Porozit_Device_2
  Not on the market yet; testing is starting. The original developer called it "ESP32_sarkany".
- **Porozit N2+ mini**: a small, **watch-style** unit (1.69" display). The developer called it
  "Sárkány Watch"/"esp32_sarkany_watch". Its UI/UX also needs redoing, but **that's on hold for now**.
- Both have Bluetooth (ESP32-S3) so the user can **connect with a phone**. In the first round the app should do the
  same things the device can (measuring, display, saving, average, unit switching, settings).

### The app: "Porozit app"
- The developer (who "built it by vibe-coding") sent a Kotlin Android app (HELLO-main.zip). It turned out to be a
  generic "Bluetooth scanner" template that has never talked to a real Porozit (details below).
- The user needs a **cross-platform** app (Android + iOS) → we **rewrote it from scratch in Flutter**.
- **Future feature (important):** **PDF test report / certificate**. In the app the user enters the
  owner's name, the glider's make, model, size, serial number and year; it shows the measurement results
  for that glider; it puts the Porozit/Hello logo on it, and it can be exported as a certificate.
- Branding: there's a logo and a colour scheme (in the Figma and on the website). The user can provide the logo
  as SVG, and the design tokens (colours, fonts) exported from Sketch as JSON. These haven't been sent yet.
  Colours measured so far from the Figma: red `#F60F43`, green `#28B891`, dark `#1A1A1A`, background `#F2F2F2`
  (Claude picked the yellow "acceptable" colour).

### Who develops it
The user said: **"from now on, we'll develop this whole thing ourselves"**: the app and both firmwares.
They don't touch the original developer's GitHub repo; they keep the code in their own repos (and may share it with the developer).
The user is **making a new Figma design for the app**, and Claude will build the app's UI from it.

---

## 2. The conversation, step by step

### 2.1 Importing the old app (HELLO-main.zip)
**User:** the developer sent a mobile app for the porosity tester on GitHub. They downloaded it as a ZIP, because
they don't want to edit the developer's repo. Upload it to their own repo, open it, and let's start working;
it probably needs cleaning up from a UI/UX point of view.

**Done:** unpacked unchanged into `Easternsugar/Hello_App`, branch `claude/festive-mayer-pyslfg`
(the repo was empty, so this is its only branch).

**What was in it:** a small Kotlin + Jetpack Compose app, two screens (Bluetooth scan → device data:
count/min/max/average/sum, list of raw numbers). Findings:
- Nothing showed it was a porosity tester ("My Application", `com.example.myapplication`, default icon).
- The measured value wasn't highlighted; no chart; the scan list showed every Bluetooth device; no save/export.
- Bugs: `addMenuProvider` calling `TODO()` (crash); the BLE part looked for a **heart-rate monitor's** standard
  service ("Heart Rate … as placeholder"); bad handling of split/merged packets.
- Classic Bluetooth (SPP), which **the ESP32-S3 doesn't support at all**, and iOS doesn't allow without MFi either.

### 2.2 The context, and the switch to Flutter
**User:** gave the context (Porozit, porosity, seconds vs l/m²/min, blog, Shopify draft page, Figma, photos),
app name "Porozit app", future PDF certificate. "Let's fix the bugs first, but if you say we should rebuild it, I'm in
for that too." Then: **"if you'd write it in a different stack, we can start from scratch. I need a cross-platform app."**

**Done:** Claude rewrote the app from scratch in **Flutter** (one codebase for Android + iOS + web demo):
- Core logic: unit conversion, rating, measurement session (save/delete/average), protocol parser, with tests.
- BLE layer with **universal_ble** (not `flutter_blue_plus`, because that one became **paid for commercial use**).
- **Simulated Porozit (demo mode):** the full measurement cycle works without a device.
- Screens: **Measure** (big value, the other unit smaller, rating, Save/Delete), **Average** (list, average,
  reset with confirmation), **Settings** (unit, device, battery, rating limits, help), **Connect** (Porozit devices
  first, signal strength, Bluetooth off / missing permission handling).
- **Hungarian and English**, following the phone's language.
- GitHub Actions CI: tests, Android APK (downloadable from Artifacts), web build, iOS build (manual).
- Web demo as a private artifact: https://claude.ai/artifact/CRbxwQzfLPQnddXdH86wdP

### 2.3 The ESP32-S3 pinout and PDF
**User:** sent a pinout image and the Waveshare ESP32-S3-Touch-LCD-1.69 PDF: "do these help?"
**Answer:** yes: the **ESP32-S3 only supports BLE** (no classic Bluetooth), and iOS also needs BLE, so the app
is built on BLE. There's a battery voltage measurement (`BAT_ADC`). The PDF belonged to the small (1.69") board, while the Figma
showed the 480×320 one (later it turned out: the PDF is for the mini, the Figma for the N2+).

### 2.4 Firmware sample code, and "from now on we develop it"
**User:** "yes, write the firmware sample code and plug it in. From now on we'll develop this whole thing ourselves.
I'll make a Figma design and you'll build from it — is that OK?"
**Done:** a BLE module + test program for ESP32-S3 (it compiled in CI). Claude's advice for the Figma:
phone-sized frames (e.g. 390×844), one frame per screen with a descriptive name, Variables for colours/fonts/spacing,
Auto Layout and components, states drawn too (empty list, measuring, no connection, Bluetooth off);
send the frame link (with `node-id`).

### 2.5 "Is the Bluetooth communication in the Kotlin repo?"
**User:** is there anything in the Kotlin repo about how it learns that it's connected, that the measurement started/ended,
and how it sends the value? Do we need the code on the chip, or a test device?
**Answer:** **no, there's nothing.** The old app only read "a number came in" (over SPP), the BLE part was a heart-rate placeholder,
the tests were just Android Studio samples. Options: (1) get the firmware source (best),
(2) nRF Connect + a real device, (3) define the protocol ourselves. For the final test you definitely need a real N2+;
a cheap ESP32-S3 board is enough as a simulator.

### 2.6 The two original firmwares arrive
**User:** got hold of the device programs: `esp32_sarkany_watch-main.zip` (the small watch) and
`ESP32_sarkany-master.zip` (the big touchscreen unit, the one in the Figma). "Have a look at where things stand."

**Findings:**
- **Big unit (ESP32_sarkany → Porozit N2+):** WT32-SC01 Plus (ESP32-S3, 3.5" 480×320 touch), Arduino + PlatformIO,
  LVGL + SquareLine Studio UI, 5 languages (en, hu, de, es, fr), stores at most 24 measurements.
  **There was no Bluetooth in it** (README: "Conectivity with phones will be added in the future.").
  Measurement logic (`main.cpp`, `measure()`): GPIO10 = measuring sensor/trigger (low → measurement running, goes high → finished),
  GPIO13 = measuring head plugged in. States: waiting, error (head pulled out mid-measurement), disconnected.
  Save (`saveflash`), Delete (`deletemes`), Reset as separate functions; a new measurement only starts after save/delete.
  **Constants match:** `PMA 7500`, limits 14.0 and 20.0 s.
- **Watch (esp32_sarkany_watch → Porozit N2+ mini):** ESP-IDF, ESP32-S3, 1.69" display (the PDF was for this one).
  **It had BLE, with a working JSON protocol:** service `0xFFF0`, characteristic `0xFFF1`, name `SarkanyWatch`,
  events `started`/`done`/`save`/`delete`, heartbeat every 10 s (battery, charging), commands from the phone (settings,
  save, delete, status). Inconsistencies: time as a string in `done`, as a number in `save`; the heartbeat had a different format;
  no live time while measuring. The watch's notes mention a "HELLO Android app" that **isn't** the version we received
  (there may be a newer version / another branch on the developer's GitHub).
- **Proposal (accepted):** one shared protocol for both devices = the watch's JSON protocol, cleaned up (**protocol v2**);
  the app switches to it (backwards-compatible with the old watch firmware); BLE gets built into the big unit.

### 2.7 "Write into the repos; rename Sárkány → Porozit; three separate repos"
**User:** "go ahead and write into the two devices' repos." "Sárkány Watch… should be Porozit Watch. **Everywhere it says Sárkány,
it should say Porozit**, because that name is pretty odd." "**Go ahead and turbocharge them**, do it the way you think is right."
And: **"let's have three separate repos"** (not a monorepo), because separate software goes onto each device.
In the meantime: "I've created the repos on GitHub: Porozit_N2_plus, Porozit_N2_plus_mini — **the mini is the watch — that'll be
its name — mini**."

**Done (all three repos compile in CI):**
- Both firmware repos keep the full history: **the first commit is the original code from the developer, unchanged**,
  then the rename, then the improvements.
- "Sarkany" is gone everywhere. BLE names: `Porozit N2+ XXXX`, `Porozit N2+ mini XXXX` (last 4 hex digits of the MAC).
- **Shared BLE protocol v2** (`PROTOCOL.md`, identical in all three repos): live time while measuring, start/end/error events,
  save/delete/reset from the phone, settings (unit, beep, sleep, volume, brightness, language), and on connect
  the device sends its type, firmware version, battery and settings.
- **N2+:** now has BLE (`PorozitBle` + `AppLink`). The phone and the device buttons call the **same functions**, so they always agree.
  The list of results stored on the device shows up in the app.
- **N2+ mini:** switched to v2; the settings handling (two contradictory parts) was merged into one;
  e.g. the beep interval now takes effect without a restart.
- **App:** speaks v2, new "Device settings" panel in Settings, works with old watches too, the demo updated as well.
- **Bugs found and fixed:**
  - N2+: after a restart, "beep off", "volume 0" and the 5-minute sleep setting were lost (NVS zero values).
  - N2+: the "every 10 minutes" beep option couldn't be selected (index wrap).
  - N2+: the code only compiled on Windows (`<ESP.h>` vs `<Esp.h>` case).
  - N2+ mini: it didn't compile with the current ESP-IDF in CI (the LVGL registry manifest was broken) → LVGL is now vendored in the repo.

### 2.8 Decision: saving a partial measurement
**Question from Claude:** on the N2+, if the measuring head is pulled out mid-measurement and plugged back in, the device lets you save the interrupted
measurement. Should only Delete be allowed then?
**User:** **"no, it's fine like this, that a partial measurement can be saved."**
**Done:** the device's behaviour stays. The app and the protocol were aligned to it: when the head is pulled out, the N2+ sends the
partial time (`error` + `reason:"unplugged"` + `time`). Once the head is plugged back in, it can be saved from the device and from the
phone too. With the head pulled out, neither allows it. On the mini, an interrupted measurement can't be saved (it doesn't keep the partial time).

### 2.9 "Would it help to connect the laptop over USB-C?"
**Answer:** yes, a lot, but only if a **Claude session runs on the laptop too** (the cloud session can't see the USB).
Then Claude can flash, read the serial log during measurements, and iterate fast. Before flashing, back up the old firmware
(`esptool read_flash`); the N2+ also needs its language/sound files flashed separately (SPIFFS).

### 2.10 The device and the trigger cable are here
**User** (with two photos): "I've got the device — not the mini, the normal one. **Let's leave the mini for now**, I'll design that one
later too; for now let's go with the big one in a test version. **It's connected to my laptop with USB-C.** I also got a **trigger cable**:
one end is a jack plug, the other end a pushbutton that **imitates the lift being released and arriving** — so the measurement starts and then
stops. **Update the firmware, and save the old firmware into some backup file**, though I don't think we'll need it."

What the photos showed: the N2+ is connected to the MacBook with a USB-C cable and runs the **old (original) UI** ("Measurement | 1", 0.0 sec).
The trigger cable: 3.5 mm mono jack + pushbutton.

**Done:** the cloud session can't see the laptop's USB, so Claude wrote **`tools/backup_and_flash.sh`**
(in the Porozit_N2_plus repo): backs up the whole 8 MB flash → checks it → flashes firmware + SPIFFS → log.
Then the user said they'd **start a new local session**, and asked for this summary.

---

## 3. Decisions and their reasons

| Decision | Why |
|---|---|
| Flutter, from scratch | The user needs cross-platform; the old Kotlin app was a non-working template. |
| BLE (not classic Bluetooth) | The ESP32-S3 only supports BLE; iOS doesn't allow SPP without MFi. |
| `universal_ble`, not `flutter_blue_plus` | flutter_blue_plus became paid for commercial use. universal_ble is BSD-3. |
| Shared protocol v2 = the watch's JSON protocol, cleaned up | It already worked on the watch; one protocol for both devices. |
| The device is the "source of truth" | The phone and the buttons call the same functions; the app only follows events. |
| The N2+ stores the results; the app mirrors them | The device was already storing 24 measurements; this way they always agree. |
| Saving a partial measurement is allowed (N2+) | **The user's decision.** |
| Three separate repos | **The user's decision:** separate software runs on each device. |
| Sárkány → Porozit everywhere | **The user's decision.** |
| "N2+ mini" is the watch's name | **The user's decision.** |
| LVGL vendored in the mini repo | The registry LVGL 8.4.0 manifest broke the build with ESP-IDF 6. |
| The original code is the first commit in the firmware repos | Traceable: every change is visible against the developer's version. |
| Back up the whole flash before flashing | **The user's request**; restores the original state exactly. |

---

## 4. Technical state

### 4.1 Repositories

| Repo | Contents | Branch |
|---|---|---|
| https://github.com/Easternsugar/Hello_App | Porozit app (Flutter) | `claude/festive-mayer-pyslfg` (all the work is here, no PR yet; the first commit is the old Kotlin app) |
| https://github.com/Easternsugar/Porozit_N2_plus | N2+ firmware (PlatformIO/Arduino) | `main` |
| https://github.com/Easternsugar/Porozit_N2_plus_mini | N2+ mini firmware (ESP-IDF) | `main` |

`PROTOCOL.md` is identical in all three repos; **change it in all three together.** CI (GitHub Actions) is green in all three.
Every repo has a `CLAUDE.md` that loads this file (`docs/HANDOFF.md`).

### 4.2 BLE protocol v2 (in short; full text in the Appendix)
- Service `0xFFF0`, characteristic `0xFFF1` (read/write/notify). One JSON object per message, ≤ 240 bytes, MTU 247 requested on Android.
- Every message: `"version":2` + `"type"`.
- Device → app: `info` (model `n2`/`mini`, fw, serial, maxSaved: N2+ = 24, mini = 0), `state` (idle/measuring/result/error,
  time, count, plugged), `measurement` (event: started/progress/done/save/delete/reset/error), `saved` (answer to `list`),
  `config`, `battery`, `result` (command answer).
- App → device: `command` (status/save/delete/reset/list), `config` (unit "sec"/"pma", beep, sleep, volume %, brightness %, language).
- Connecting: app → `status`; device → `info`, `config`, `state`, `battery`.
- The app also understands v1 (old watch firmware).

### 4.3 Porozit app (Flutter): Hello_App
- Flutter 3.47.x / Dart 3.13. Packages: universal_ble, shared_preferences, intl, flutter_localizations, gen-l10n
  (`lib/l10n/app_en.arb`, `app_hu.arb`).
- `lib/core/`: units (conversion), rating, session (measurement session), protocol (v2 parsing, commands, DeviceConfig).
- `lib/device/`: porozit_connection (abstract), ble_connection (FFF0/FFF1, MTU), device_scanner (name prefix `porozit`/`sarkany`
  or service match), simulated_connection (demo N2+).
- `lib/state/porozit_controller.dart`: app state, MeasurePhase {idle, measuring, result, error}, canSave/canDelete.
- `lib/ui/`: theme (PorozitColors), home_shell (bottom navigation), screens: measure, average, settings, connect.
- Tests: `flutter test` (32 tests, passing).
  Format check: `dart format --output=none --set-exit-if-changed -l 120 $(find lib test -name '*.dart' -not -path 'lib/l10n/*')`
  (the generated `lib/l10n/*.dart` is committed exactly as the tool emits it).
- The current UI is a temporary design of Claude's; it gets rebuilt from the user's Figma.

### 4.4 Porozit N2+ firmware: Porozit_N2_plus
- Hardware: **WT32-SC01 Plus** (ESP32-S3, 3.5" 480×320, 8 MB flash, PSRAM).
- PlatformIO: `espressif32@6.10.0` (Arduino core 2.0.17), env `wt32-sc01-plus`, `default_8MB.csv`, SPIFFS.
- Libraries: LVGL 8.3.6 (SquareLine Studio: `SquareLine/` → `src/ui/`), LovyanGFX, ArduinoNvs, ESP32-audioI2S,
  ArduinoJson 7, NimBLE-Arduino ^2.3.0.
- GPIO: 10 = trigger (3.5 mm jack), 13 = head plugged in, 11 = battery, 12 = charging, 14 = button, 21 = power hold.
- Storage: results in EEPROM (24 doubles), settings in NVS, SPIFFS `data/lang` (translations from `lang.xlsx`), `data/song` (sounds).
- New: `src/PorozitBle.*` (NimBLE GATT, incoming messages handled in `loop()`, MTU check), `src/AppLink.*` (protocol v2),
  hooks in `src/main.cpp` (started, progress, done, error + partial time, save/delete/reset, plug state). FW_VERSION **2.0.0**.
- Size: flash 58.6%, static RAM 63.6%.
- `Serial.begin(9600)`; native USB-CDC, so the baud rate doesn't matter.
- **`tools/backup_and_flash.sh`**: full 8 MB backup → `backup/porozit_n2_flash_<date>.bin` (+ sha256, `backup/` is in .gitignore),
  size check, `pio run -t upload`, `pio run -t uploadfs`, `pio device monitor`. `--backup` = backup only.
  Restore: `esptool.py --chip esp32s3 --port <port> write_flash 0 backup/<file>.bin`.
  Needs: `brew install platformio esptool`. If it can't connect: hold BOOT while plugging in.

### 4.5 Porozit N2+ mini firmware: Porozit_N2_plus_mini (on hold)
- Waveshare **ESP32-S3-Touch-LCD-1.69**, ESP-IDF **v6.0.1**, NimBLE, cJSON, LVGL 8.4.0 + esp_lvgl_port 2.9.0 (vendored in `components/`).
- `main/`: watch_settings, ble_protocol (v2, model "mini"), measure (v2 events, progress every 500 ms), ble_service
  ("Porozit N2+ mini XXXX", advertises 0xFFF0), main.c (battery heartbeat).
- Project `porozit_n2_mini`, version 2.0.0. CI: `espressif/esp-idf-ci-action` v6.0.1.
- Stores no results (`maxSaved: 0`); the app keeps them. A partial measurement can't be saved.

---

## 5. Where we are now, and what comes next

**Situation:** the big N2+ is connected to the user's **MacBook** over USB-C, and there's a **trigger cable** (jack + pushbutton).
The device still runs the **original firmware**. The user is starting a **local Claude session** on the Mac,
so it can reach the device.

**Next steps (in this order):**
1. On the Mac: `brew install platformio esptool`; `git clone https://github.com/Easternsugar/Porozit_N2_plus.git`.
2. `tools/backup_and_flash.sh`: **first back up the old firmware** (the user's explicit request), then flash 2.0.0 + SPIFFS.
   If it can't connect: hold BOOT while plugging in.
3. Check in the serial log:
   - clean boot, the UI comes up, languages and sounds load;
   - the trigger pushbutton starts/stops a measurement, and the time is right;
   - pulling the head out mid-measurement → error + partial time; plugging it back in → Save works;
   - BLE advertising "Porozit N2+ XXXX".
4. Test with the app: Android APK (GitHub Actions → Hello_App → latest run → Artifacts) or `flutter run`.
   Connecting, live time, save/delete/reset both ways, list sync, device settings
   (unit, beep, sleep, language, volume, brightness).
5. Fix bugs in the relevant repo, push, CI green.

---

## 6. Open questions, ideas, later tasks
- **App design from Figma:** the user is making it; then Claude rebuilds the UI (send frame links with `node-id`).
- **PDF certificate:** owner, glider make/model/size/serial number/year, results (sec + l/m²/min, average, rating), logo.
- **Logo SVG, app icon, design tokens JSON**: the user will send them (the header currently has temporary "HELLO | POROZIT" text).
- **N2+ mini:** the user will design the UI; then a hardware test.
- Worth asking the developer whether there's a newer HELLO app version/branch that used to talk to the watch (only relevant for the past;
  we have our own protocol now).
- PR from the Hello_App branch to a default branch: only if the user asks.
- The N2+'s static RAM is 63.6% used, so keep an eye on memory when adding new features.

### Working rules
- Answer the user in Hungarian; code, comments and commit messages in English; no model name in commits.
- Don't open a PR unless the user asks.
- The user likes proposals followed by execution; product decisions are theirs.

---

## 7. Appendix: the most important code in full

The files are in the repos; the full text is also included here for reference.

### Porozit_N2_plus: PROTOCOL.md (the shared BLE protocol v2)

#### Porozit BLE protocol — version 2

Shared by every Porozit control unit (**Porozit N2+**, **Porozit N2+ mini**) and the **Porozit app**.
The same file lives in all three repositories; change it in all of them together.

##### Transport

| Item | Value |
|---|---|
| Advertised name | starts with `Porozit` — `Porozit N2+ 1A2B`, `Porozit N2+ mini 1A2B` (last 4 hex digits of the MAC) |
| Service | `0xFFF0` (advertised) |
| Characteristic | `0xFFF1` — read, write, notify. Device → app by **notify**, app → device by **write** |
| Encoding | one UTF-8 JSON object per notification / write, no newline |
| Size | ≤ 240 bytes. The app requests ATT MTU 247 right after connecting (iOS negotiates by itself); the device refuses to send anything that does not fit the negotiated MTU |

Every message has `"version": 2` and a `"type"`. Unknown fields must be ignored, so fields can be added
without a version bump. Times are **seconds** (number, 1–3 decimals); the device never converts to
l/m²/min for the protocol — the app does (`l/m²/min = 7500 / sec`).

##### Session start

1. App connects, requests MTU, subscribes to `0xFFF1` notifications.
2. App writes `{"version":2,"type":"command","action":"status"}`.
3. Device answers with `info`, `config`, `state` and `battery` (four notifications, in this order).

##### Device → app

###### `info` — what is connected
```json
{"version":2,"type":"info","model":"n2","fw":"2.0.0","serial":"PZ2401-0042","maxSaved":24}
```
`model`: `"n2"` (N2+) or `"mini"` (N2+ mini). `maxSaved`: how many results the device itself stores (`0` on the N2+ mini,
which keeps nothing and relies on the app). `serial` may be empty.

###### `state` — where the measurement cycle is
```json
{"version":2,"type":"state","state":"result","time":520.3,"count":3,"plugged":true}
```
| `state` | meaning |
|---|---|
| `idle` | ready, waiting for the trigger |
| `measuring` | timer running (`time` = elapsed so far) |
| `result` | finished, waiting for Save or Delete (`time` = the result) |
| `error` | last measurement failed (measuring head pulled out mid-measurement). N2+: cleared by Delete. N2+ mini: cleared by plugging the head back in |

`count`: results saved so far in this session. `plugged`: measuring head connected.

###### `measurement` — events, as they happen
```json
{"version":2,"type":"measurement","event":"started"}
{"version":2,"type":"measurement","event":"progress","time":12.4}
{"version":2,"type":"measurement","event":"done","time":520.3}
{"version":2,"type":"measurement","event":"save","time":520.3,"count":4}
{"version":2,"type":"measurement","event":"delete"}
{"version":2,"type":"measurement","event":"reset"}
{"version":2,"type":"measurement","event":"error","reason":"unplugged"}
```
- `progress` is sent about twice a second while measuring.
- `save` / `delete` are sent whatever triggered them (button on the device or command from the app),
  so the app only has to follow events. `count` in `save` is the new number of saved results.
- `reset`: all saved results were cleared.
- `error` `reason`: `"unplugged"` (head pulled out while measuring). The N2+ includes the partial
  `time`, which can still be saved.

###### `saved` — stored results (answer to `list`)
```json
{"version":2,"type":"saved","index":1,"time":482.1}
```
One per stored result, `index` 1-based, followed by a `state`. Only devices with `maxSaved > 0`.

###### `config` — current settings
```json
{"version":2,"type":"config","unit":"sec","beep":60,"sleep":600,"volume":60,"brightness":80,"language":"hu"}
```
| key | values |
|---|---|
| `unit` | `"sec"` or `"pma"` (l/m²/min) — what the device's big number shows |
| `beep` | reminder beep interval in seconds, `0` = off. N2+ mini: 0, 60, 180, 300, 600. N2+: also 30 |
| `sleep` | auto sleep after seconds of inactivity: 300, 600, 1800 |
| `volume` | 0–100 (%), mapped to the device's levels; 0 = silent |
| `brightness` | 0–100 (%), mapped to the device's levels |
| `language` | `"en"`, `"hu"`, `"de"`, `"es"`, `"fr"` |

Sent on `status` and after every successful config change, so the app always shows what the device
actually applied (after mapping to its levels).

###### `battery`
```json
{"version":2,"type":"battery","battery":71,"charging":false}
```
On `status` and every 10 seconds.

###### `result` — answer to a command or config write
```json
{"version":2,"type":"result","request":"save","status":"OK","message":"OK"}
{"version":2,"type":"result","request":"config","status":"ERROR","message":"beep: expected one of 0,60,180,300,600"}
```
`status` is exactly `"OK"` or `"ERROR"`. `request` is `save`, `delete`, `reset`, `list`, `config` or
`unknown`.

##### App → device

```json
{"version":2,"type":"command","action":"save"}
{"version":2,"type":"command","action":"delete"}
{"version":2,"type":"command","action":"reset"}
{"version":2,"type":"command","action":"list"}
{"version":2,"type":"command","action":"status"}
{"version":2,"type":"config","unit":"pma","brightness":60}
```
- `save` / `delete` act exactly like the buttons on the device: only in `result`. N2+: delete also in
  `error`, and save too once the head is plugged back in — this stores the partial time of the
  interrupted measurement (deliberate). On the N2+ `save` fails with "storage full" when `maxSaved` results are stored. Answered by a `result`, and on success also by the matching `measurement` event.
- `reset` clears all saved results (`measurement` `reset` event follows).
- `config` may carry any subset of the keys above; the device validates all of them first and
  applies all or nothing, then answers with `result` and a fresh `config`.
- `status` is answered by `info`, `config`, `state`, `battery` (no `result`).

##### Version history

- **2** — unified for N2+ and N2+ mini: `event` instead of `alert`, numeric times, `progress` and `error`
  events, `info` / `state` / `battery` / `saved` messages, config in physical units (seconds, %),
  multi-key config. Version 1 (N2+ mini only) is no longer accepted.
- **1** — first N2+ mini protocol (`alert`, heartbeat without version, index-based settings).

### Backup and flash script — `Porozit_N2_plus: tools/backup_and_flash.sh`

````bash
#!/usr/bin/env bash
# Backs up the whole flash of a Porozit N2+ connected over USB, then flashes this firmware and
# its SPIFFS data (languages, sounds). Run from the repository root:
#   tools/backup_and_flash.sh            back up, then flash
#   tools/backup_and_flash.sh --backup   back up only
# Needs PlatformIO and esptool (macOS: brew install platformio esptool).
set -euo pipefail
cd "$(dirname "$0")/.."

esptool=$(command -v esptool.py || command -v esptool || true)
if [[ -z "$esptool" ]] || ! command -v pio >/dev/null; then
  echo "Install the tools first:  brew install platformio esptool   (or: pip3 install platformio esptool)" >&2
  exit 1
fi

ports=( $(ls /dev/cu.usbmodem* /dev/ttyACM* /dev/cu.usbserial* /dev/ttyUSB* 2>/dev/null || true) )
if (( ${#ports[@]} != 1 )); then
  echo "Expected exactly one device port, found: ${ports[*]:-none}. Plug in only the N2+ (USB-C), or set PORT=..." >&2
  [[ -n "${PORT:-}" ]] || exit 1
fi
port=${PORT:-${ports[0]}}
echo "Device port: $port"

mkdir -p backup
backup="backup/porozit_n2_flash_$(date +%Y%m%d_%H%M%S).bin"
echo "Reading the whole 8 MB flash into $backup (takes about 2 minutes)..."
"$esptool" --chip esp32s3 --port "$port" --baud 921600 read_flash 0 0x800000 "$backup"
size=$(wc -c < "$backup" | tr -d ' ')
if [[ "$size" != 8388608 ]]; then
  echo "Backup is $size bytes, expected 8388608. Not flashing." >&2
  exit 1
fi
shasum -a 256 "$backup" | tee "$backup.sha256"
echo "Backup OK. Restore it any time with:"
echo "  $esptool --chip esp32s3 --port $port write_flash 0 $backup"

[[ "${1:-}" == "--backup" ]] && exit 0

echo "Flashing firmware..."
pio run -t upload --upload-port "$port"
echo "Flashing SPIFFS data (languages, sounds)..."
pio run -t uploadfs --upload-port "$port"
echo "Done. Watching the log (Ctrl+C to quit)..."
pio device monitor --port "$port"

````

### N2+ BLE layer (header) — `Porozit_N2_plus: src/PorozitBle.h`

````cpp
#pragma once

#include <Arduino.h>
#include <functional>
#include <string>

// BLE transport of the Porozit protocol v2 (PROTOCOL.md): service 0xFFF0, characteristic 0xFFF1
// (read / write / notify), one JSON object per message.
//
// Incoming writes are queued on the NimBLE task and handed to the handler from poll(), so the
// handler runs in loop() next to LVGL and may touch the UI. send() is for loop() as well.
class PorozitBle
{
public:
  using MessageHandler = std::function<void(const char *json, size_t length)>;

  // Starts advertising as "<namePrefix> XXXX" (last two MAC bytes).
  void begin(const char *namePrefix, MessageHandler onMessage);

  // Call from loop(): dispatches queued messages to the handler.
  void poll();

  // True when a phone is connected and subscribed, i.e. send() can reach it.
  bool canSend() const;

  // Notify one JSON message. Refused (false) when nobody listens or it does not fit the MTU.
  bool send(const char *json, int length);

  const char *name() const { return name_; }

private:
  char name_[32] = {0};
  MessageHandler onMessage_;
};

extern PorozitBle porozitBle;

````

### N2+ BLE layer — `Porozit_N2_plus: src/PorozitBle.cpp`

````cpp
#include "PorozitBle.h"

#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <atomic>
#include <cstring>

PorozitBle porozitBle;

namespace
{
constexpr uint16_t kServiceUuid = 0xFFF0;
constexpr uint16_t kCharacteristicUuid = 0xFFF1;
constexpr size_t kMaxMessage = 244; // fits one notification at the MTU the app requests (247)
constexpr size_t kQueueLength = 4;

struct RxMessage
{
  uint16_t length;
  char json[kMaxMessage + 1];
};

QueueHandle_t gRxQueue = nullptr;
NimBLEServer *gServer = nullptr;
NimBLECharacteristic *gCharacteristic = nullptr;
std::atomic<bool> gSubscribed{false};
std::atomic<uint16_t> gConnHandle{BLE_HS_CONN_HANDLE_NONE};

class ServerCallbacks : public NimBLEServerCallbacks
{
  void onConnect(NimBLEServer *, NimBLEConnInfo &connInfo) override
  {
    gConnHandle = connInfo.getConnHandle();
  }

  void onDisconnect(NimBLEServer *, NimBLEConnInfo &, int) override
  {
    gSubscribed = false;
    gConnHandle = BLE_HS_CONN_HANDLE_NONE;
    NimBLEDevice::startAdvertising();
  }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks
{
  void onWrite(NimBLECharacteristic *characteristic, NimBLEConnInfo &) override
  {
    // Runs on the NimBLE host task: copy and hand over, never block here.
    const NimBLEAttValue value = characteristic->getValue();
    if (value.length() == 0 || value.length() > kMaxMessage || gRxQueue == nullptr)
    {
      return;
    }
    RxMessage message;
    message.length = value.length();
    memcpy(message.json, value.data(), value.length());
    message.json[value.length()] = '\0';
    xQueueSend(gRxQueue, &message, 0);
  }

  void onSubscribe(NimBLECharacteristic *, NimBLEConnInfo &, uint16_t subValue) override
  {
    gSubscribed = subValue != 0;
  }
};

ServerCallbacks gServerCallbacks;
CharacteristicCallbacks gCharacteristicCallbacks;
} // namespace

void PorozitBle::begin(const char *namePrefix, MessageHandler onMessage)
{
  onMessage_ = std::move(onMessage);
  gRxQueue = xQueueCreate(kQueueLength, sizeof(RxMessage));

  uint8_t mac[6] = {0};
  esp_read_mac(mac, ESP_MAC_BT);
  snprintf(name_, sizeof(name_), "%s %02X%02X", namePrefix, mac[4], mac[5]);

  NimBLEDevice::init(name_);
  gServer = NimBLEDevice::createServer();
  gServer->setCallbacks(&gServerCallbacks);

  NimBLEService *service = gServer->createService(NimBLEUUID(kServiceUuid));
  gCharacteristic = service->createCharacteristic(
      NimBLEUUID(kCharacteristicUuid),
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  gCharacteristic->setCallbacks(&gCharacteristicCallbacks);

  NimBLEAdvertising *advertising = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advertisement;
  advertisement.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
  advertisement.addServiceUUID(NimBLEUUID(kServiceUuid));
  advertisement.setName(name_);
  advertising->setAdvertisementData(advertisement);
  advertising->start();
}

void PorozitBle::poll()
{
  if (gRxQueue == nullptr || !onMessage_)
  {
    return;
  }
  RxMessage message;
  while (xQueueReceive(gRxQueue, &message, 0) == pdTRUE)
  {
    onMessage_(message.json, message.length);
  }
}

bool PorozitBle::canSend() const
{
  return gSubscribed && gConnHandle != BLE_HS_CONN_HANDLE_NONE;
}

bool PorozitBle::send(const char *json, int length)
{
  if (!canSend() || length <= 0 || (size_t)length > kMaxMessage)
  {
    return false;
  }
  // A notification carries MTU - 3 bytes; anything longer would arrive truncated as broken JSON.
  const uint16_t mtu = gServer->getPeerMTU(gConnHandle);
  if (mtu != 0 && (size_t)length + 3 > mtu)
  {
    Serial.printf("[BLE] %d byte message does not fit MTU %u, dropped\n", length, mtu);
    return false;
  }
  gCharacteristic->setValue(reinterpret_cast<const uint8_t *>(json), (size_t)length);
  return gCharacteristic->notify();
}

````

### N2+ protocol v2 (header) — `Porozit_N2_plus: src/AppLink.h`

````cpp
#pragma once

// Link to the Porozit app over BLE: Porozit protocol v2 (PROTOCOL.md) on top of PorozitBle.
// All functions are for loop() / LVGL context.

// Start BLE once the UI and settings are up (end of initialize()).
void appLinkBegin();

// Handle incoming messages and the periodic battery report. Call every loop().
void appLinkLoop();

// "measurement" event. Negative time / count and a null reason are left out of the message.
void appLinkEvent(const char *event, double time = -1.0, int count = -1, const char *reason = nullptr);

// Live timer while measuring; throttled to twice a second.
void appLinkProgress(double time);

// "state" message, for changes that are not measurement events (head plugged / unplugged).
void appLinkSendState();

````

### N2+ protocol v2 — `Porozit_N2_plus: src/AppLink.cpp`

````cpp
#include "AppLink.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ArduinoNvs.h>
#include <EEPROM.h>
#include <math.h>

#include "Display.h"
#include "Globals.h"
#include "LanguageManager.h"
#include "PorozitBle.h"
#include "ui/ui.h"

#ifndef FW_VERSION
#define FW_VERSION "0.0.0"
#endif

// From main.cpp
void updateTexts();
bool readChargeStateStable();

namespace
{
constexpr int kProtocolVersion = 2;
constexpr uint32_t kProgressIntervalMs = 500;
constexpr uint32_t kBatteryIntervalMs = 10000;

// Native levels the settings screen offers (see setVolume() / setBrigthness() in main.cpp)
constexpr int kVolumeLevels[] = {0, 4, 8, 12, 16, 21};
constexpr int kVolumeMax = 21;
constexpr int kBrightnessLevels[] = {25, 51, 102, 153, 204, 255};
constexpr int kBrightnessMax = 255;
constexpr int kBeepCount = sizeof(beepValues) / sizeof(beepValues[0]);
constexpr int kSleepCount = sizeof(autoSleepValues) / sizeof(autoSleepValues[0]);

bool gStarted = false;
uint32_t gLastProgressMs = 0;
uint32_t gLastBatteryMs = 0;

void sendDoc(JsonDocument &doc)
{
  doc["version"] = kProtocolVersion;
  char buffer[240];
  const size_t length = serializeJson(doc, buffer, sizeof(buffer));
  if (length == 0 || length >= sizeof(buffer))
  {
    Serial.println("[BLE] message too long, not sent");
    return;
  }
  porozitBle.send(buffer, (int)length);
}

double roundMs(double seconds) { return round(seconds * 1000.0) / 1000.0; }

template <size_t N>
int nearestLevel(const int (&levels)[N], int target)
{
  int best = levels[0];
  for (int level : levels)
  {
    if (abs(level - target) < abs(best - target))
      best = level;
  }
  return best;
}

int toPercent(int level, int max) { return (level * 100 + max / 2) / max; }

const char *stateName()
{
  if (measurementData.isMeas)
    return "measuring";
  if (timeSettings.measureTime > prell && !deviceSettings.allowMeasure)
    return measurementData.measureError ? "error" : "result";
  return "idle";
}

void sendResult(const char *request, const char *error)
{
  JsonDocument doc;
  doc["type"] = "result";
  doc["request"] = request;
  doc["status"] = error == nullptr ? "OK" : "ERROR";
  doc["message"] = error == nullptr ? "OK" : error;
  sendDoc(doc);
}

void sendInfo()
{
  JsonDocument doc;
  doc["type"] = "info";
  doc["model"] = "n2";
  doc["fw"] = FW_VERSION;
  doc["serial"] = NVS.getString("sn");
  doc["maxSaved"] = EEPROM_COUNT;
  sendDoc(doc);
}

void sendConfig()
{
  JsonDocument doc;
  doc["type"] = "config";
  doc["unit"] = deviceSettings.isPMA ? "pma" : "sec";
  doc["beep"] = beepValues[deviceSettings.bIndex].beepValue;
  doc["sleep"] = autoSleepValues[deviceSettings.sIndex].sleepValue;
  doc["volume"] = toPercent(deviceSettings.volume, kVolumeMax);
  doc["brightness"] = toPercent(deviceSettings.brightness, kBrightnessMax);
  doc["language"] = langValues[deviceSettings.lIndex].langCode;
  sendDoc(doc);
}

void sendBattery()
{
  // Linear between the shutdown threshold and a full cell; the device itself only shows bands.
  const float voltage = batteryData.batteryVoltage;
  if (voltage <= 0.0f)
    return;
  const float fraction = (voltage - batteryDead) / (chargedFull - batteryDead);
  JsonDocument doc;
  doc["type"] = "battery";
  doc["battery"] = constrain((int)lroundf(fraction * 100.0f), 0, 100);
  doc["charging"] = readChargeStateStable();
  sendDoc(doc);
}

void sendSaved()
{
  const int count = min(measurementData.measCount, EEPROM_COUNT);
  for (int i = 0; i < count; i++)
  {
    double seconds = 0;
    EEPROM.get(i * sizeof(double), seconds);
    JsonDocument doc;
    doc["type"] = "saved";
    doc["index"] = i + 1;
    doc["time"] = roundMs(seconds);
    sendDoc(doc);
  }
}

void setUnit(bool pma)
{
  // changeUnit() reads the unit from which toggle half is visible, like a tap on the switch.
  if (pma)
  {
    lv_obj_add_flag(ui_secOff, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_pmaOff, LV_OBJ_FLAG_HIDDEN);
  }
  else
  {
    lv_obj_add_flag(ui_pmaOff, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ui_secOff, LV_OBJ_FLAG_HIDDEN);
  }
  changeUnit(nullptr);
}

// Validates every key first and applies all or nothing. Returns an error or nullptr.
const char *applyConfig(JsonDocument &doc)
{
  int unit = -1, beepIndex = -1, sleepIndex = -1, volume = -1, brightness = -1, langIndex = -1;

  if (doc["unit"].is<const char *>())
  {
    const char *value = doc["unit"];
    if (strcmp(value, "sec") == 0)
      unit = 0;
    else if (strcmp(value, "pma") == 0)
      unit = 1;
  }
  if (!doc["unit"].isNull() && unit < 0)
    return "unit: expected \"sec\" or \"pma\"";

  if (!doc["beep"].isNull())
  {
    for (int i = 0; doc["beep"].is<int>() && i < kBeepCount; i++)
      if (beepValues[i].beepValue == doc["beep"].as<int>())
        beepIndex = i;
    if (beepIndex < 0)
      return "beep: expected one of 0,30,60,180,300,600";
  }

  if (!doc["sleep"].isNull())
  {
    for (int i = 0; doc["sleep"].is<int>() && i < kSleepCount; i++)
      if (autoSleepValues[i].sleepValue == doc["sleep"].as<int>())
        sleepIndex = i;
    if (sleepIndex < 0)
      return "sleep: expected one of 300,600,1800";
  }

  if (!doc["volume"].isNull())
  {
    if (!doc["volume"].is<int>() || doc["volume"].as<int>() < 0 || doc["volume"].as<int>() > 100)
      return "volume: expected 0-100";
    volume = nearestLevel(kVolumeLevels, (doc["volume"].as<int>() * kVolumeMax + 50) / 100);
  }

  if (!doc["brightness"].isNull())
  {
    if (!doc["brightness"].is<int>() || doc["brightness"].as<int>() < 0 || doc["brightness"].as<int>() > 100)
      return "brightness: expected 0-100";
    brightness = nearestLevel(kBrightnessLevels, (doc["brightness"].as<int>() * kBrightnessMax + 50) / 100);
  }

  if (!doc["language"].isNull())
  {
    for (int i = 0; doc["language"].is<const char *>() && i < langNum; i++)
      if (strcmp(langValues[i].langCode, doc["language"].as<const char *>()) == 0)
        langIndex = i;
    if (langIndex < 0)
      return "language: expected en, hu, de, es or fr";
  }

  if (unit < 0 && beepIndex < 0 && sleepIndex < 0 && volume < 0 && brightness < 0 && langIndex < 0)
    return "no known config key";

  if (unit >= 0)
    setUnit(unit == 1);
  if (beepIndex >= 0)
  {
    deviceSettings.bIndex = beepIndex;
    NVS.setInt("beepTime", beepIndex);
    prepareBeep(nullptr);
  }
  if (sleepIndex >= 0)
  {
    deviceSettings.sIndex = sleepIndex;
    NVS.setInt("sleepTime", sleepIndex);
    prepareSleep(nullptr);
  }
  if (volume >= 0)
  {
    deviceSettings.volume = volume;
    audio.setVolume(volume);
    NVS.setInt("volume", volume);
    prepareVolume(nullptr);
  }
  if (brightness >= 0)
  {
    deviceSettings.brightness = brightness;
    tft.setBrightness(brightness);
    NVS.setInt("brightness", brightness);
    prepareBrigthness(nullptr);
  }
  if (langIndex >= 0)
  {
    deviceSettings.lIndex = langIndex;
    NVS.setInt("lIndex", langIndex);
    lang.setLanguage(langValues[langIndex].langCode);
    updateTexts();
  }
  return nullptr;
}

void handleCommand(const char *action)
{
  const char *state = stateName();
  if (strcmp(action, "save") == 0)
  {
    // Like the Save button: a result, or the partial time of an interrupted measurement once the
    // head is plugged back in (deliberately allowed on the N2+).
    const bool partial = strcmp(state, "error") == 0 && deviceSettings.isPlugged;
    if (strcmp(state, "result") != 0 && !partial)
      return sendResult("save", "nothing to save");
    if (measurementData.measCount >= EEPROM_COUNT)
      return sendResult("save", "storage full, reset first");
    saveflash(nullptr);
    sendResult("save", nullptr);
  }
  else if (strcmp(action, "delete") == 0)
  {
    if (strcmp(state, "result") != 0 && strcmp(state, "error") != 0)
      return sendResult("delete", "nothing to delete");
    deletemes(nullptr);
    sendResult("delete", nullptr);
  }
  else if (strcmp(action, "reset") == 0)
  {
    if (measurementData.isMeas)
      return sendResult("reset", "measurement running");
    resetMeas(nullptr);
    sendResult("reset", nullptr);
  }
  else if (strcmp(action, "list") == 0)
  {
    sendResult("list", nullptr);
    sendSaved();
    appLinkSendState();
  }
  else if (strcmp(action, "status") == 0)
  {
    sendInfo();
    sendConfig();
    appLinkSendState();
    sendBattery();
  }
  else
  {
    sendResult("unknown", "unknown action");
  }
}

void onMessage(const char *json, size_t length)
{
  // Phone activity counts as activity: do not fall asleep under the user's hands.
  timeSettings.startTime = millis();

  JsonDocument doc;
  if (deserializeJson(doc, json, length) != DeserializationError::Ok || !doc.is<JsonObject>())
    return sendResult("unknown", "malformed json");
  if ((doc["version"] | 0) != kProtocolVersion)
    return sendResult("unknown", "unsupported version, expected 2");

  const char *type = doc["type"] | "";
  if (strcmp(type, "command") == 0)
  {
    handleCommand(doc["action"] | "");
  }
  else if (strcmp(type, "config") == 0)
  {
    const char *error = applyConfig(doc);
    sendResult("config", error);
    if (error == nullptr)
      sendConfig();
  }
  else
  {
    sendResult("unknown", "unknown type");
  }
}
} // namespace

void appLinkBegin()
{
  if (gStarted)
    return;
  gStarted = true;
  porozitBle.begin("Porozit N2+", onMessage);
}

void appLinkLoop()
{
  if (!gStarted)
    return;
  porozitBle.poll();
  const uint32_t now = millis();
  if (now - gLastBatteryMs >= kBatteryIntervalMs)
  {
    gLastBatteryMs = now;
    if (porozitBle.canSend())
      sendBattery();
  }
}

void appLinkEvent(const char *event, double time, int count, const char *reason)
{
  if (!porozitBle.canSend())
    return;
  if (strcmp(event, "started") == 0)
    gLastProgressMs = millis();
  JsonDocument doc;
  doc["type"] = "measurement";
  doc["event"] = event;
  if (time >= 0)
    doc["time"] = roundMs(time);
  if (count >= 0)
    doc["count"] = count;
  if (reason != nullptr)
    doc["reason"] = reason;
  sendDoc(doc);
}

void appLinkProgress(double time)
{
  const uint32_t now = millis();
  if (now - gLastProgressMs < kProgressIntervalMs)
    return;
  gLastProgressMs = now;
  appLinkEvent("progress", time);
}

void appLinkSendState()
{
  if (!porozitBle.canSend())
    return;
  JsonDocument doc;
  doc["type"] = "state";
  doc["state"] = stateName();
  doc["time"] = strcmp(stateName(), "idle") == 0 ? 0.0 : roundMs(timeSettings.measureTime);
  doc["count"] = measurementData.measCount;
  doc["plugged"] = deviceSettings.isPlugged;
  sendDoc(doc);
}

````

### N2+ — the hooks in main.cpp (excerpt: lines that call AppLink, with context)

````cpp
 559-
 560-    measurementData.measCount++;
 561-    NVS.setInt("measCount", measurementData.measCount);
 562:    appLinkEvent("save", savedTime, measurementData.measCount);
 563-
 564-    char buf[17];
 --
 622-  deviceSettings.autosleep = true;
 623-
 624-  logDebug("INFO", "MEASURE", "Delete finished. values reset, allowMeasure=%d", deviceSettings.allowMeasure);
 625:  appLinkEvent("delete");
 626-}
 627-
 --
 783-  _ui_state_modify(ui_reset, LV_STATE_DISABLED, _UI_MODIFY_STATE_ADD);
 784-
 785-  logDebug("WARN", "MEASURE", "All measurements were reset. previousCount=%d", previousCount);
 786:  appLinkEvent("reset");
 787-}
 788-
 --
 1199-  if (measurementData.isMeas && (digitalRead(input) == HIGH) && !(timeSettings.measureTime <= prell))
 1200-  {
 1201-    logDebug("INFO", "MEASURE", "Measurement finished. duration=%.3fs, PMA=%.2f", timeSettings.measureTime, calcPMA(timeSettings.measureTime));
 1202:    appLinkEvent("done", timeSettings.measureTime);
 1203-
 1204-    NVS.setFloat("lastMeasure", timeSettings.measureTime);
 --
 1239-      }
 1240-
 1241-      deviceSettings.isPlugged = true;
 1242:      appLinkSendState();
 1243-
 1244-      delay(1000);
 --
 1250-        timeSettings.startMeasureTime = millis();
 1251-        soundManager.play();
 1252-        logDebug("INFO", "MEASURE", "Measurement started. savedCount=%d", measurementData.measCount);
 1253:        appLinkEvent("started");
 1254-      }
 1255-      timeSettings.currentMeasureTime = millis();
 1256-
 1257-      timeSettings.measureTime = (timeSettings.currentMeasureTime - timeSettings.startMeasureTime) / 1000;
 1258:      appLinkProgress(timeSettings.measureTime);
 1259-
 1260-      char pmaBuff[9];
 --
 1378-    deviceSettings.isPlugged = false;
 1379-    measurementData.measureError = true;
 1380-    deviceSettings.autosleep = true;
 1381:    appLinkEvent("error", timeSettings.measureTime, -1, "unplugged"); // partial time, can still be saved
 1382-  }
 1383-  else if ((digitalRead(input2) == HIGH) && !measurementData.measureError)
 --
 1396-      deviceSettings.lastState = true;
 1397-      deviceSettings.isPlugged = false;
 1398-      deviceSettings.autosleep = true;
 1399:      appLinkSendState();
 1400-
 1401-      delay(500);
 --
 1917-    _ui_state_modify(ui_reset, LV_STATE_DISABLED, _UI_MODIFY_STATE_ADD);
 1918-  }
 1919-
 1920:  appLinkBegin();
 1921-
 1922-  logDebug("INFO", "INIT", "Initialization finished");
 --
 2026-void loop()
 2027-{
 2028-  measure();
 2029:  appLinkLoop();
 2030-
 2031-  if (APP_HEARTBEAT_DEBUG && (g_lastHeartbeatLogMs == 0 || (millis() - g_lastHeartbeatLogMs >= 30000)))
````

### N2+ build configuration — `Porozit_N2_plus: platformio.ini`

````ini
; PlatformIO Project Configuration File
;
;   Build options: build flags, source filter
;   Upload options: custom upload port, speed and extra flags
;   Library options: dependencies, extra library storages
;   Advanced options: extra scripting
;
; Please visit documentation for the other options and examples
; https://docs.platformio.org/page/projectconf.html

[env:wt32-sc01-plus]
platform = espressif32@6.10.0
board = esp32-s3-devkitc-1
framework = arduino
board_build.partitions = default_8MB.csv
board_build.filesystem = spiffs
board_build.mcu = esp32s3
board_build.f_cpu = 240000000L
lib_deps = 
	lovyan03/LovyanGFX@^0.4.18
	rpolitex/ArduinoNvs@^2.5
	esphome/ESP32-audioI2S@^2.0.7
	lvgl/lvgl@8.3.6
	bblanchon/ArduinoJson@^7.4.1
	h2zero/NimBLE-Arduino@^2.3.0
build_flags = 
	-DBOARD_HAS_PSRAM
	-mfix-esp32-psram-cache-issue
	-I lib
	-DLGFX_USE_V1
	-D PLUS=1
	-D LV_MEM_SIZE="(96U * 1024U)"
	-D APP_DEBUG_SERIAL=1
	-D APP_HEARTBEAT_DEBUG=1
	-D ARDUINONVS_SILENT=1

````

### App — `Hello_App: lib/core/units.dart`

````dart
/// Units the Porozit can show a result in. Seconds are the device's native measurement.
enum MeasurementUnit {
  seconds('sec'),
  litresPerM2Min('l/m²/min');

  const MeasurementUnit(this.symbol);

  final String symbol;

  MeasurementUnit get other =>
      this == MeasurementUnit.seconds ? MeasurementUnit.litresPerM2Min : MeasurementUnit.seconds;
}

class PorozitConversion {
  PorozitConversion._();

  /// Flow (l/m²/min) = [flowConstant] / time (sec).
  ///
  /// Derived from the published result intervals (20 sec ≙ 375 l/m²/min, 14 sec ≙ 536 l/m²/min)
  /// and the device mock-ups (466.6 sec ≙ 16.07, 520.3 sec ≙ 14.42).
  /// TODO: confirm the exact constant with the firmware developer.
  static const double flowConstant = 7500.0;

  static double secondsToFlow(double seconds) => seconds <= 0 ? 0 : flowConstant / seconds;

  static double flowToSeconds(double flow) => flow <= 0 ? 0 : flowConstant / flow;

  /// Value of a measurement (given in seconds) expressed in [unit].
  static double valueIn(MeasurementUnit unit, double seconds) =>
      unit == MeasurementUnit.seconds ? seconds : secondsToFlow(seconds);

  /// Device-style formatting: 1 decimal for seconds, 2 for l/m²/min, comma thousands separator.
  static String format(MeasurementUnit unit, double seconds) =>
      formatNumber(valueIn(unit, seconds), unit == MeasurementUnit.seconds ? 1 : 2);

  static String formatNumber(double value, int decimals) {
    final fixed = value.toStringAsFixed(decimals);
    final dot = fixed.indexOf('.');
    final whole = dot < 0 ? fixed : fixed.substring(0, dot);
    final fraction = dot < 0 ? '' : fixed.substring(dot);
    final buffer = StringBuffer();
    for (var i = 0; i < whole.length; i++) {
      if (i > 0 && (whole.length - i) % 3 == 0 && whole[i - 1] != '-') buffer.write(',');
      buffer.write(whole[i]);
    }
    return '$buffer$fraction';
  }
}

````

### App — `Hello_App: lib/core/rating.dart`

````dart
/// Porozit result intervals (from the product page):
///  - Good:       > 20 sec   (< 375 l/m²/min)
///  - Acceptable: 14–20 sec  (375–536 l/m²/min)
///  - Fail:       ≤ 14 sec   (> 536 l/m²/min)
///
/// Seconds are the canonical value; the l/m²/min limits follow from the conversion constant.
enum ResultRating {
  good,
  acceptable,
  fail;

  static const double goodAboveSeconds = 20.0;
  static const double failAtOrBelowSeconds = 14.0;

  static ResultRating of(double seconds) {
    if (seconds > goodAboveSeconds) return ResultRating.good;
    if (seconds > failAtOrBelowSeconds) return ResultRating.acceptable;
    return ResultRating.fail;
  }
}

````

### App — `Hello_App: lib/core/session.dart`

````dart
import 'rating.dart';

/// One saved measurement. [index] is 1-based, as shown on the device ("Measurement | 3").
class Measurement {
  const Measurement({required this.index, required this.seconds, required this.timestamp});

  final int index;
  final double seconds;
  final DateTime timestamp;

  ResultRating get rating => ResultRating.of(seconds);

  Measurement withIndex(int index) => Measurement(index: index, seconds: seconds, timestamp: timestamp);

  @override
  bool operator ==(Object other) =>
      other is Measurement && other.index == index && other.seconds == seconds && other.timestamp == timestamp;

  @override
  int get hashCode => Object.hash(index, seconds, timestamp);
}

/// Mirrors the device workflow: a finished result is *pending* until the user saves it
/// (it joins the average) or deletes it (it is discarded). Immutable; every action returns a new state.
class MeasurementSession {
  const MeasurementSession({this.saved = const [], this.pendingSeconds});

  final List<Measurement> saved;
  final double? pendingSeconds;

  int get count => saved.length;

  /// Average of the saved times. The flow average is derived from it, as on the device.
  double? get averageSeconds =>
      saved.isEmpty ? null : saved.fold<double>(0, (sum, m) => sum + m.seconds) / saved.length;

  /// The latest saved measurement, if any.
  Measurement? get last => saved.isEmpty ? null : saved.last;

  MeasurementSession withPending(double seconds) => MeasurementSession(saved: saved, pendingSeconds: seconds);

  MeasurementSession savePending(DateTime now) {
    final seconds = pendingSeconds;
    if (seconds == null) return this;
    return MeasurementSession(
      saved: [
        ...saved,
        Measurement(index: saved.length + 1, seconds: seconds, timestamp: now),
      ],
    );
  }

  MeasurementSession deletePending() => MeasurementSession(saved: saved);

  MeasurementSession remove(int index) {
    final kept = saved.where((m) => m.index != index).toList();
    return MeasurementSession(
      saved: [for (var i = 0; i < kept.length; i++) kept[i].withIndex(i + 1)],
      pendingSeconds: pendingSeconds,
    );
  }

  MeasurementSession reset() => const MeasurementSession();

  /// Compact text form of the saved measurements for local persistence ("seconds@epochMs;...").
  String encode() => saved.map((m) => '${m.seconds}@${m.timestamp.millisecondsSinceEpoch}').join(';');

  static MeasurementSession decode(String? text) {
    if (text == null || text.trim().isEmpty) return const MeasurementSession();
    final entries = <(double, int)>[];
    for (final entry in text.split(';')) {
      final parts = entry.split('@');
      if (parts.length != 2) continue;
      final seconds = double.tryParse(parts[0]);
      final millis = int.tryParse(parts[1]);
      if (seconds == null || seconds <= 0 || millis == null) continue;
      entries.add((seconds, millis));
    }
    return MeasurementSession(
      saved: [
        for (var i = 0; i < entries.length; i++)
          Measurement(
            index: i + 1,
            seconds: entries[i].$1,
            timestamp: DateTime.fromMillisecondsSinceEpoch(entries[i].$2),
          ),
      ],
    );
  }
}

````

### App — `Hello_App: lib/core/protocol.dart`

````dart
import 'dart:convert';

/// Porozit BLE protocol v2 (PROTOCOL.md), shared with the N2+ and N2+ mini firmware.
///
/// Every BLE notification carries one JSON object. Version 1 messages from N2+ mini firmware that
/// has not been updated yet are understood as well, so those units keep working meanwhile.
const int protocolVersion = 2;

/// `n2`: Porozit N2+ (touchscreen). `mini`: Porozit N2+ mini (watch-style).
enum DeviceModel { n2, mini, unknown }

enum DeviceState { idle, measuring, result, error }

enum MeasurementEventType { started, progress, done, save, delete, reset, error }

sealed class DeviceMessage {
  const DeviceMessage();
}

/// What is connected.
class InfoMessage extends DeviceMessage {
  const InfoMessage({required this.model, required this.firmware, this.serial = '', this.maxSaved = 0});

  final DeviceModel model;
  final String firmware;
  final String serial;

  /// How many results the device stores itself (0 on the N2+ mini).
  final int maxSaved;
}

/// Where the measurement cycle is.
class StateMessage extends DeviceMessage {
  const StateMessage({required this.state, this.time = 0, this.count = 0, this.plugged = true});

  final DeviceState state;
  final double time;
  final int count;
  final bool plugged;
}

class MeasurementEvent extends DeviceMessage {
  const MeasurementEvent(this.type, {this.time, this.count, this.reason});

  final MeasurementEventType type;
  final double? time;
  final int? count;
  final String? reason;

  @override
  bool operator ==(Object other) =>
      other is MeasurementEvent &&
      other.type == type &&
      other.time == time &&
      other.count == count &&
      other.reason == reason;

  @override
  int get hashCode => Object.hash(type, time, count, reason);
}

/// One result stored on the device (answer to `list`).
class SavedMessage extends DeviceMessage {
  const SavedMessage(this.index, this.time);

  final int index;
  final double time;
}

class ConfigMessage extends DeviceMessage {
  const ConfigMessage(this.config);

  final DeviceConfig config;
}

class BatteryMessage extends DeviceMessage {
  const BatteryMessage(this.percent, {this.charging = false});

  final int percent;
  final bool charging;

  @override
  bool operator ==(Object other) => other is BatteryMessage && other.percent == percent && other.charging == charging;

  @override
  int get hashCode => Object.hash(percent, charging);
}

/// Answer to a command or config write.
class ResultMessage extends DeviceMessage {
  const ResultMessage({required this.request, required this.ok, this.message = ''});

  final String request;
  final bool ok;
  final String message;
}

/// Device settings, in protocol units (seconds, percent).
class DeviceConfig {
  const DeviceConfig({
    this.unit = 'sec',
    this.beep = 0,
    this.sleep = 300,
    this.volume = 60,
    this.brightness = 60,
    this.language = 'en',
  });

  final String unit;
  final int beep;
  final int sleep;
  final int volume;
  final int brightness;
  final String language;

  static const beepOptions = [0, 60, 180, 300, 600];
  static const sleepOptions = [300, 600, 1800];
  static const languages = ['en', 'hu', 'de', 'es', 'fr'];

  DeviceConfig copyWith({String? unit, int? beep, int? sleep, int? volume, int? brightness, String? language}) =>
      DeviceConfig(
        unit: unit ?? this.unit,
        beep: beep ?? this.beep,
        sleep: sleep ?? this.sleep,
        volume: volume ?? this.volume,
        brightness: brightness ?? this.brightness,
        language: language ?? this.language,
      );
}

/// Parses one notification. Returns null for anything that is not a message this app understands.
DeviceMessage? parseDeviceMessage(String text) {
  final Object? decoded;
  try {
    decoded = jsonDecode(text.trim());
  } on FormatException {
    return null;
  }
  if (decoded is! Map<String, dynamic>) return null;
  final json = decoded;

  // Version 1 (N2+ mini firmware before 2.0): heartbeat without version, "alert" events.
  if (json['command'] == 'heartbeat') {
    return BatteryMessage(_int(json['battery']) ?? 0, charging: json['charging'] == true);
  }
  final version = _int(json['version']);
  if (version == 1 && json['type'] == 'measurement') {
    return _measurementEvent(json['alert'], json);
  }
  if (version != protocolVersion) return null;

  switch (json['type']) {
    case 'measurement':
      return _measurementEvent(json['event'], json);
    case 'state':
      final state = DeviceState.values.asNameMap()[json['state']];
      if (state == null) return null;
      return StateMessage(
        state: state,
        time: _double(json['time']) ?? 0,
        count: _int(json['count']) ?? 0,
        plugged: json['plugged'] != false,
      );
    case 'info':
      return InfoMessage(
        model: DeviceModel.values.asNameMap()[json['model']] ?? DeviceModel.unknown,
        firmware: '${json['fw'] ?? ''}',
        serial: '${json['serial'] ?? ''}',
        maxSaved: _int(json['maxSaved']) ?? 0,
      );
    case 'saved':
      final index = _int(json['index']);
      final time = _double(json['time']);
      return index == null || time == null ? null : SavedMessage(index, time);
    case 'config':
      const defaults = DeviceConfig();
      return ConfigMessage(
        DeviceConfig(
          unit: json['unit'] == 'pma' ? 'pma' : 'sec',
          beep: _int(json['beep']) ?? defaults.beep,
          sleep: _int(json['sleep']) ?? defaults.sleep,
          volume: _int(json['volume']) ?? defaults.volume,
          brightness: _int(json['brightness']) ?? defaults.brightness,
          language: json['language'] is String ? json['language'] as String : defaults.language,
        ),
      );
    case 'battery':
      return BatteryMessage((_int(json['battery']) ?? 0).clamp(0, 100), charging: json['charging'] == true);
    case 'result':
      return ResultMessage(
        request: '${json['request'] ?? ''}',
        ok: json['status'] == 'OK',
        message: '${json['message'] ?? ''}',
      );
    default:
      return null;
  }
}

MeasurementEvent? _measurementEvent(Object? name, Map<String, dynamic> json) {
  final type = MeasurementEventType.values.asNameMap()[name];
  if (type == null) return null;
  final time = _double(json['time']);
  // v1 "done" with time 0 meant a failed measurement.
  if (type == MeasurementEventType.done && (time == null || time <= 0)) {
    return const MeasurementEvent(MeasurementEventType.error, reason: 'failed');
  }
  return MeasurementEvent(type, time: time, count: _int(json['count']), reason: json['reason'] as String?);
}

/// Numbers may arrive as JSON numbers or (v1 "done") as strings.
double? _double(Object? value) => switch (value) {
  num n => n.toDouble(),
  String s => double.tryParse(s),
  _ => null,
};

int? _int(Object? value) => switch (value) {
  int n => n,
  num n => n.round(),
  String s => int.tryParse(s),
  _ => null,
};

/// Messages the app writes to the device.
abstract final class PorozitCommands {
  static String command(String action) => jsonEncode({'version': protocolVersion, 'type': 'command', 'action': action});

  static String get status => command('status');
  static String get save => command('save');
  static String get delete => command('delete');
  static String get reset => command('reset');
  static String get list => command('list');

  /// Any subset of unit, beep, sleep, volume, brightness, language.
  static String config(Map<String, Object> values) =>
      jsonEncode({'version': protocolVersion, 'type': 'config', ...values});
}

````

### App — `Hello_App: lib/state/porozit_controller.dart`

````dart
import 'dart:async';

import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

import '../core/protocol.dart';
import '../core/session.dart';
import '../core/units.dart';
import '../device/ble_connection.dart';
import '../device/porozit_connection.dart';
import '../device/simulated_connection.dart';

enum MeasurePhase { idle, measuring, result, error }

/// App state: the connected device, the measurement cycle and the saved session.
///
/// The device is the source of truth for the measurement cycle: the app sends commands (save,
/// delete, reset, config) and follows the events the device reports (PROTOCOL.md). A device that
/// stores results itself (N2+, `maxSaved > 0`) also owns the saved list, which the app mirrors.
class PorozitController extends ChangeNotifier {
  PorozitController(this._prefs, {DateTime Function()? clock}) : _clock = clock ?? DateTime.now {
    _unit = MeasurementUnit.values.asNameMap()[_prefs.getString(_unitKey)] ?? MeasurementUnit.seconds;
    _session = MeasurementSession.decode(_prefs.getString(_sessionKey));
  }

  static const _unitKey = 'unit';
  static const _sessionKey = 'session';

  final SharedPreferences _prefs;
  final DateTime Function() _clock;

  late MeasurementUnit _unit;
  late MeasurementSession _session;
  PorozitConnection? _connection;
  StreamSubscription<DeviceMessage>? _messagesSub;
  MeasurePhase _phase = MeasurePhase.idle;
  double? _liveSeconds;
  int? _battery;
  bool _charging = false;
  bool _plugged = true;
  InfoMessage? _info;
  DeviceConfig? _deviceConfig;
  String? _deviceError;

  /// Collects the device's stored results while a `list` answer is coming in.
  List<double>? _incomingSaved;

  MeasurementUnit get unit => _unit;

  MeasurementSession get session => _session;

  MeasurePhase get phase => _phase;

  int? get battery => _battery;

  bool get charging => _charging;

  /// Measuring head plugged into the control unit.
  bool get plugged => _plugged;

  InfoMessage? get deviceInfo => _info;

  DeviceConfig? get deviceConfig => _deviceConfig;

  /// Last refusal from the device (e.g. "storage full"), until [clearDeviceError].
  String? get deviceError => _deviceError;

  PorozitConnection? get connection => _connection;

  ConnectionStatus get connectionStatus => _connection?.status.value ?? ConnectionStatus.disconnected;

  bool get isConnected => connectionStatus == ConnectionStatus.connected;

  bool get isDemo => _connection?.isSimulated ?? false;

  /// The connected device keeps the saved results itself (N2+), so the list follows the device.
  bool get deviceOwnsSession => isConnected && (_info?.maxSaved ?? 0) > 0;

  /// The value the big display shows: the live timer, the unsaved result, or the last saved result.
  double get displaySeconds => switch (_phase) {
    MeasurePhase.measuring => _liveSeconds ?? 0,
    MeasurePhase.result => _session.pendingSeconds ?? 0,
    MeasurePhase.error => _session.pendingSeconds ?? _session.last?.seconds ?? 0,
    MeasurePhase.idle => _session.last?.seconds ?? 0,
  };

  /// Whether [displaySeconds] is a real result (and so can be rated), not a running timer or zero.
  bool get hasDisplayResult => _phase == MeasurePhase.result || (_phase == MeasurePhase.idle && _session.last != null);

  /// A result, or on the N2+ also the partial time of an interrupted measurement once the measuring
  /// head is plugged back in (the device allows it on purpose).
  bool get canSave =>
      _phase == MeasurePhase.result ||
      (_phase == MeasurePhase.error && _info?.model == DeviceModel.n2 && _plugged && _session.pendingSeconds != null);

  bool get canDelete =>
      _phase == MeasurePhase.result || (_phase == MeasurePhase.error && _info?.model == DeviceModel.n2);

  /// Single results can only be removed where the app owns the list.
  bool get canRemoveSingle => !deviceOwnsSession;

  // ---- Settings ----

  void setUnit(MeasurementUnit unit) {
    if (unit == _unit) return;
    _unit = unit;
    _prefs.setString(_unitKey, unit.name);
    notifyListeners();
  }

  /// Sends any subset of the device settings; the device answers with its applied `config`.
  Future<void> setDeviceConfig(Map<String, Object> values) async {
    await _connection?.send(PorozitCommands.config(values));
  }

  void clearDeviceError() {
    if (_deviceError == null) return;
    _deviceError = null;
    notifyListeners();
  }

  // ---- Connection ----

  Future<void> connectDemo() => connectWith(SimulatedConnection());

  Future<void> connectTo(DiscoveredDevice device) => connectWith(BleConnection(device));

  @visibleForTesting
  Future<void> connectWith(PorozitConnection connection) async {
    await disconnect();
    _connection = connection;
    connection.status.addListener(_onStatus);
    _messagesSub = connection.messages.listen(_onMessage);
    notifyListeners();
    await connection.connect();
  }

  Future<void> disconnect() async {
    final connection = _connection;
    if (connection == null) return;
    _connection = null;
    await _messagesSub?.cancel();
    _messagesSub = null;
    connection.status.removeListener(_onStatus);
    await connection.disconnect();
    connection.dispose();
    _battery = null;
    _info = null;
    _deviceConfig = null;
    _incomingSaved = null;
    if (_phase == MeasurePhase.measuring || _phase == MeasurePhase.error) _phase = MeasurePhase.idle;
    notifyListeners();
  }

  void _onStatus() => notifyListeners();

  /// Demo only: what releasing the trigger on the device does.
  void simulateTrigger() {
    final connection = _connection;
    if (connection is SimulatedConnection) connection.releaseTrigger();
  }

  bool get canSimulateTrigger => isDemo && isConnected && _phase == MeasurePhase.idle;

  // ---- Messages from the device ----

  void _onMessage(DeviceMessage message) {
    switch (message) {
      case InfoMessage():
        _info = message;
        if (message.maxSaved > 0) {
          // The device keeps the results: fetch them and mirror its list.
          _incomingSaved = [];
          _connection?.send(PorozitCommands.list);
        }
      case SavedMessage(:final time):
        _incomingSaved?.add(time);
      case StateMessage():
        _applyState(message);
      case MeasurementEvent():
        _applyEvent(message);
      case ConfigMessage(:final config):
        _deviceConfig = config;
      case BatteryMessage(:final percent, :final charging):
        _battery = percent;
        _charging = charging;
      case ResultMessage(:final ok, :final message):
        if (!ok) _deviceError = message;
    }
    notifyListeners();
  }

  void _applyState(StateMessage message) {
    _plugged = message.plugged;
    final incoming = _incomingSaved;
    if (incoming != null) {
      // The state after a `list` answer closes it.
      _incomingSaved = null;
      final now = _clock();
      var session = const MeasurementSession();
      for (final seconds in incoming) {
        session = session.withPending(seconds).savePending(now);
      }
      _updateSession(session);
    }
    switch (message.state) {
      case DeviceState.idle:
        _phase = MeasurePhase.idle;
        _session = _session.deletePending();
      case DeviceState.measuring:
        _phase = MeasurePhase.measuring;
        _liveSeconds = message.time;
      case DeviceState.result:
        _phase = MeasurePhase.result;
        _session = _session.withPending(message.time);
      case DeviceState.error:
        _phase = MeasurePhase.error;
        _session = _partial(message.time);
    }
  }

  void _applyEvent(MeasurementEvent event) {
    switch (event.type) {
      case MeasurementEventType.started:
        _phase = MeasurePhase.measuring;
        _liveSeconds = 0;
      case MeasurementEventType.progress:
        _phase = MeasurePhase.measuring;
        _liveSeconds = event.time ?? _liveSeconds;
      case MeasurementEventType.done:
        _phase = MeasurePhase.result;
        _liveSeconds = null;
        if (event.time != null) _session = _session.withPending(event.time!);
      case MeasurementEventType.save:
        final seconds = event.time ?? _session.pendingSeconds;
        if (seconds != null) _updateSession(_session.withPending(seconds).savePending(_clock()));
        _phase = MeasurePhase.idle;
      case MeasurementEventType.delete:
        _session = _session.deletePending();
        _phase = MeasurePhase.idle;
      case MeasurementEventType.reset:
        _updateSession(_session.reset());
        if (_phase == MeasurePhase.result) _phase = MeasurePhase.idle;
      case MeasurementEventType.error:
        _session = _partial(event.time);
        _liveSeconds = null;
        _phase = MeasurePhase.error;
    }
  }

  /// Keeps the partial time of an interrupted measurement (N2+ reports it) so it can still be saved.
  MeasurementSession _partial(double? seconds) =>
      seconds != null && seconds > 0 ? _session.withPending(seconds) : _session.deletePending();

  // ---- Actions ----

  /// Keep the result: it joins the average. On a device, this is the device's Save button.
  void save() {
    if (!canSave) return;
    if (isConnected) {
      _connection!.send(PorozitCommands.save);
      return;
    }
    _updateSession(_session.savePending(_clock()));
    _phase = MeasurePhase.idle;
    notifyListeners();
  }

  /// Discard the result (e.g. the cloth was not clamped properly).
  void delete() {
    if (!canDelete) return;
    if (isConnected) {
      _connection!.send(PorozitCommands.delete);
      return;
    }
    _session = _session.deletePending();
    _phase = MeasurePhase.idle;
    notifyListeners();
  }

  void removeMeasurement(int index) {
    if (!canRemoveSingle) return;
    _updateSession(_session.remove(index));
    notifyListeners();
  }

  /// Clears the saved results, on the device too when one is connected.
  void resetSession() {
    if (isConnected) {
      _connection!.send(PorozitCommands.reset);
      return;
    }
    _updateSession(_session.reset());
    if (_phase == MeasurePhase.result) _phase = MeasurePhase.idle;
    notifyListeners();
  }

  void _updateSession(MeasurementSession session) {
    _session = session;
    _prefs.setString(_sessionKey, session.encode());
  }

  @override
  void dispose() {
    _messagesSub?.cancel();
    _connection?.status.removeListener(_onStatus);
    _connection?.dispose();
    super.dispose();
  }
}

````

### N2+ mini protocol v2 — `Porozit_N2_plus_mini: main/ble_protocol.c`

````c
/**
 * @file ble_protocol.c
 * @brief Porozit BLE protocol v2 (see PROTOCOL.md)
 *
 * Messages are parsed and applied on a dedicated task, never in the BLE
 * callback: applying a setting writes NVS and relabels widgets, and blocking
 * the NimBLE host task for that long upsets the connection.
 */

#include "ble_protocol.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "battery_monitor.h"
#include "ble_service.h"
#include "measure.h"
#include "watch_settings.h"

static const char *TAG = "ble_protocol";

#define BLE_PROTOCOL_TASK_STACK 4096
#define BLE_PROTOCOL_TASK_PRIO  4

typedef struct {
    uint16_t len;
    char json[BLE_PROTOCOL_MAX_MSG_LEN];
} ble_protocol_msg_t;

static QueueHandle_t s_queue;
static TaskHandle_t s_task;

static esp_err_t send_json(const char *json, int len)
{
    if (len <= 0 || (size_t)len >= 240) {
        ESP_LOGE(TAG, "Message too long (%d bytes), not sent", len);
        return ESP_ERR_INVALID_SIZE;
    }
    return ble_service_notify((const uint8_t *)json, (size_t)len);
}

/**
 * @brief Answer a command or config write
 * @param request Always a string literal from this file, never a value taken
 *                from the received JSON, so it cannot inject quotes.
 * @param error   NULL on success, else a static reason without quotes
 */
static void send_result(const char *request, const char *error)
{
    if (error != NULL) {
        ESP_LOGW(TAG, "%s rejected: %s", request, error);
    }
    char json[200];
    const int len = snprintf(json, sizeof(json),
                             "{\"version\":%d,\"type\":\"result\",\"request\":\"%s\","
                             "\"status\":\"%s\",\"message\":\"%s\"}",
                             BLE_PROTOCOL_VERSION, request, error == NULL ? "OK" : "ERROR",
                             error == NULL ? "OK" : error);
    send_json(json, len);
}

static esp_err_t send_info(void)
{
    char json[160];
    const int len = snprintf(json, sizeof(json),
                             "{\"version\":%d,\"type\":\"info\",\"model\":\"mini\",\"fw\":\"%s\","
                             "\"serial\":\"\",\"maxSaved\":0}",
                             BLE_PROTOCOL_VERSION, esp_app_get_description()->version);
    return send_json(json, len);
}

static esp_err_t send_config(void)
{
    char json[200];
    return send_json(json, watch_settings_config_json(json, sizeof(json)));
}

esp_err_t ble_protocol_send_state(void)
{
    char json[160];
    return send_json(json, measure_state_json(json, sizeof(json)));
}

esp_err_t ble_protocol_send_battery(void)
{
    battery_status_t battery;
    battery_monitor_get_status(&battery);
    if (!battery.valid) {
        return ESP_ERR_INVALID_STATE;
    }
    char json[96];
    const int len = snprintf(json, sizeof(json),
                             "{\"version\":%d,\"type\":\"battery\",\"battery\":%d,\"charging\":%s}",
                             BLE_PROTOCOL_VERSION, battery.percentage,
                             battery.charging ? "true" : "false");
    return send_json(json, len);
}

static void handle_command(const char *action)
{
    if (action == NULL) {
        send_result("unknown", "missing action");
    } else if (strcasecmp(action, "save") == 0) {
        send_result("save", measure_request_save() ? NULL : "nothing to save");
    } else if (strcasecmp(action, "delete") == 0) {
        send_result("delete", measure_request_delete() ? NULL : "nothing to delete");
    } else if (strcasecmp(action, "reset") == 0) {
        send_result("reset", measure_request_reset() ? NULL : "measurement running");
    } else if (strcasecmp(action, "list") == 0) {
        /* The N2+ mini stores no results (maxSaved 0): nothing to list. */
        send_result("list", NULL);
        ble_protocol_send_state();
    } else if (strcasecmp(action, "status") == 0) {
        send_info();
        send_config();
        ble_protocol_send_state();
        ble_protocol_send_battery();
    } else {
        send_result("unknown", "unknown action");
    }
}

static void dispatch(const char *json, size_t len)
{
    ESP_LOGI(TAG, "RX %u bytes: %s", (unsigned)len, json);

    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        send_result("unknown", "malformed json");
        return;
    }

    const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *type = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsNumber(version) || (int)version->valuedouble != BLE_PROTOCOL_VERSION) {
        send_result("unknown", "unsupported version, expected 2");
    } else if (!cJSON_IsString(type)) {
        send_result("unknown", "missing type");
    } else if (strcmp(type->valuestring, "config") == 0) {
        const char *error = NULL;
        if (watch_settings_apply(root, &error)) {
            send_result("config", NULL);
            send_config();
        } else {
            send_result("config", error);
        }
    } else if (strcmp(type->valuestring, "command") == 0) {
        const cJSON *action = cJSON_GetObjectItemCaseSensitive(root, "action");
        handle_command(cJSON_IsString(action) ? action->valuestring : NULL);
    } else {
        send_result("unknown", "unknown type");
    }

    cJSON_Delete(root);
}

static void ble_protocol_task(void *param)
{
    (void)param;
    ble_protocol_msg_t msg;
    while (1) {
        if (xQueueReceive(s_queue, &msg, portMAX_DELAY) == pdTRUE) {
            dispatch(msg.json, msg.len);
        }
    }
}

esp_err_t ble_protocol_init(void)
{
    if (s_task != NULL) {
        return ESP_OK;
    }
    s_queue = xQueueCreate(BLE_PROTOCOL_QUEUE_LEN, sizeof(ble_protocol_msg_t));
    if (s_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(ble_protocol_task, "ble_proto", BLE_PROTOCOL_TASK_STACK, NULL,
                    BLE_PROTOCOL_TASK_PRIO, &s_task) != pdPASS) {
        vQueueDelete(s_queue);
        s_queue = NULL;
        return ESP_FAIL;
    }
    return ESP_OK;
}

void ble_protocol_receive(const uint8_t *data, size_t len)
{
    if (s_queue == NULL || data == NULL || len == 0) {
        return;
    }
    if (len >= BLE_PROTOCOL_MAX_MSG_LEN) {
        ESP_LOGW(TAG, "Dropping a %u byte message, the limit is %d",
                 (unsigned)len, BLE_PROTOCOL_MAX_MSG_LEN - 1);
        return;
    }
    ble_protocol_msg_t msg;
    memcpy(msg.json, data, len);
    msg.json[len] = '\0';
    msg.len = (uint16_t)len;
    if (xQueueSend(s_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Queue full, message dropped");
    }
}

````
