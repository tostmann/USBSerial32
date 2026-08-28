# USBSerial32

Byte-transparente Bruecke **USB-Serial-JTAG ↔ Transceiver-UART** fuer die
busware-Sticks **TUL** (KNX-TP, NCN5130) und **EUL** (EnOcean, TCM515), auf
**ESP32-C3** und **ESP32-C6**. ESP-IDF, kein Arduino.

Loest die frueheren Arduino-basierten `busware-*-serial-transparent`-Builds ab
(siehe [busware-esp32](https://github.com/tostmann/busware-esp32)); seit
August 2026 wird die ausgelieferte Bruecke hier gebaut.

## Was anders ist als vorher

| | alt | hier |
|---|---|---|
| Transceiver-Probe | ein Versuch, ~90 ms nach Boot | Wiederholung ueber 3 s, meldet Baudrate/Dauer/Versuche |
| Nutzdatenstrom | Bootloader- und Coredump-Meldungen darin | Konsole und Bootloader-Log aus, ein bewusster Banner |
| Partitionslayout (C6) | `factory` leer, App in `ota_0`, 1 MB verschenkt | App in `factory` @0x10000, coredump-Partition |
| Flash-Header | App-Header behauptete 8 MB | 4 MB, passend zum C6FH4-Modul |
| Identitaet im Image | `app='arduino-lib-builder'` | `app='usbserial32'`, echte Version |
| Groesse (C6) | 1 407 664 B | 241 456 B |

Der Probe-Punkt ist der wichtigste: Der NCN5130 kennt laut Datenblatt
(Tabelle 10, S. 29) Zustaende, in denen er Anfragen **verwirft** — RESET
("services are ignored and not executed") und POWER-UP, solange VBUS/VFILT/XTAL
nicht stabil sind. Ein einmaliger Versuch direkt nach dem Boot kann deshalb
fehlschlagen, obwohl die Hardware in Ordnung ist. Genau dieses Bild — Banner
meldet FAIL, ein Dialog Sekunden spaeter laeuft einwandfrei — trat mit der
Altfirmware am Pruefplatz reproduzierbar auf.

## Bauen

Braucht ESP-IDF (getestet mit v6.1-dev). Gebaut wird auf lokalem Dateisystem,
nur die fertigen Artefakte landen im Baum.

```bash
. $IDF_PATH/export.sh
./build.sh                 # alle vier Ziele -> dist/
./build.sh tul_c6          # nur eines
BW_QUIET=1 ./build.sh      # ohne Boot-Banner
```

Ergebnis: `dist/busware-<produkt>-<chip>-serial-transparent.factory.bin`,
flashbar bei Offset 0.

## Pins

Jede Zahl ist belegt, keine geraten — Herkunft steht in `main/board.h`.

| | TUL C6 | EUL C6 | TUL C3 | EUL C3 |
|---|---|---|---|---|
| ESP → Transceiver | IO4 | IO4 | IO21 | IO21 |
| Transceiver → ESP | IO5 | IO5 | IO20 | IO20 |
| LED | IO8 (low-aktiv) | IO8 (low-aktiv) | IO4 | IO4 |
| weitere | — | SET=IO2, RST=IO3 | — | SET=IO5, RST=IO3 |
| Baud | 38400 8E1 | 460800/57600 8N1 | 38400 8E1 | 460800/57600 8N1 |

## Stand

Alle vier Ziele im August 2026 auf echter Hardware verifiziert: TUL/C6 + TUL/C3
am KNX-Bus (vollstaendiger NCN-Dialog, knxd 0.14.73 fremdbytefrei in beide
Richtungen), EUL/C6 + EUL/C3 gegen den TCM515. Wiederanlauf ohne Reset, wenn
die Busspannung erst spaeter kommt.

## Stolpersteine

- **knxd kann die by-id-Pfade dieser Sticks nicht verarbeiten**: es trennt das
  Backend-Argument an Doppelpunkten, und der by-id-Name nativer C3/C6 enthaelt
  immer die MAC (`...aa:bb:cc:dd:ee:ff`) -- `-b tpuart:/dev/serial/by-id/...`
  scheitert mit "Too many arguments for tpuart!". Also `/dev/ttyACMn` nehmen
  (wandert bei Re-Enumeration) oder per udev-Regel einen Symlink ohne
  Doppelpunkte anlegen.
- **Der SET-Pin des TCM515 muss LOW sein.** Mit SET HIGH laeuft der Baustein im
  Programmiermodus und antwortet nur bei 57600 statt 460800; der
  Baudraten-Fallback des Probes verdeckt das, die Bruecke arbeitet dann im
  falschen Modus. Der Banner nennt deshalb die tatsaechlich verwendete Baudrate.

Ein Boot-Banner sieht so aus:

```
busware TUL serial bridge 1.0.1 (esp32c6) -- NCN5130 OK @38400 baud, 299 ms, 1 Versuch
```

Bekannte Kleinigkeit: Der Probe wartet pro Versuch die vollen 300 ms auf
Antwortbytes, auch wenn die Antwort nach 50 ms da ist — der Boot dauert
dadurch rund 300 ms laenger als noetig. Funktional ohne Belang, aber ein
Poll-Loop waere sauberer.

## Lizenz

Apache-2.0 — siehe [LICENSE](LICENSE).

Copyright (c) 2026 Dirk Tostmann
