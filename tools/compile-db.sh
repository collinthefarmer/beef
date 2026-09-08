#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
: "${XWIN_DIR:=$HOME/.xwin/splat}"
export XWIN_DIR
nix shell --extra-experimental-features 'nix-command flakes' \
	nixpkgs#llvmPackages.clang-unwrapped nixpkgs#llvmPackages.llvm nixpkgs#lld nixpkgs#cmake nixpkgs#ninja \
	-c cmake -S . -B build/Release -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-xwin.toolchain.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON > /dev/null
python3 - <<'PY'
import json, pathlib
root = str(pathlib.Path('.').resolve())
db = json.load(open('build/Release/compile_commands.json'))
own = [d for d in db if d['file'].startswith(root + '/src/') or d['file'].startswith(root + '/tests/')]
pathlib.Path('build/clangd').mkdir(exist_ok=True)
json.dump(own, open('build/clangd/compile_commands.json', 'w'), indent=1)
print(f"{len(own)} entries in build/clangd/compile_commands.json")
PY
