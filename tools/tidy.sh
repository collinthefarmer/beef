#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/.."

OUT=build/tidy
ANALYZER=0
FORCE=0
JOBS=1
FILES=()
for arg in "$@"; do
	case "$arg" in
		--analyzer) ANALYZER=1; OUT=build/tidy-analyzer ;;
		--force) FORCE=1 ;;
		--jobs=*) JOBS="${arg#--jobs=}" ;;
		--changed) mapfile -t FILES < <(git diff --name-only HEAD -- 'src/*.cpp' | grep -v '^src/_old/') ;;
		--summary) SUMMARY_ONLY=1 ;;
		-*) echo "usage: tools/tidy.sh [--analyzer] [--changed] [--force] [--jobs=N] [--summary] [file...]" >&2; exit 2 ;;
		*) FILES+=("$arg") ;;
	esac
done
[ ${#FILES[@]} -eq 0 ] && mapfile -t FILES < <(find src -name '*.cpp' -not -path 'src/_old/*' -not -path 'src/extern/*' -printf '%s %p\n' | sort -n | cut -d' ' -f2-)

DB=build/clangd/compile_commands.json
[ -f "$DB" ] || { echo "no $DB; run tools/compile-db.sh" >&2; exit 1; }
mkdir -p "$OUT"


if [ "${SUMMARY_ONLY:-0}" -eq 0 ]; then
	TIDY_BIN="${CLANG_TIDY:-clang-tidy}"
	command -v "$TIDY_BIN" >/dev/null 2>&1 || { echo "$TIDY_BIN is not on PATH; run 'nix develop' first" >&2; exit 1; }
	CHECKS=""
	[ "$ANALYZER" -eq 0 ] && CHECKS="--checks=-clang-analyzer-*"
	TODO=()
	for f in "${FILES[@]}"; do
		[ -f "$f" ] || continue
		dest="$OUT/$(basename "$f" .cpp).txt"
		if [ -f "$dest.part" ]; then
			mv "$dest.part" "$dest.skipped"
			echo "skip $f (a previous run was killed on it)" >&2
			continue
		fi
		if [ "$FORCE" -eq 0 ] && { [ -f "$dest" ] || [ -f "$dest.skipped" ]; }; then
			continue
		fi
		TODO+=("$f")
	done
	if [ ${#TODO[@]} -gt 0 ]; then
		printf '%s\n' "${TODO[@]}" | xargs -P "$JOBS" -I{} bash -c '
			f="$1"; out="$2"; bin="$3"; checks="$4"
			dest="$out/$(basename "$f" .cpp).txt"
			echo "tidy $f" >&2
			"$bin" -p build/clangd --quiet $checks "$f" > "$dest.part" 2>/dev/null
			mv "$dest.part" "$dest"
		' _ {} "$OUT" "$TIDY_BIN" "$CHECKS"
	fi
fi

echo
echo "# clang-tidy: $(ls "$OUT"/*.txt 2>/dev/null | wc -l) of ${#FILES[@]} files, $(cat "$OUT"/*.txt 2>/dev/null | grep -c 'warning:' || true) findings"
SKIPPED=$(ls "$OUT"/*.skipped 2>/dev/null | wc -l)
[ "$SKIPPED" -gt 0 ] && echo "  $SKIPPED file(s) too heavy to lint here: $(ls "$OUT"/*.skipped | xargs -n1 basename | sed 's/\.txt\.skipped//' | tr '\n' ' ')"
echo
cat "$OUT"/*.txt 2>/dev/null | grep -oE '\[[a-z][a-z0-9-]+-[a-z0-9.-]+\]$' | sort | uniq -c | sort -rn | sed 's/^/  /' || true
