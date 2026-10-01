#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
MO2_MODS_DIR=$(tools/mo2-mods-dir.sh)
NAME=$(sed -n 's/^project(\([A-Za-z0-9_]*\).*/\1/p' CMakeLists.txt)
SRC="dist/$NAME"
DST="$MO2_MODS_DIR/$NAME"
INI="SKSE/Plugins/$NAME.ini"
RECIPES="SKSE/Plugins/$NAME/recipes"
[ -f "$SRC/SKSE/Plugins/$NAME.dll" ] || { echo "no staged build in $SRC; run cmake --build --preset windows-release" >&2; exit 1; }
[ -f "$SRC/$INI" ] || { echo "no staged default INI in $SRC; run cmake --build --preset windows-release" >&2; exit 1; }
mkdir -p "$DST"
status=0
if command -v rsync >/dev/null 2>&1; then
	rsync -a --exclude "/$INI" --exclude "/$RECIPES" "$SRC/" "$DST/" || status=$?
else
	tar -C "$SRC" --exclude "./$INI" --exclude "./$RECIPES" -cf - . | tar -C "$DST" -xf - || status=$?
fi
if [ "$status" -eq 0 ] && [ ! -e "$DST/$INI" ] && [ ! -L "$DST/$INI" ]; then
	cp "$SRC/$INI" "$DST/$INI" || status=$?
fi
if [ "$status" -ne 0 ]; then
	echo "installation incomplete; some files may have been copied (is the game running?)" >&2
	exit "$status"
fi
echo "installed to $DST (existing INI and recipes kept)"
