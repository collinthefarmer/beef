#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/.."

CHECK=0
CHANGED=0
FILES=()
for arg in "$@"; do
	case "$arg" in
		--check) CHECK=1 ;;
		--changed) CHANGED=1 ;;
		-*) echo "usage: tools/format.sh [--check] [--changed] [file...]" >&2; exit 2 ;;
		*) FILES+=("$arg") ;;
	esac
done

FMT_BIN="${CLANG_FORMAT:-clang-format}"
command -v "$FMT_BIN" >/dev/null 2>&1 || { echo "$FMT_BIN is not on PATH; run 'nix develop' first" >&2; exit 1; }

if [ "$CHANGED" -eq 1 ]; then
	mapfile -t FILES < <(git diff --name-only HEAD -- 'src/*.cpp' 'src/*.h' | grep -Ev '^src/(_old|extern|cs)/')
fi
[ ${#FILES[@]} -eq 0 ] && mapfile -t FILES < <(find src -name '*.cpp' -o -name '*.h' | grep -Ev '^src/(_old|extern|cs)/' | sort)

if [ "$CHECK" -eq 1 ]; then
	bad=0
	for f in "${FILES[@]}"; do
		[ -f "$f" ] || continue
		if ! "$FMT_BIN" "$f" | diff -q "$f" - >/dev/null; then
			echo "needs formatting: $f"
			bad=$((bad + 1))
		fi
	done
	[ "$bad" -eq 0 ] && echo "all ${#FILES[@]} files formatted" || echo "$bad of ${#FILES[@]} files need formatting"
	exit $((bad > 0))
fi

n=0
for f in "${FILES[@]}"; do
	[ -f "$f" ] || continue
	"$FMT_BIN" -i "$f"
	n=$((n + 1))
done
echo "formatted $n files"
