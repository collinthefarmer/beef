#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/.."

TIDY=0
CHANGED=0
TOP=15
for arg in "$@"; do
	case "$arg" in
		--tidy) TIDY=1 ;;
		--changed) CHANGED=1 ;;
		--top=*) TOP="${arg#--top=}" ;;
		*) echo "usage: tools/lint.sh [--tidy] [--changed] [--top=N]" >&2; exit 2 ;;
	esac
done

echo "# readability metrics"
python3 tools/readability.py --top "$TOP"

echo
echo "# co-change coupling (pairs of files changed together, last 400 commits)"
git log --format=%H --max-count=400 | while read -r sha; do
	git show --pretty=format: --name-only "$sha" | grep -E '^src/.*\.(cpp|h)$' | sort -u | paste -sd' ' -
done | awk '{ for (i = 1; i <= NF; i++) for (j = i + 1; j <= NF; j++) print $i, $j }' \
	| sort | uniq -c | sort -rn | head -"$TOP" | sed 's/^/  /'

echo
echo "# preconditions, expected to fall to zero"
printf '  raw engine pointer members: '
grep -cE '^[[:blank:]]+(const )?(RE::)?[A-Za-z_][A-Za-z0-9_]*(::[A-Za-z_][A-Za-z0-9_]*)*[[:space:]]*\*[[:space:]]*[a-z_][A-Za-z0-9_]*[[:space:]]*(=[^;]*)?;' src/*.h 2>/dev/null | grep -v ':0$' | awk -F: '{ n += $2 } END { print n + 0 }'
printf '  default: labels in switches: '
grep -c 'default:' src/*.cpp src/*.h 2>/dev/null | grep -v ':0$' | awk -F: '{ n += $2 } END { print n + 0 }'

if [ "$TIDY" -eq 1 ]; then
	if [ "$CHANGED" -eq 1 ]; then tools/tidy.sh --changed; else tools/tidy.sh; fi
fi
