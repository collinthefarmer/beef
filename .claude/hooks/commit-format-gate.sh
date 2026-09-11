#!/usr/bin/env bash
set -uo pipefail

cwd=$(cat | python3 -c 'import sys, json; print(json.load(sys.stdin).get("cwd", ""))' 2>/dev/null)
[ -n "$cwd" ] && cd "$cwd" 2>/dev/null || true

root=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
cd "$root" || exit 0

if [ -x .githooks/pre-commit ]; then
	if command -v clang-format >/dev/null 2>&1; then
		.githooks/pre-commit >&2
	else
		nix develop --command .githooks/pre-commit >&2
	fi || {
		echo "commit blocked by Claude Code format gate (bypasses --no-verify)." >&2
		exit 2
	}
fi
exit 0
