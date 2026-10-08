# Porozit Watch — firmware

Firmware of the **Porozit Watch**, the wrist-size control unit of the Porozit porosity tester
(paraglider fabric air permeability) by Hello Ltd. — [porosimeter.hu](https://porosimeter.hu).

The watch measures the fall time, shows it in seconds or l/m²/min (`7500 / sec`) with the
Good / Acceptable / Fail colours, and sends every result to the **Porozit app** over Bluetooth LE.
It keeps no history itself: **Save** sends the result to the phone (so it needs a connected app),
**Delete** discards it.

## Hardware

| Part | Type |
|---|---|
| Board | Waveshare ESP32-S3-Touch-LCD-1.69 (schematic: `ESP32-S3-Touch-LCD-1.69_V2.1.pdf`) |
| MCU | ESP32-S3, Bluetooth 5 LE |
| Display / touch | 1.69" ST7789 240×280, CST816S |
| Measuring head | GPIO2 = trigger (active low), GPIO3 = head plugged in (active low) |

## Bluetooth

The protocol shared with the Porozit N2+ and the app is in [PROTOCOL.md](PROTOCOL.md) (version 2).
The watch advertises as `Porozit Watch XXXX` with service `0xFFF0`.

| File | What |
|---|---|
| `main/ble_service.c` | NimBLE GATT service, advertising, notify |
| `main/ble_protocol.c` | protocol v2: commands, config, info/state/battery messages |
| `main/measure.c` | measurement state machine and the `measurement` events |
| `main/watch_settings.c` | settings shared with the app (validate, store in NVS, apply) |

## Build

ESP-IDF **v6.0.1**, target `esp32s3`:

```bash
idf.py set-target esp32s3   # first time only
idf.py build
idf.py -p PORT flash monitor
```

GitHub Actions builds every push and keeps the binaries as the *porozit-watch-firmware* artifact.

The UI is designed in SquareLine Studio (`squareline/`) and exported to `main/ui/`. Custom code
lives outside `main/ui/` so a re-export does not overwrite it.

## License

[MIT](LICENSE)
