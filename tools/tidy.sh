#!/usr/bin/env bash
set -uo pipefail
cd "$(dirname "$0")/.."

OUT=build/tidy
BUILD="${TIDY_BUILD_DIR:-build/Release}"
ANALYZER=0
FORCE=0
JOBS=6
FILES=()
SELECTED=0
for arg in "$@"; do
	case "$arg" in
		--analyzer) ANALYZER=1; OUT=build/tidy-analyzer ;;
		--force) FORCE=1 ;;
		--jobs=*) JOBS="${arg#--jobs=}" ;;
		--changed) SELECTED=1; mapfile -t FILES < <(git diff --name-only --diff-filter=ACMRT HEAD -- 'src/*.cpp' | grep -v '^src/_old/') ;;
		--summary) SUMMARY_ONLY=1 ;;
		-*) echo "usage: tools/tidy.sh [--analyzer] [--changed] [--force] [--jobs=N] [--summary] [file...]" >&2; exit 2 ;;
		*) SELECTED=1; FILES+=("$arg") ;;
	esac
done
[ "$SELECTED" -eq 0 ] && mapfile -t FILES < <(find src -name '*.cpp' -not -path 'src/_old/*' -not -path 'src/extern/*' -printf '%s %p\n' | sort -n | cut -d' ' -f2-)

DB=build/clangd/compile_commands.json
[ -f "$DB" ] || { echo "no $DB; run tools/compile-db.sh" >&2; exit 1; }
mkdir -p "$OUT"

# One result per source path, not per basename: src/engine/X.cpp and
# src/studio/X.cpp must not share a cache file.
result_name() {
	local stem="${1%.cpp}"
	echo "${stem//\//_}.txt"
}

# A result is fresh when it is newer than its source, the compile database,
# .clang-tidy, and every header its object depended on at the last build
# (ninja's dependency log). A source with no recorded dependencies falls back
# to "newer than every header under src".
# stdin: one source per line. stdout: the stale sources (mode stale) or the
# fresh result files (mode fresh).
read -r -d '' CLASSIFY <<'PY'
import json, os, pathlib, shutil, subprocess, sys

mode, out, db, build = sys.argv[1], pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3]), pathlib.Path(sys.argv[4])
files = [line for line in sys.stdin.read().split('\n') if line]
root = pathlib.Path('.').resolve()


def mtime(path):
    try:
        return os.stat(path).st_mtime_ns
    except OSError:
        return None


def result_of(source):
    stem = source[:-4] if source.endswith('.cpp') else source
    return out / (stem.replace('/', '_') + '.txt')


def recorded_dependencies():
    ninja = shutil.which('ninja')
    if not ninja or not (build / '.ninja_deps').exists():
        return {}
    try:
        text = subprocess.run([ninja, '-C', str(build), '-t', 'deps'], capture_output=True,
                              text=True, timeout=120, check=False).stdout
    except (OSError, subprocess.SubprocessError):
        return {}
    deps, current = {}, None
    for line in text.splitlines():
        if line.startswith('    ') and current is not None:
            deps[current].append(line.strip())
        elif ':' in line and not line.startswith(' '):
            current = line.split(':', 1)[0]
            deps[current] = []
    return deps


def objects_by_source():
    try:
        entries = json.loads(db.read_text())
    except (OSError, ValueError):
        return {}
    build_abs = build.resolve()
    result = {}
    for entry in entries:
        source, output = entry.get('file'), entry.get('output')
        if not source or not output:
            continue
        output_path = pathlib.Path(output)
        if output_path.is_absolute():
            try:
                output = str(output_path.resolve().relative_to(build_abs))
            except ValueError:
                continue
        result[str(pathlib.Path(source).resolve())] = output
    return result


def own_header(path):
    try:
        rel = pathlib.Path(path).resolve().relative_to(root)
    except (ValueError, OSError):
        return None
    text = str(rel)
    if not text.startswith('src/') or text.startswith('src/_old/') or text.startswith('src/extern/'):
        return None
    return rel


newest_header = None


def newest_header_mtime():
    global newest_header
    if newest_header is None:
        newest_header = 0
        for header in pathlib.Path('src').rglob('*.h'):
            if own_header(header) is not None:
                newest_header = max(newest_header, mtime(header) or 0)
    return newest_header


deps = recorded_dependencies()
objects = objects_by_source() if deps else {}
db_mtime = mtime(db)
config_mtime = mtime('.clang-tidy')


def fresh(source):
    result = result_of(source)
    source_mtime = mtime(source)
    result_mtime = mtime(result)
    if source_mtime is None or result_mtime is None:
        return False
    if result.with_name(result.name + '.part').exists():
        return False
    if source_mtime > result_mtime:
        return False
    if db_mtime is not None and db_mtime > result_mtime:
        return False
    if config_mtime is not None and config_mtime > result_mtime:
        return False
    headers = deps.get(objects.get(str((root / source).resolve()), ''))
    if headers is None:
        return newest_header_mtime() <= result_mtime
    for header in headers:
        if own_header(header) is None:
            continue
        header_mtime = mtime(header)
        if header_mtime is None or header_mtime > result_mtime:
            return False
    return True


for source in files:
    ok = fresh(source)
    if mode == 'stale' and not ok:
        print(source)
    elif mode == 'fresh' and ok:
        print(result_of(source))
PY
classify() {
	python3 -c "$CLASSIFY" "$1" "$OUT" "$DB" "$BUILD"
}

if [ "${SUMMARY_ONLY:-0}" -eq 0 ]; then
	TIDY_BIN="${CLANG_TIDY:-clang-tidy}"
	command -v "$TIDY_BIN" >/dev/null 2>&1 || { echo "$TIDY_BIN is not on PATH; run 'nix develop' first" >&2; exit 1; }
	CHECKS=""
	[ "$ANALYZER" -eq 0 ] && CHECKS="--checks=-clang-analyzer-*"
	EXISTING=()
	for f in "${FILES[@]}"; do
		[ -f "$f" ] && EXISTING+=("$f")
	done
	TODO=()
	if [ ${#EXISTING[@]} -gt 0 ]; then
		if [ "$FORCE" -eq 1 ]; then
			TODO=("${EXISTING[@]}")
		else
			mapfile -t TODO < <(printf '%s\n' "${EXISTING[@]}" | classify stale)
		fi
	fi
	if [ ${#TODO[@]} -gt 0 ]; then
		if ! printf '%s\n' "${TODO[@]}" | xargs -P "$JOBS" -I{} bash -c '
			f="$1"; out="$2"; bin="$3"; checks="$4"
			stem="${f%.cpp}"
			dest="$out/${stem//\//_}.txt"
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

# Summarize only fresh results for this selection. Old logs (including deleted
# sources) must not silently contribute findings to a targeted run.
RESULTS=()
if [ ${#FILES[@]} -gt 0 ]; then
	mapfile -t RESULTS < <(printf '%s\n' "${FILES[@]}" | classify fresh)
fi
echo
if [ ${#RESULTS[@]} -eq 0 ]; then
	echo "# clang-tidy: 0 of ${#FILES[@]} files have fresh results"
else
	echo "# clang-tidy: ${#RESULTS[@]} of ${#FILES[@]} files have fresh results, $(cat "${RESULTS[@]}" | grep -c 'warning:' || true) findings"
	echo
	cat "${RESULTS[@]}" | grep -oE '\[[a-z][a-z0-9-]+-[a-z0-9.-]+\]$' | sort | uniq -c | sort -rn | sed 's/^/  /' || true
fi
