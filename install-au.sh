#!/bin/bash
set -euo pipefail

SRC="$(cd "$(dirname "$0")" && pwd)/build/VarispeedDelay_artefacts/Release/AU/VarispeedDelay.component"
DST="/Library/Audio/Plug-Ins/Components"

[ -d "$SRC" ] || { echo "Not built: $SRC" >&2; exit 1; }

codesign --force --deep --sign - "$SRC"

sudo rm -rf "$DST/VarispeedDelay.component"
sudo cp -R "$SRC" "$DST/"
sudo xattr -dr com.apple.quarantine "$DST/VarispeedDelay.component" 2>/dev/null || true

echo "Installed to $DST/VarispeedDelay.component"

killall -9 AudioComponentRegistrar 2>/dev/null || true
sleep 2

auval -v aumf Vspd VSil 2>&1 | tail -20
