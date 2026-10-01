#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
MO2_MODS_DIR=$(tools/mo2-mods-dir.sh)
NAME=$(sed -n 's/^project(\([A-Za-z0-9_]*\).*/\1/p' CMakeLists.txt)
ZIP="build/regression/BEEF-regression.zip"
DST="$MO2_MODS_DIR/$NAME Regression"
[ -f "$ZIP" ] || { echo "no regression package at $ZIP; run tools/regression-compile.py then tools/regression-package.py" >&2; exit 1; }
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
python3 -m zipfile -e "$ZIP" "$STAGE"
mkdir -p "$DST"
status=0
if command -v rsync >/dev/null 2>&1; then
	rsync -a --checksum "$STAGE/" "$DST/" || status=$?
else
	tar -C "$STAGE" -cf - . | tar -C "$DST" -xf - || status=$?
fi
if [ "$status" -ne 0 ]; then
	echo "installation incomplete; some files may have been copied (is the game running?)" >&2
	exit "$status"
fi
echo "installed to $DST"
