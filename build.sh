#!/usr/bin/env bash
# Baut die vier Ziele und legt fertige Factory-Images in dist/ ab.
#
#   ./build.sh                 alle vier
#   ./build.sh tul_c6          nur eines
#   BW_QUIET=1 ./build.sh      ohne Boot-Banner
#
# Gebaut wird auf LOKALEM Dateisystem, nicht auf dem NFS-Baum: der lazy
# Write-Back dort zeigt Lesern gelegentlich halb geschriebene Dateien, und ein
# Build-Verzeichnis ist genau so ein Leser (ninja, esptool, der zweite
# Build-Schritt).  Nur die fertigen Artefakte wandern zurueck, per rename.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_ROOT="${BW_BUILD_ROOT:-/tmp/usbserial32-build}"
DIST="$REPO/dist"
QUIET="${BW_QUIET:-0}"

: "${IDF_PATH:?IDF_PATH nicht gesetzt -- vorher . \$IDF_PATH/export.sh}"

VERSION="$(tr -d '[:space:]' < "$REPO/version.txt")"
BUILD_NO="$(tr -d '[:space:]' < "$REPO/build_number.txt")"
BUILD_NO=$((BUILD_NO + 1))
FULL="${VERSION}.${BUILD_NO}"

declare -A TARGETS=(
  [tul_c6]="TUL esp32c6"
  [eul_c6]="EUL esp32c6"
  [tul_c3]="TUL esp32c3"
  [eul_c3]="EUL esp32c3"
)

WANTED=("$@")
[ ${#WANTED[@]} -eq 0 ] && WANTED=(tul_c6 eul_c6 tul_c3 eul_c3)

mkdir -p "$DIST" "$BUILD_ROOT"
echo "== USBSerial32 v$FULL =="

for name in "${WANTED[@]}"; do
  spec="${TARGETS[$name]:-}"
  [ -n "$spec" ] || { echo "unbekanntes Ziel: $name" >&2; exit 2; }
  product="${spec%% *}"; chip="${spec##* }"
  bdir="$BUILD_ROOT/$name"

  echo "-- $name  ($product / $chip)"
  idf.py -C "$REPO" -B "$bdir" \
         -DBW_PRODUCT="$product" -DBW_VERSION="$FULL" -DBW_QUIET="$QUIET" \
         set-target "$chip" >/dev/null
  idf.py -C "$REPO" -B "$bdir" \
         -DBW_PRODUCT="$product" -DBW_VERSION="$FULL" -DBW_QUIET="$QUIET" \
         build >/dev/null

  # merge-bin nimmt die Offsets aus dem Build, statt sie zu raten.
  merged="$bdir/factory.bin"
  idf.py -C "$REPO" -B "$bdir" merge-bin -o "$merged" >/dev/null

  out="busware-$(echo "$product" | tr 'A-Z' 'a-z')-${chip#esp32}-serial-transparent.factory.bin"
  cp "$merged" "$DIST/.tmp.$out"
  sync
  mv "$DIST/.tmp.$out" "$DIST/$out"
  echo "   -> dist/$out  ($(stat -c%s "$DIST/$out") Bytes, md5 $(md5sum "$DIST/$out" | cut -d' ' -f1))"
done

printf '%s' "$BUILD_NO" > "$REPO/.build_number.tmp"
sync
mv "$REPO/.build_number.tmp" "$REPO/build_number.txt"
echo "== fertig, Build $BUILD_NO =="
