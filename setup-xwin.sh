#!/usr/bin/env bash
# Downloads the Windows CRT and SDK (about 630 MB) into $XWIN_DIR via xwin.
set -euo pipefail
: "${XWIN_DIR:=$HOME/.xwin/splat}"
if [ -d "$XWIN_DIR/crt" ] && [ -d "$XWIN_DIR/sdk" ]; then
	echo "xwin splat already present at $XWIN_DIR"
	exit 0
fi
mkdir -p "$(dirname "$XWIN_DIR")"
nix shell --extra-experimental-features 'nix-command flakes' nixpkgs#xwin \
	-c xwin --accept-license splat --output "$XWIN_DIR"
