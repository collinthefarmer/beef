#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
if [ -z "${BEEF_DEV_SHELL:-}" ]; then
  exec nix develop --command python3 tools/gate.py "$@"
fi
exec python3 tools/gate.py "$@"
