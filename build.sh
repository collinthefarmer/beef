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
BUILD_DIR="build/$CONFIG"
exec nix shell --extra-experimental-features 'nix-command flakes' \
	nixpkgs#llvmPackages.clang-unwrapped nixpkgs#llvmPackages.llvm nixpkgs#lld \
	nixpkgs#cmake nixpkgs#ninja \
	-c bash -euo pipefail -c "
		cmake -S . -B '$BUILD_DIR' -G Ninja \
			-DCMAKE_BUILD_TYPE='$CONFIG' \
			-DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.toolchain.cmake
		cmake --build '$BUILD_DIR' $*
	"
