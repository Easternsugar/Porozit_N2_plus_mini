# BLE JSON-üzenetek

Ez a dokumentum a firmware **jelenlegi kódjában ténylegesen megvalósított** BLE-adatcserét írja le. A példákban szereplő JSON az átvitt adat; a Bluetooth-napló `value received` szövege nem része az üzenetnek.

## Átviteli csatorna

| Elem | Érték |
| --- | --- |
| Eszköz neve | `SarkanyWatch` |
| GATT-szolgáltatás | `0xFFF0` |
| Karakterisztika | `0xFFF1` — olvasás, írás és értesítés (Notify) |
| Kódolás | Az alábbi kimenő JSON-ok UTF-8 bájtok |
| Maximális karakterisztikaérték | 128 bájt |

Az ESP csak aktív BLE-kapcsolat és bekapcsolt Notify-feliratkozás mellett tud üzenetet küldeni. Az `0xFFF1` olvasása az utoljára beírt vagy értesítésként kiküldött nyers értéket adja vissza; ez nem külön JSON-válasz. Forrás: [`main/ble_service.c`](main/ble_service.c).

## ESP → app: jelenleg küldött JSON-ok

### Mérési események

A firmware négy különböző `measurement` értesítést küld. Az `alert` mező alapján kell megkülönböztetni őket:

| Esemény | Mikor keletkezik? | Tartalmaz mérési időt? |
| --- | --- | --- |
| `started` | A mérés indulásakor | Nem |
| `done` | A mérés lezárásakor | Igen, de **szövegként** |
| `save` | Az ESP Save/Mentés gombjának megnyomásakor | Igen, **számként** |
| `delete` | Az ESP Delete/Törlés gombjának megnyomásakor | Nem |

```json
{"version":1,"type":"measurement","alert":"started","timestamp":428000}
{"version":1,"type":"measurement","alert":"done","time":"1.813580","timestamp":428500}
{"version":1,"type":"measurement","alert":"save","time":1.813580,"timestamp":428576}
{"version":1,"type":"measurement","alert":"delete","timestamp":429000}
```

Az alábbi táblázat a **mentés** üzenetét részletezi:

```json
{"version":1,"type":"measurement","alert":"save","time":1.813580,"timestamp":428576}
```

| Mező | Típus | Jelentés |
| --- | --- | --- |
| `version` | egész szám | A küldött séma verziója, jelenleg `1`. |
| `type` | szöveg | Mindig `measurement`. |
| `alert` | szöveg | `started`, `done`, `save` vagy `delete`. |
| `time` | szám vagy szöveg | A mérés időtartama **másodpercben**, hat tizedesjeggyel. Csak `done` és `save` esetén van jelen; `done` esetén JSON-szöveg, `save` esetén JSON-szám. |
| `timestamp` | egész szám | Az ESP indulása óta eltelt **ezredmásodperc** az adott esemény feldolgozásakor (`esp_timer_get_time() / 1000`). Nem Unix-idő. |

Az előzménybe mentést a `save` esemény jelzi, nem a `done`. A `done` értesítés sikertelen mérés lezárásakor is kimehet; ilyenkor a `time` `"0.000000"` lehet. Sikertelen BLE-értesítés esetén a firmware naplóz, de nincs újraküldés vagy app-visszaigazolás; mentéskor a helyi mérési állapotot ettől függetlenül törli. Forrás: [`main/measure.c`](main/measure.c), `measure_start()`, `measure_finalize()`, `measure_handle_save()` és `measure_handle_delete()`.

### Szívdobbanás és akkumulátor

```json
{"command":"heartbeat","battery":71,"charging":false}
```

| Mező | Típus | Jelentés |
| --- | --- | --- |
| `command` | szöveg | Mindig `heartbeat`. |
| `battery` | egész szám | Akkumulátortöltöttség százalékban (`0`–`100`). |
| `charging` | logikai | `true`, ha töltés alatt áll, egyébként `false`. |

A feladat 10 másodpercenként próbál küldeni, de csak akkor állít össze üzenetet, ha az akkumulátor-állapot érvényes. Ebben a JSON-ban nincs `version` mező. Forrás: [`main/main.c`](main/main.c), `ble_heartbeat_task()`.

## App → ESP: jelenlegi fogadás

Az app az `0xFFF1` karakterisztikára írhat legfeljebb 128 bájtot. A firmware a `{"version":1,"type":"config",...}` alakú, pontosan **egy beállítást** módosító JSON-t külön feladatban ellenőrzi. Sikeres tartós mentés és alkalmazás után `OK`, elutasításkor `ERROR` szöveget küld Notify-on. A GATT-írás sikere önmagában még nem jelenti azt, hogy a beállítás érvénybe lépett. Forrás: [`main/ble_service.c`](main/ble_service.c), [`main/watch_settings.c`](main/watch_settings.c).

A HELLO Android-app és a firmware az alábbi, egy mezőt módosító JSON-formátumot használja:

```json
{"version":1,"type":"config","measurementUnit":"sec"}
{"version":1,"type":"config","beepTimer":300}
{"version":1,"type":"config","volume":2}
{"version":1,"type":"config","brightness":4}
{"version":1,"type":"config","sleepTimer":1800}
{"version":1,"type":"config","langSelector":"hu"}
```

| Beállítás | Az app által küldhető értékek |
| --- | --- |
| `measurementUnit` | `"sec"`, `"l/m²/min"` |
| `beepTimer` | `0`, `60`, `180`, `300`, `600` másodperc |
| `volume` | `0`–`5` szint |
| `brightness` | `0`–`5` szint |
| `sleepTimer` | `300`, `600`, `1800` másodperc |
| `langSelector` | `"en"`, `"hu"`, `"de"`, `"es"`, `"fr"` |

A `beepTimer` a periodikus hangjelzés időköze, `0` esetén kikapcsolva. A `volume` a hangjelző PWM-szintjét, a `brightness` a kijelző háttérvilágítását állítja. A `sleepTimer` az inaktivitás utáni alvás ideje. A `measurementUnit` a fő kijelzett mérési értéket váltja másodperc és légáteresztés között. A `langSelector` a feliratok nyelve. Az értékek NVS-be kerülnek, ezért újraindítás után is megmaradnak. Az app csak `OK` után módosítja a kijelzett értéket; `ERROR` vagy időtúllépés esetén hibát jelez.

**Más app-parancsok:** a HELLO régebbi parancsútvonala (`{"type":"get_config"}`, valamint `{"command":"restart"}` és hasonlók) `0xFFE3` karakterisztikára céloz. Az ESP ebben a projektben csak `0xFFF1`-et hirdet, ezért ezeket a jelenlegi firmware nem fogadja. A firmware `config` állapotképet sem küld.

## Válaszüzenetek

```text
OK
ERROR
```

Ezek sima UTF-8 szövegek, nem JSON-objektumok. Az `OK` csak a sikeres NVS-mentés után megy ki. `ERROR` esetén a firmware nem alkalmazza a módosítást.
