# USBSerial32

Byte-transparente Bruecke **USB-Serial-JTAG ↔ Transceiver-UART** fuer die
busware-Sticks **TUL** (KNX-TP, NCN5130) und **EUL** (EnOcean, TCM515), auf
**ESP32-C3** und **ESP32-C6**. ESP-IDF, kein Arduino.

Ersetzt die alte Firmware `busware-*-serial-transparent`, deren Quelle nicht
mehr auffindbar ist (weder in `busware-esp32/Simple/USBSerial` noch sonst im
GIT-Baum; der IDF-Prototyp unter `projects/serial_bridge` war ein 70-Zeilen-
Wegwerfstand fuer nur C6/TUL mit `FLASHSIZE_2MB`).

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

- **TUL/C6 auf Hardware verifiziert** (2026-08-01, TUL32 mit angeklemmtem
  KNX-Bus): sauberer Boot, Banner `NCN5130 OK @38400 baud`, und durch die
  Bruecke `U_Reset.req`→`0x03`, `U_State.req`→`0x07`,
  `U_SystemStat.req`→`0x4B 0xFB` (alle Rails gesetzt, Mode Normal).
- **EUL/C6, TUL/C3, EUL/C3 gebaut, aber noch nicht auf Hardware geprueft.**
  Der EUL-Zweig braucht ein EUL32 mit TCM515; der C3-Zweig einen Legacy-Stick.
  Bis dahin gilt fuer diese drei: Layout geprueft, Funktion nicht.

Ein Boot-Banner sieht so aus:

```
busware TUL serial bridge 1.0.1 (esp32c6) -- NCN5130 OK @38400 baud, 299 ms, 1 Versuch
```

Bekannte Kleinigkeit: Der Probe wartet pro Versuch die vollen 300 ms auf
Antwortbytes, auch wenn die Antwort nach 50 ms da ist — der Boot dauert
dadurch rund 300 ms laenger als noetig. Funktional ohne Belang, aber ein
Poll-Loop waere sauberer.
