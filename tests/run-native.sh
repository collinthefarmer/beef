#!/usr/bin/env bash
# usage: tests/run-native.sh
#   SUITE=<substring>   build and run only suites whose name contains it
#   BEEF_SANITIZE=1     build with ASan/UBSan
#   NATIVE_CXX=<c++>    compiler (default clang++ from the dev shell)
#   NATIVE_JOBS=<n>     parallel compiles (default 4; WSL runs out of memory past ~6)
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
OUT_ABS="$(cd "$OUT" && pwd)"
FLAGS=(-std=c++23 -O1 -Wall -Wextra -pthread -I src -I src/extern -I tests "-DBEEF_FIXTURES_DIR=\"$PWD/tests/fixtures\"" "-DBEEF_TEST_OUT_DIR=\"$OUT_ABS\"")
if [ -n "${BEEF_SANITIZE:-}" ]; then
	FLAGS+=(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer -fno-sanitize-recover=undefined -g)
fi
JOBS="${NATIVE_JOBS:-4}"

MODULES=(recipe mesh studio planners)
declare -A MODULE_DEPS=(
	[recipe]=""
	[mesh]="recipe"
	[studio]="recipe mesh"
	[planners]="recipe mesh"
)

object_of() {
	echo "$OUT/$(echo "$1" | tr / _).o"
}
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
compile_one() {
	local src="$1" obj
	obj="$(object_of "$src")"
	if needs_rebuild "$src" "$obj"; then
		echo "cc $src" >&2
		"$CXX" "${FLAGS[@]}" -MMD -MF "$obj.d" -c "$src" -o "$obj"
	fi
}
export -f object_of needs_rebuild compile_one
export CXX OUT
FLAGS_QUOTED="$(printf '%q ' "${FLAGS[@]}")"
export FLAGS_QUOTED

# Every suite is declared first; the union of their sources is compiled in
# parallel once; then each suite links and runs in order.
SUITE_NAMES=()
SUITE_SOURCES=()
declare_suite() {
	local name="$1"; shift
	if [ -n "${SUITE:-}" ] && [[ "$name" != *"$SUITE"* ]]; then
		return
	fi
	SUITE_NAMES+=("$name")
	SUITE_SOURCES+=("$*")
}

for mod in "${MODULES[@]}"; do
	module_sources=()
	for dir in ${MODULE_DEPS[$mod]:-} "$mod"; do
		while IFS= read -r -d '' src; do
			module_sources+=("$src")
		done < <(find "src/$dir" -name '*.cpp' -print0 2>/dev/null | sort -z)
	done
	while IFS= read -r -d '' test; do
		name="${mod}_$(basename "$test" .cpp)"
		declare_suite "$name" "$test" "${module_sources[@]}"
	done < <(find "tests/$mod" -name '*_tests.cpp' -print0 2>/dev/null | sort -z)
done

declare_suite engine_sessionqueue tests/engine/sessionqueue_tests.cpp src/engine/SessionQueue.cpp src/diagnostics/Trace.cpp
declare_suite engine_applicator tests/engine/applicator_tests.cpp src/engine/ApplicationService.cpp src/studio/ApplicationRecord.cpp src/engine/SessionQueue.cpp src/diagnostics/Trace.cpp
declare_suite engine_applicationservice tests/engine/applicationservice_tests.cpp src/engine/ApplicationService.cpp src/studio/ApplicationRecord.cpp src/engine/SessionQueue.cpp src/diagnostics/Trace.cpp
declare_suite engine_textfile tests/engine/textfile_tests.cpp src/engine/TextFile.cpp
declare_suite diagnostics_trace tests/diagnostics/trace_tests.cpp src/diagnostics/Trace.cpp
declare_suite settingspublication tests/settingspublication_tests.cpp src/Settings.cpp

if [ ${#SUITE_NAMES[@]} -eq 0 ]; then
	echo "no suite matched SUITE='${SUITE:-}'" >&2
	exit 1
fi

if ! printf '%s\n' "${SUITE_SOURCES[@]}" | tr ' ' '\n' | sort -u | xargs -P "$JOBS" -I{} bash -c 'eval "FLAGS=($FLAGS_QUOTED)"; compile_one "$1"' _ {}; then
	echo "native compile failed" >&2
	exit 1
fi

status=0
for i in "${!SUITE_NAMES[@]}"; do
	name="${SUITE_NAMES[$i]}"
	echo "== $name"
	objs=()
	for src in ${SUITE_SOURCES[$i]}; do
		objs+=("$(object_of "$src")")
	done
	"$CXX" "${FLAGS[@]}" "${objs[@]}" -o "$OUT/$name" || { status=1; continue; }
	"$OUT/$name" || status=1
done

if [ -n "${SUITE:-}" ]; then
	exit $status
fi

if command -v check-jsonschema >/dev/null 2>&1; then
	echo "== schema"
	check-jsonschema --schemafile schema/recipe.schema.json schema/example-magicka.json || status=1
else
	echo "== schema (skipped: check-jsonschema not on PATH)"
fi

python3 tests/tools/presenter_tests.py || status=1
python3 tests/tools/tidy_tests.py || status=1

exit $status
