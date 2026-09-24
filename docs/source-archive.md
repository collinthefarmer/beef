# Source candidate archives

These are local review artifacts. Runtime acceptance and publication remain
open. First-party GPLv3 and linking-permission terms are in `COPYING.md`. No Git history, captured runtime
logs, proprietary SDK, game files, or installed peer DLLs are included.

## Produce a snapshot

Inside `nix develop`, build the selected Windows candidate, then run:

```sh
cmake --build --preset windows-release --target package-candidate
python3 tools/source-archive.py create \
  --identity build/Release/generated/build-identity.json \
  --dependencies build/Release/_deps --output dist/archives
```

`tools/source-inventory.json` is the explicit first-party file list and
dependency revision inventory. New files require review and an inventory edit.
It includes the current synthetic fixtures, tests, templates, scripts, notices,
and build documentation. Historical documents referenced by retained docs are
not included; consult the original repository's reviewed documentation separately.
The producer rejects missing inputs, links, destination collisions, omitted
build-identity inputs, changed candidate source, and mismatched dependency pins.

Dependencies are exported directly from pinned Git blobs, without repository
metadata or untracked files. Tracked checkout edits are rejected. Submodules
and special entries require a new review rather than being silently omitted;
the current three dependency revisions have no submodules. CommonLib's unused
Flash/Scaleform source and three Address Library test-data files are explicitly
excluded. Its C++ sources, CMake files, source tests, and license remain intact;
CommonLib tests requiring the excluded data are not part of this configuration.
Bundled spdlog includes its generated fmt headers and their embedded notices.
The menu API, JSON, and Community Shaders reference headers remain in the
project source with the existing license inventory.

The archive includes `SOURCE_PROVENANCE.json`, `SOURCE_MANIFEST.json`, and a
`bundled-dependencies.cmake` cache initializer. The manifest hashes every file
and records executable permissions and dependency exclusions. The adjacent
SHA-256 file identifies the whole archive, including tests/docs/tools outside
the narrower plugin build fingerprint. Retain it with the binary checksums.

## Verify and build a fresh extraction

Run verification before extraction, then use ordinary tar so script executable
permissions are retained:

```sh
python3 tools/source-archive.py verify path/to/source.tar.gz
mkdir /tmp/beef-source-check
tar -xzf path/to/source.tar.gz -C /tmp/beef-source-check
cd /tmp/beef-source-check
nix develop
cmake --preset native
cmake --build --preset native
ctest --preset native
cmake --preset windows-release -C bundled-dependencies.cmake
cmake --build --preset windows-release --target package-candidate
```

The initializer selects the recorded profile, points FetchContent at bundled
sources, and disables dependency downloads. Compiler/SDK and Nix dependencies
must already be provisioned to build without network access. No pre-existing
project build directory is needed. Compare the extracted build identity with
`SOURCE_MANIFEST.json.identity`; unchanged sources must match. Binary byte
equality is a separate property and is not promised, particularly for PDB paths.

The external toolchain is pinned by `flake.lock`: Linux x86-64, clang/LLVM and
lld, CMake, Ninja, Python, check-jsonschema, and the Nix host compiler wrapper.
Windows compilation additionally requires the Microsoft CRT and Windows SDK
under `XWIN_DIR` (default `~/.xwin/splat`), provisioned separately using
`bash setup-xwin.sh`. These components retain Microsoft's terms and are not
redistributed here. The dated source-delivery checkpoint records the versions
actually exercised. Git is needed for the synthetic Git fixtures in tooling
tests; it is not needed to derive the archive's plugin build identity.

For intentional source modifications, see the archive identity options in
[build.md](build.md#source-archives-without-git-metadata). A verifier establishes
content consistency, not producer authenticity or a legal compliance conclusion.
