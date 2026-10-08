# Porozit BLE protocol — version 2

Shared by every Porozit control unit (**Porozit N2+**, **Porozit N2+ mini**) and the **Porozit app**.
The same file lives in all three repositories; change it in all of them together.

## Transport

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

## Session start

1. App connects, requests MTU, subscribes to `0xFFF1` notifications.
2. App writes `{"version":2,"type":"command","action":"status"}`.
3. Device answers with `info`, `config`, `state` and `battery` (four notifications, in this order).

## Device → app

### `info` — what is connected
```json
{"version":2,"type":"info","model":"n2","fw":"2.0.0","serial":"PZ2401-0042","maxSaved":24}
```
`model`: `"n2"` (N2+) or `"mini"` (N2+ mini). `maxSaved`: how many results the device itself stores (`0` on the N2+ mini,
which keeps nothing and relies on the app). `serial` may be empty.

### `state` — where the measurement cycle is
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

### `measurement` — events, as they happen
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
- `error` `reason`: `"unplugged"` (head pulled out while measuring).

### `saved` — stored results (answer to `list`)
```json
{"version":2,"type":"saved","index":1,"time":482.1}
```
One per stored result, `index` 1-based, followed by a `state`. Only devices with `maxSaved > 0`.

### `config` — current settings
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

### `battery`
```json
{"version":2,"type":"battery","battery":71,"charging":false}
```
On `status` and every 10 seconds.

### `result` — answer to a command or config write
```json
{"version":2,"type":"result","request":"save","status":"OK","message":"OK"}
{"version":2,"type":"result","request":"config","status":"ERROR","message":"beep: expected one of 0,60,180,300,600"}
```
`status` is exactly `"OK"` or `"ERROR"`. `request` is `save`, `delete`, `reset`, `list`, `config` or
`unknown`.

## App → device

```json
{"version":2,"type":"command","action":"save"}
{"version":2,"type":"command","action":"delete"}
{"version":2,"type":"command","action":"reset"}
{"version":2,"type":"command","action":"list"}
{"version":2,"type":"command","action":"status"}
{"version":2,"type":"config","unit":"pma","brightness":60}
```
- `save` / `delete` act exactly like the buttons on the device: only in `result` (N2+: delete also in
  `error`). On the N2+ `save` fails with "storage full" when `maxSaved` results are stored. Answered by a `result`, and on success also by the matching `measurement` event.
- `reset` clears all saved results (`measurement` `reset` event follows).
- `config` may carry any subset of the keys above; the device validates all of them first and
  applies all or nothing, then answers with `result` and a fresh `config`.
- `status` is answered by `info`, `config`, `state`, `battery` (no `result`).

## Version history

- **2** — unified for N2+ and N2+ mini: `event` instead of `alert`, numeric times, `progress` and `error`
  events, `info` / `state` / `battery` / `saved` messages, config in physical units (seconds, %),
  multi-key config. Version 1 (N2+ mini only) is no longer accepted.
- **1** — first N2+ mini protocol (`alert`, heartbeat without version, index-based settings).
