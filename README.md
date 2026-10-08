# Sárkány Watch - Sárkányrepülő Anyag Légáteresztés Mérő

Az ESP és az Android-app közötti BLE JSON-üzenetek leírása: [BLE_JSON_PROTOCOL.md](BLE_JSON_PROTOCOL.md).

Sárkányrepülő vitorla anyagának levegőáteresztő képességét mérő hordozható eszköz ESP32-S3 alapokon.

## Leírás

A **Sárkány Watch** egy kompakt mérőeszköz, amely sárkányrepülők vitorlaanyagának légáteresztő képességét méri. Az eszköz Bluetooth kapcsolaton keresztül továbbítja a mérési adatokat okostelefonra, ahol azok feldolgozásra és elemzésre kerülnek.

### Főbb funkciók

- **Légáteresztés mérés**: Pontos mérés a vitorlaanyag állapotának felmérésére
- **Bluetooth adatátvitel**: Valós idejű adatküldés okostelefonra
- **Érintőképernyős kijelző**: Intuitív felhasználói felület LVGL keretrendszerrel
- **Akkumulátor figyelés**: Beépített akkumulátor töltöttség monitoring
- **Energiatakarékos üzemmód**: Hosszú üzemidő hordozható használathoz

## Hardver

| Komponens | Típus |
|-----------|-------|
| Mikrokontroller | ESP32-S3 |
| Kijelző | TFT LCD érintőképernyővel |
| Érintésvezérlő | CST816S |

## Projekt struktúra

```
├── CMakeLists.txt             Projekt konfiguráció
├── main/
│   ├── main.c                 Fő alkalmazás belépési pont
│   ├── display_driver.c/h     Kijelző meghajtó
│   ├── touch_driver.c/h       Érintőképernyő meghajtó
│   ├── battery_monitor.c/h    Akkumulátor figyelés
│   ├── power_manager.c/h      Energiagazdálkodás
│   ├── lv_conf.h              LVGL konfiguráció
│   └── ui/                    Felhasználói felület (SquareLine Studio)
├── managed_components/        ESP-IDF komponensek
└── README.md                  Ez a fájl
```

## Fordítás és telepítés

### Előfeltételek

- ESP-IDF v5.x telepítve
- ESP32-S3 fejlesztői kártya

### Lépések

1. Nyisd meg a projektet VS Code-ban ESP-IDF extension-nel
2. Válaszd ki a megfelelő COM portot
3. Fordítás: `ESP-IDF: Build`
4. Feltöltés: `ESP-IDF: Flash`
5. Monitor: `ESP-IDF: Monitor`

Vagy parancssorból:
```bash
idf.py build
idf.py -p PORT flash monitor
```

## Hibaelhárítás

* **Feltöltési hiba**
    * Ellenőrizd a hardver csatlakozást: `idf.py -p PORT monitor` futtatásával
    * Csökkentsd az átviteli sebességet a `menuconfig` menüben

## Licensz

[MIT License](LICENSE)
* For a feature request or bug report, create a [GitHub issue](https://github.com/espressif/esp-idf/issues)

We will get back to you as soon as possible.
