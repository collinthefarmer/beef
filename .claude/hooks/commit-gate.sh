#!/usr/bin/env bash
set -uo pipefail

payload=$(cat)
# Fast path: the vast majority of Bash calls are not commits. Skip the JSON
# parse entirely unless the raw payload even mentions a commit.
case "$payload" in
	*commit*) ;;
	*) exit 0 ;;
esac

cmd=$(printf '%s' "$payload" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("tool_input", {}).get("command", ""))' 2>/dev/null)
# Gate only commit-creating commands; git commit-graph is maintenance, not a commit.
case "$cmd" in
	*"git commit-graph"*) exit 0 ;;
	*"git commit"* | *"git "*" commit "* | *"git "*" commit") ;;
	*) exit 0 ;;
esac

cwd=$(printf '%s' "$payload" | python3 -c 'import sys, json; print(json.load(sys.stdin).get("cwd", ""))' 2>/dev/null)
[ -n "$cwd" ] && cd "$cwd" 2>/dev/null || true
root=$(git rev-parse --show-toplevel 2>/dev/null) || exit 0
cd "$root" || exit 0

# tools/gate.sh re-execs itself through 'nix develop' when run outside the dev
# shell, so the pinned clang-format/clang-tidy are always used. Exit 2 blocks
# the commit and bypasses --no-verify.
if [ -x tools/gate.sh ]; then
	tools/gate.sh commit >&2 || {
		echo "commit blocked by Claude Code gate (format + lint; bypasses --no-verify)." >&2
		exit 2
	}
fi
exit 0
