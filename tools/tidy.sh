#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/.."

OUT=build/tidy
ANALYZER=0
FORCE=0
JOBS=6
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

result_fresh() {
	local dest="$1" src="$2"
	[ -f "$dest" ] || return 1
	[ "$src" -nt "$dest" ] && return 1
	[ "$DB" -nt "$dest" ] && return 1
	[ -n "$(find src -name '*.h' -not -path 'src/_old/*' -not -path 'src/extern/*' -newer "$dest" -print -quit 2>/dev/null)" ] && return 1
	return 0
}


if [ "${SUMMARY_ONLY:-0}" -eq 0 ]; then
	TIDY_BIN="${CLANG_TIDY:-clang-tidy}"
	command -v "$TIDY_BIN" >/dev/null 2>&1 || { echo "$TIDY_BIN is not on PATH; run 'nix develop' first" >&2; exit 1; }
	CHECKS=""
	[ "$ANALYZER" -eq 0 ] && CHECKS="--checks=-clang-analyzer-*"
	TODO=()
	for f in "${FILES[@]}"; do
		[ -f "$f" ] || continue
		dest="$OUT/$(basename "$f" .cpp).txt"
		rm -f "$dest.part"
		if [ "$FORCE" -eq 0 ] && result_fresh "$dest" "$f"; then
			continue
		fi
		TODO+=("$f")
	done
	if [ ${#TODO[@]} -gt 0 ]; then
		if ! printf '%s\n' "${TODO[@]}" | xargs -P "$JOBS" -I{} bash -c '
			f="$1"; out="$2"; bin="$3"; checks="$4"
			dest="$out/$(basename "$f" .cpp).txt"
			echo "tidy $f" >&2
			if ! "$bin" -p build/clangd --quiet $checks "$f" > "$dest.part" 2>&1; then
				cat "$dest.part" >&2
				exit 1
			fi
			mv "$dest.part" "$dest"
		' _ {} "$OUT" "$TIDY_BIN" "$CHECKS"; then
			echo "clang-tidy failed; incomplete results were not cached" >&2
			exit 1
		fi
	fi
fi

echo
echo "# clang-tidy: $(ls "$OUT"/*.txt 2>/dev/null | wc -l) of ${#FILES[@]} files, $(cat "$OUT"/*.txt 2>/dev/null | grep -c 'warning:' || true) findings"
echo
cat "$OUT"/*.txt 2>/dev/null | grep -oE '\[[a-z][a-z0-9-]+-[a-z0-9.-]+\]$' | sort | uniq -c | sort -rn | sed 's/^/  /' || true
