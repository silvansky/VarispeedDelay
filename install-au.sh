#!/bin/bash
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)/build/VarispeedDelay_artefacts/Release/AU/VarispeedDelay.component"
DST="$HOME/Library/Audio/Plug-Ins/Components"
SUDO=""

for arg in "$@"; do
  case "$arg" in
    --system) DST="/Library/Audio/Plug-Ins/Components"; SUDO="sudo" ;;
    --user)   ;;
    *) echo "usage: $0 [--user|--system]" >&2; exit 2 ;;
  esac
done

[ -d "$SRC" ] || { echo "Not built: $SRC" >&2; exit 1; }

PLIST="$SRC/Contents/Info.plist"
read -r TYPE SUBTYPE MANU <<< "$(/usr/bin/plutil -extract AudioComponents.0 json -o - "$PLIST" \
  | /usr/bin/python3 -c 'import json,sys; d=json.load(sys.stdin); print(d["type"], d["subtype"], d["manufacturer"])')"

codesign --force --deep --sign - "$SRC"

$SUDO mkdir -p "$DST"
$SUDO rm -rf "$DST/VarispeedDelay.component"
$SUDO cp -R "$SRC" "$DST/"
$SUDO xattr -dr com.apple.quarantine "$DST/VarispeedDelay.component" 2>/dev/null || true

echo "Installed to $DST/VarispeedDelay.component"

killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2

auval -v "$TYPE" "$SUBTYPE" "$MANU" 2>&1 | tail -20
