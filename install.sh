#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${MO2_MODS_DIR:=/mnt/a/mods/SkyrimSE/mods}"
NAME=$(sed -n 's/^project(\([A-Za-z0-9_]*\).*/\1/p' CMakeLists.txt)
SRC="dist/$NAME"
DST="$MO2_MODS_DIR/$NAME"
INI="SKSE/Plugins/$NAME.ini"
[ -f "$SRC/SKSE/Plugins/$NAME.dll" ] || { echo "no staged build in $SRC; run cmake --build --preset windows-release --target stage" >&2; exit 1; }
mkdir -p "$DST"
status=0
if command -v rsync >/dev/null 2>&1; then
	rsync -a --exclude "/$INI" "$SRC/" "$DST/" || status=$?
else
	tar -C "$SRC" --exclude "./$INI" -cf - . | tar -C "$DST" -xf - || status=$?
fi
[ -f "$DST/$INI" ] || cp "$SRC/$INI" "$DST/$INI"
if [ "$status" -ne 0 ]; then
	echo "some files were not copied (is the game running?)" >&2
fi
echo "installed to $DST (INI kept when present)"
exit $status
