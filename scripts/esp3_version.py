#!/usr/bin/env python3
"""CO_RD_VERSION ueber die transparente Bruecke -- Kurztest fuer EUL-Sticks.

Beweist USB -> ESP -> TCM515 -> ESP -> USB in beiden Richtungen, ohne an der
Firmware vorbeizugehen: ESP3 COMMON_COMMAND/CO_RD_VERSION hin, RESPONSE mit
App-/API-Version, Chip-ID und App-Beschreibung zurueck.  Der Baudraten-Teil
liegt in der Bruecke selbst (460800, Klone 57600) -- hostseitig ist der
CDC-Port baud-agnostisch.

Laeuft mit Python 3.8 (Pruefplatz .197) und pyserial.  Exit 0 = RESPONSE mit
RET_OK und gueltigen CRCs, alles andere != 0.
"""

import argparse
import sys
import time

import serial


def crc8(data):
    """ESP3-CRC8, Polynom 0x07, Init 0 (EnOcean ESP3 Spec, Kap. 2.3)."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc


def build_common_command(cmd):
    data = bytes([cmd])
    hdr = bytes([0x00, len(data), 0x00, 0x05])  # len hi/lo, optlen, type=COMMON
    return b"\x55" + hdr + bytes([crc8(hdr)]) + data + bytes([crc8(data)])


def read_packet(port, deadline):
    """Scannt auf 0x55-Sync und liefert (type, data, optdata) CRC-geprueft."""
    while time.time() < deadline:
        b = port.read(1)
        if b != b"\x55":
            continue
        hdr = port.read(4)
        if len(hdr) < 4:
            continue
        hcrc = port.read(1)
        if len(hcrc) < 1 or hcrc[0] != crc8(hdr):
            continue  # Fehlsync mitten im Datenstrom -- weiterscannen
        dlen = (hdr[0] << 8) | hdr[1]
        olen = hdr[2]
        payload = port.read(dlen + olen)
        dcrc = port.read(1)
        if len(payload) < dlen + olen or len(dcrc) < 1:
            continue
        if dcrc[0] != crc8(payload):
            continue
        return hdr[3], payload[:dlen], payload[dlen:]
    return None, None, None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyACM0")
    ap.add_argument("--listen", type=float, default=4.0,
                    help="erst so lange Boot-Banner/Probe-Ausgabe mitlesen")
    ap.add_argument("--timeout", type=float, default=3.0,
                    help="Wartezeit auf die ESP3-RESPONSE")
    args = ap.parse_args()

    # Nach einem Flash re-enumeriert der native USB -- aufs Device warten.
    port = None
    for _ in range(20):
        try:
            port = serial.Serial(args.port, 115200, timeout=0.2)
            break
        except (OSError, serial.SerialException):
            time.sleep(0.5)
    if port is None:
        print("FAIL: %s nicht zu oeffnen" % args.port)
        return 2

    # Erst nur zuhoeren: jedes Host-Byte beendet den Selbsttest der Bruecke,
    # also Banner/Probe-Ausgabe einsammeln, BEVOR wir senden.
    t0 = time.time()
    pre = b""
    while time.time() - t0 < args.listen:
        pre += port.read(256)
    if pre:
        for line in pre.decode("utf-8", "replace").splitlines():
            if line.strip():
                print("  banner: %s" % line.strip())

    port.write(build_common_command(0x03))  # CO_RD_VERSION
    port.flush()

    deadline = time.time() + args.timeout
    while True:
        ptype, data, _opt = read_packet(port, deadline)
        if ptype is None:
            print("FAIL: keine gueltige ESP3-RESPONSE in %.1f s" % args.timeout)
            return 1
        if ptype != 0x02:  # RESPONSE
            continue
        break

    if len(data) < 33 or data[0] != 0x00:
        print("FAIL: RESPONSE ret=%s len=%d" % (data[0] if data else None, len(data)))
        return 1

    def v4(off):
        return ".".join(str(x) for x in data[off:off + 4])

    desc = data[17:33].split(b"\x00")[0].decode("ascii", "replace")
    print("PASS: app %s  api %s  chipid %s  chipver %s  desc \"%s\""
          % (v4(1), v4(5), data[9:13].hex(), data[13:17].hex(), desc))
    return 0


if __name__ == "__main__":
    sys.exit(main())
