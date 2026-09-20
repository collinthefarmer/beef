#!/usr/bin/env bash
set -uo pipefail
cd "$(git rev-parse --show-toplevel 2>/dev/null)" 2>/dev/null || cd "$(dirname "$0")/.."

STAGE="${1:-}"
case "$STAGE" in
	commit | push) ;;
	*) echo "usage: tools/gate.sh {commit|push}" >&2; exit 2 ;;
esac

# Every gate runs through the pinned dev-shell tools, never whatever
# clang-format/clang-tidy happens to be on the base PATH. This is what makes the
# gate identical for a human in the shell, a human outside it, and Claude Code.
if [ -z "${BEEF_DEV_SHELL:-}" ]; then
	command -v nix >/dev/null 2>&1 || { echo "gate: run inside 'nix develop' (nix not found on PATH)." >&2; exit 1; }
	exec nix develop --command tools/gate.sh "$STAGE"
fi

not_in_compile_db() {
	local db=build/clangd/compile_commands.json
	[ -f "$db" ] || { printf '%s\n' "$@"; return; }
	python3 - "$db" "$@" <<'PY'
import json, pathlib, sys
db = json.load(open(sys.argv[1]))
have = {str(pathlib.Path(d['file']).resolve()) for d in db}
root = pathlib.Path('.').resolve()
for f in sys.argv[2:]:
    if str((root / f).resolve()) not in have:
        print(f)
PY
}

require_compile_db() {
	[ -f build/clangd/compile_commands.json ] && return 0
	echo "gate: no build/clangd/compile_commands.json; run tools/compile-db.sh first." >&2
	exit 1
}

warn_missing_from_db() {
	local missing
	mapfile -t missing < <(not_in_compile_db "$@")
	[ ${#missing[@]} -eq 0 ] && return 0
	{
		echo
		echo "gate: these sources are not in the compile DB, so clang-tidy cannot lint them:"
		printf '    %s\n' "${missing[@]}"
		echo "run tools/compile-db.sh (a source file was added or moved), then try again."
	} >&2
	exit 1
}

if [ "$STAGE" = commit ]; then
	mapfile -t staged < <(git diff --cached --name-only --diff-filter=ACM -- 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h' | grep -Ev '^(src/(_old|extern|cs)|tests/_old)/')
	[ ${#staged[@]} -eq 0 ] && exit 0

	if ! tools/format.sh --check "${staged[@]}"; then
		{
			echo
			echo "commit blocked: staged files are not clang-formatted. run:"
			echo "    tools/format.sh ${staged[*]}"
			echo "then restage them and commit again."
		} >&2
		exit 1
	fi

	mapfile -t staged_cpp < <(printf '%s\n' "${staged[@]}" | grep '^src/.*\.cpp$')
	if [ ${#staged_cpp[@]} -gt 0 ]; then
		require_compile_db
		warn_missing_from_db "${staged_cpp[@]}"
		tools/tidy.sh "${staged_cpp[@]}" >/dev/null || exit 1
		tools/tidy-baseline.sh --gate "${staged_cpp[@]}" || exit 1
	fi
	exit 0
fi

# push: the fuller sweep, run once per push rather than once per commit.
if ! tools/format.sh --check; then
	echo "push blocked: run tools/format.sh to fix formatting." >&2
	exit 1
fi

# The one layer graph: tools/layers.sh reports every include outside the table
# in REQUIREMENTS.md, and every RE:: symbol in an engine-free directory.
if ! tools/layers.sh >/dev/null; then
	echo "push blocked: see the edges above; the graph is the table in tools/layers.sh." >&2
	exit 1
fi

# No comments in the C++ sources (CLAUDE.md rule 2): a fact the code cannot
# state goes in REFERENCE.md under its module's heading. The pattern matches a
# // that begins a line or follows whitespace after a ; or }, so a URL inside a
# string literal is not one. src/_old, src/extern and src/cs are frozen or
# vendored and keep their comments. A NOLINT marker is a tool directive rather
# than prose; the one in src/engine/RecipeStore.cpp is the only one and its
# reason is in REFERENCE.md.
mapfile -t commented < <(grep -rnE '(^|[[:space:];}])//' src --include='*.cpp' --include='*.h' | grep -Ev '^src/(_old|extern|cs)/' | grep -v NOLINT)
if [ ${#commented[@]} -gt 0 ]; then
	{
		printf '%s\n' "${commented[@]}"
		echo
		echo "push blocked: the C++ sources carry no comments. Move the fact to"
		echo "REFERENCE.md under the module's heading, or say it with a name."
	} >&2
	exit 1
fi

if ! BEEF_SANITIZE=1 tests/run-native.sh >/dev/null; then
	echo "push blocked: sanitized native tests failed (ASan/UBSan). run BEEF_SANITIZE=1 tests/run-native.sh." >&2
	exit 1
fi

require_compile_db
mapfile -t all_cpp < <(find src -name '*.cpp' -not -path 'src/_old/*' -not -path 'src/extern/*' -not -path 'src/cs/*')
warn_missing_from_db "${all_cpp[@]}"
if ! tools/tidy.sh --jobs=8 >/dev/null; then
	echo "push blocked: clang-tidy could not run; is the compile DB current? (tools/compile-db.sh)" >&2
	exit 1
fi
if ! tools/tidy-baseline.sh --check; then
	{
		echo
		echo "push blocked: clang-tidy findings differ from tools/tidy-baseline.txt."
		echo "review the difference; if it is intended, regenerate the baseline:"
		echo "    tools/tidy.sh --force && tools/tidy-baseline.sh"
	} >&2
	exit 1
fi
