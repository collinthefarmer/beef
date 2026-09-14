#!/usr/bin/env bash
# usage: tools/layers.sh
#   Checks every #include "..." under src/ against the layer graph
#   REQUIREMENTS.md records, and checks that the engine-free directories name
#   no engine symbol. Prints file:line and the offending line for each edge
#   outside the table; exits non-zero if there is one.
set -uo pipefail
cd "$(git rev-parse --show-toplevel 2>/dev/null)" 2>/dev/null || cd "$(dirname "$0")/.."

# One directory per row, listing what it may include. "Core.h" is the shared
# value vocabulary and is open to every layer; the other root headers
# (PCH.h, Identity.h, Settings.h, SettingsFile.h) belong to the adapters.
declare -A ALLOWS=(
	[recipe]="Core.h recipe"
	[mesh]="Core.h recipe mesh"
	[planners]="Core.h recipe mesh planners"
	[diagnostics]="Core.h diagnostics"
	[studio]="Core.h recipe mesh planners diagnostics studio"
	[render]="Core.h recipe mesh planners diagnostics studio render PCH.h Identity.h Settings.h SettingsFile.h"
	[engine]="Core.h recipe mesh planners diagnostics studio render engine PCH.h Identity.h Settings.h SettingsFile.h"
	[menu]="Core.h recipe mesh planners diagnostics studio render engine menu PCH.h Identity.h Settings.h SettingsFile.h"
)
PURE=(recipe mesh planners diagnostics studio)

findings=0
report() {
	printf '%s\n' "$1" >&2
	findings=$((findings + 1))
}

allows() {
	local dir="$1" target="$2" allowed
	for allowed in ${ALLOWS[$dir]}; do
		[ "$allowed" = "$target" ] && return 0
	done
	return 1
}

for dir in "${!ALLOWS[@]}"; do
	while IFS= read -r hit; do
		[ -n "$hit" ] || continue
		file="${hit%%:*}"
		rest="${hit#*:}"
		line="${rest%%:*}"
		include="${rest#*:}"
		include="${include#*\"}"
		include="${include%%\"*}"
		if [ "$include" = "${include%%/*}" ]; then
			case "$include" in
			Core.h | PCH.h | Identity.h | Settings.h | SettingsFile.h) target="$include" ;;
			*)
				report "$file:$line: include \"$include\" names no directory; src is the only include root"
				continue
				;;
			esac
		else
			target="${include%%/*}"
		fi
		allows "$dir" "$target" ||
			report "$file:$line: $dir may not include \"$include\""
	done < <(grep -rn '^[[:space:]]*#include[[:space:]]*"' "src/$dir" 2>/dev/null)
done

for dir in "${PURE[@]}"; do
	while IFS= read -r hit; do
		[ -n "$hit" ] || continue
		report "${hit%%:*}:${hit#*:}"
	done < <(grep -rn '\bRE::' "src/$dir" 2>/dev/null |
		sed -E 's/^([^:]*:[0-9]+):.*/\1: engine-free directory names an RE:: symbol/')
done

if [ "$findings" -gt 0 ]; then
	echo "layers: $findings edge(s) outside the graph in REQUIREMENTS.md" >&2
	exit 1
fi
echo "layers: every include stays inside the graph"
