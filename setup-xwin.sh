#!/usr/bin/env bash
set -euo pipefail
: "${XWIN_DIR:=$HOME/.xwin/splat}"
if [ -d "$XWIN_DIR/crt" ] && [ -d "$XWIN_DIR/sdk" ]; then
	echo "xwin splat already present at $XWIN_DIR"
	exit 0
fi
command -v xwin >/dev/null 2>&1 || { echo "xwin is not on PATH; run 'nix develop' first" >&2; exit 1; }
mkdir -p "$(dirname "$XWIN_DIR")"
xwin --accept-license splat --output "$XWIN_DIR"
