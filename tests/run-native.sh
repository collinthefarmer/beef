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
FLAGS=(-std=c++23 -O1 -Wall -Wextra -I src -I src/_old -I src/extern "-DBEEF_FIXTURES_DIR=\"$PWD/tests/fixtures\"")
if [ -n "${BEEF_SANITIZE:-}" ]; then
	FLAGS+=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g)
fi
MODEL=(src/_old/Recipe.cpp src/_old/RecipeJson.cpp src/_old/Expression.cpp src/_old/Signals.cpp src/_old/Importer.cpp src/_old/Timing.cpp)

status=0
compile() {
	local src="$1" obj="$OUT/$(echo "$1" | tr / _).o"
	if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ -n "$(find src tests -name '*.h' -newer "$obj" 2>/dev/null | head -1)" ]; then
		"$CXX" "${FLAGS[@]}" -c "$src" -o "$obj" || return 1
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
RUN_ARGS=()
build_and_run timing_tests tests/timing_tests.cpp src/_old/Timing.cpp
build_and_run settings_tests tests/settings_tests.cpp src/_old/SettingsCore.cpp src/_old/Timing.cpp
build_and_run expression_tests tests/expression_tests.cpp src/_old/Expression.cpp
build_and_run recipe_tests tests/recipe_tests.cpp "${MODEL[@]}"
build_and_run signal_tests tests/signal_tests.cpp "${MODEL[@]}"
RUN_ARGS=("$@")
build_and_run importer_tests tests/importer_tests.cpp "${MODEL[@]}"
build_and_run bake_tests tests/bake_tests.cpp src/_old/Mesh.cpp "${MODEL[@]}"
build_and_run analysis_tests tests/analysis_tests.cpp src/_old/Analysis.cpp src/_old/Mesh.cpp "${MODEL[@]}"
build_and_run merge_tests tests/merge_tests.cpp src/_old/Merge.cpp "${MODEL[@]}"
build_and_run region_tests tests/region_tests.cpp src/_old/Region.cpp src/_old/Expression.cpp
build_and_run studio_tests tests/studio_tests.cpp src/_old/Studio.cpp src/_old/MenuState.cpp src/_old/History.cpp src/_old/Edits.cpp src/_old/EditCheck.cpp src/_old/Paint.cpp src/_old/Region.cpp src/_old/Analysis.cpp src/_old/Mesh.cpp "${MODEL[@]}"
build_and_run edits_tests tests/edits_tests.cpp src/_old/Edits.cpp src/_old/History.cpp "${MODEL[@]}"

if command -v check-jsonschema >/dev/null 2>&1; then
	echo "== schema"
	check-jsonschema --schemafile schema/recipe.schema.json schema/example-magicka.json tests/fixtures/recipes/*.json || status=1
else
	echo "== schema (skipped: check-jsonschema not on PATH)"
fi
exit $status
