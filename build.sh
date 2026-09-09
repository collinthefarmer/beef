#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
: "${XWIN_DIR:=$HOME/.xwin/splat}"
export XWIN_DIR
CONFIG="${1:-Release}"
shift || true
if [ ! -d "$XWIN_DIR/crt" ]; then
	echo "no xwin splat at $XWIN_DIR; run ./setup-xwin.sh first" >&2
	exit 1
fi
for tool in cmake ninja clang-cl lld-link; do
	command -v "$tool" >/dev/null 2>&1 || { echo "$tool is not on PATH; run 'nix develop' first" >&2; exit 1; }
done
BUILD_DIR="build/$CONFIG"
cmake -S . -B "$BUILD_DIR" -G Ninja \
	-DCMAKE_BUILD_TYPE="$CONFIG" \
	-DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.toolchain.cmake
exec cmake --build "$BUILD_DIR" "$@"
