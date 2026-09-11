#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${XWIN_DIR:=$HOME/.xwin/splat}"
export XWIN_DIR
for tool in cmake ninja clang-cl lld-link python3; do
	command -v "$tool" >/dev/null 2>&1 || { echo "$tool is not on PATH; run 'nix develop' first" >&2; exit 1; }
done
cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release \
	-DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.toolchain.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
python3 - <<'PY'
import json, pathlib
root = str(pathlib.Path('.').resolve())
db = json.load(open('build/Release/compile_commands.json'))
own = [d for d in db if d['file'].startswith(root + '/src/') or d['file'].startswith(root + '/tests/')]

# CMAKE_CXX_COMPILER_LAUNCHER prepends ccache to each command; clang-tidy and
# clangd need the real compiler as argv[0], so drop the launcher token.
def strip_launcher(entry):
    if 'arguments' in entry and entry['arguments']:
        if pathlib.Path(entry['arguments'][0]).name == 'ccache':
            entry['arguments'] = entry['arguments'][1:]
    if 'command' in entry:
        parts = entry['command'].split(' ', 1)
        if len(parts) == 2 and pathlib.Path(parts[0]).name == 'ccache':
            entry['command'] = parts[1]
    return entry
own = [strip_launcher(d) for d in own]
pathlib.Path('build/clangd').mkdir(exist_ok=True)
json.dump(own, open('build/clangd/compile_commands.json', 'w'), indent=1)
print(f"{len(own)} entries in build/clangd/compile_commands.json")
PY
