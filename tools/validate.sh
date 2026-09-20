#!/usr/bin/env bash
# usage: tools/validate.sh <recipe.json>...
#   builds build/validator/beef-validate when a recipe or validator source
#   changed, then runs it on the given files
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${NATIVE_CXX:-${CXX:-}}"
if [ -z "$CXX" ]; then
	command -v clang++ >/dev/null 2>&1 || {
		echo "clang++ is not on PATH; run 'nix develop' first" >&2
		exit 1
	}
	CXX=clang++
fi
OUT=build/validator
BIN="$OUT/beef-validate"
mkdir -p "$OUT"
if [ ! -x "$BIN" ] || [ -n "$(find src/validator src/recipe src/Core.h -newer "$BIN" -print -quit 2>/dev/null)" ]; then
	echo "building the validator" >&2
	"$CXX" -std=c++23 -O1 -Wall -Wextra -I src -I src/extern \
		src/validator/Main.cpp src/recipe/*.cpp -o "$BIN"
fi
exec "$BIN" "$@"
