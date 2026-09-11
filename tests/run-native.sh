#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
CXX="${NATIVE_CXX:-${CXX:-}}"
if [ -z "$CXX" ]; then
	command -v clang++ >/dev/null 2>&1 || { echo "clang++ is not on PATH; run 'nix develop' first" >&2; exit 1; }
	CXX=clang++
fi
if [ -n "${BEEF_SANITIZE:-}" ] && [ "$(basename "$CXX")" = "g++" ]; then
	echo "g++ cannot build this project under BEEF_SANITIZE; run 'nix develop' first, or set NATIVE_CXX to a clang++" >&2
	exit 1
fi
OUT="${TEST_OUT_DIR:-build/native-tests-$(basename "$CXX")${BEEF_SANITIZE:+-sanitized}}"
mkdir -p "$OUT"
FLAGS=(-std=c++23 -O1 -Wall -Wextra -I src -I src/extern -I tests "-DBEEF_FIXTURES_DIR=\"$PWD/tests/fixtures\"")
if [ -n "${BEEF_SANITIZE:-}" ]; then
	FLAGS+=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g)
fi

MODULES=(recipe mesh studio planners)
declare -A MODULE_DEPS=(
	[recipe]=""
	[mesh]="recipe"
	[studio]="recipe mesh"
	[planners]="recipe mesh"
)

status=0
needs_rebuild() {
	local src="$1" obj="$2" dep="$2.d" prereq
	[ -f "$obj" ] || return 0
	[ "$src" -nt "$obj" ] && return 0
	[ -f "$dep" ] || return 0
	while IFS= read -r prereq; do
		[ -n "$prereq" ] || continue
		[ "$prereq" -nt "$obj" ] && return 0
	done < <(sed -e 's/^[^:]*://' -e 's/\\//g' "$dep" | tr ' \t' '\n\n' | grep -v '^$')
	return 1
}
compile() {
	local src="$1" obj="$OUT/$(echo "$1" | tr / _).o"
	if needs_rebuild "$src" "$obj"; then
		"$CXX" "${FLAGS[@]}" -MMD -MF "$obj.d" -c "$src" -o "$obj" || return 1
	fi
	echo "$obj"
}
build_and_run() {
	local name="$1"; shift
	echo "== $name"
	local objs=()
	local src
	for src in "$@"; do
		local obj
		obj=$(compile "$src") || { status=1; return; }
		objs+=("$obj")
	done
	"$CXX" "${FLAGS[@]}" "${objs[@]}" -o "$OUT/$name" || { status=1; return; }
	"$OUT/$name" "${RUN_ARGS[@]}" || status=1
}

RUN_ARGS=("$@")
suites=0
for mod in "${MODULES[@]}"; do
	module_sources=()
	for dir in ${MODULE_DEPS[$mod]:-} "$mod"; do
		while IFS= read -r -d '' src; do
			module_sources+=("$src")
		done < <(find "src/$dir" -name '*.cpp' -print0 2>/dev/null | sort -z)
	done
	while IFS= read -r -d '' test; do
		suites=$((suites + 1))
		name="${mod}_$(basename "$test" .cpp)"
		build_and_run "$name" "$test" "${module_sources[@]}"
	done < <(find "tests/$mod" -name '*_tests.cpp' -print0 2>/dev/null | sort -z)
done

if [ "$suites" -eq 0 ]; then
	echo "zero suites green"
fi

if command -v check-jsonschema >/dev/null 2>&1; then
	echo "== schema"
	check-jsonschema --schemafile schema/recipe.schema.json schema/example-magicka.json || status=1
else
	echo "== schema (skipped: check-jsonschema not on PATH)"
fi

exit $status
