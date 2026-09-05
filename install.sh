#!/usr/bin/env bash
# Copies the staged mod folder (DLL, PDB, slot textures) into the MO2 mods
# directory as its own mod in one bulk transfer (rsync, or tar when rsync is
# missing). The INI is copied only when the mod has none: once installed it
# holds the user's saved settings and is never touched. Recipes are not
# staged; they live in the mod folder and are edited there. Enable the mod
# (named after the CMake project) in MO2's left pane after the first install.
set -euo pipefail
cd "$(dirname "$0")"
: "${MO2_MODS_DIR:=/mnt/a/mods/SkyrimSE/mods}"
NAME=$(sed -n 's/^project(\([A-Za-z0-9_]*\).*/\1/p' CMakeLists.txt)
SRC="dist/$NAME"
DST="$MO2_MODS_DIR/$NAME"
INI="SKSE/Plugins/$NAME.ini"
[ -f "$SRC/SKSE/Plugins/$NAME.dll" ] || { echo "no build in $SRC; run ./build.sh" >&2; exit 1; }
mkdir -p "$DST"
# A DLL locked by the running game makes the transfer fail on that one file
# and continue with the rest; the exit status is kept, not allowed to abort.
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
