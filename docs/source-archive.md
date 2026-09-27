# Source candidate archives

These are local review artifacts. Runtime acceptance and publication remain
open. First-party GPLv3 and linking-permission terms are in `COPYING.md`. No Git history, captured runtime
logs, proprietary SDK, game files, or installed peer DLLs are included.

## Produce a snapshot

Inside `nix develop`, configure the selected Windows profile so FetchContent
has checked out its dependencies, commit everything, then run:

```sh
cmake --preset windows-release
python3 tools/source-archive.py --profile steam-1.6.1170
```

The script refuses a tree with uncommitted or untracked files, because the
archive holds only HEAD. It writes
`dist/archives/BetterEnchantmentEffects-<profile>-<revision>-source.tar.gz`
and an adjacent `.sha256` file. `--dependencies` names another FetchContent
`_deps` directory, and `--output` another destination.

The first-party part is `git archive HEAD`: the tracked files are the
inventory. `.gitattributes` marks development-only paths `export-ignore`:
`.claude`, `.githooks`, `MAP.md`, `docs/.obsidian`,
`docs/checkpoints` (it holds captured runtime logs), `docs/history` and
`docs/plans`. It also stamps `cmake/source-revision.txt` with the commit hash,
which build identity reads when there is no `.git`.

Each dependency is `git archive` of its pinned revision from the profile, taken
from its FetchContent checkout and placed under `dependencies/<name>/`. Untracked files
and edits in the checkout do not enter the archive. `git archive` omits
submodule contents; the current three dependency revisions have no
submodules. CommonLib's unused `Flash/` Scaleform source and three Address
Library test-data files under `tests/REL/` are excluded by `EXCLUDED` in the
script. CommonLib tests that need the excluded data are not part of this
configuration. Bundled spdlog includes its generated fmt headers and their
embedded notices. The menu API, JSON, and Community Shaders reference headers
remain in the project source with their notices in `THIRD_PARTY_NOTICES.md`.

The archive also holds `bundled-dependencies.cmake`, a cache initializer that
selects the profile, points each `FETCHCONTENT_SOURCE_DIR_*` at
`dependencies/<name>`, and sets `FETCHCONTENT_FULLY_DISCONNECTED`. The
SHA-256 file identifies the whole archive; retain it with the binary checksums.

## Build a fresh extraction

Extract with ordinary tar so script executable permissions are retained:

```sh
sha256sum -c path/to/source.tar.gz.sha256
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

Compiler/SDK and Nix dependencies must already be provisioned to build without
network access. No pre-existing project build directory is needed. The
extracted build ID is `<revision>-archive-<config>`, with the same revision
as the producing checkout. Binary byte equality is a separate property and is
not promised, particularly for PDB paths.

The external toolchain is pinned by `flake.lock`: Linux x86-64, clang/LLVM and
lld, CMake, Ninja, Python, check-jsonschema, and the Nix host compiler wrapper.
Windows compilation additionally requires the Microsoft CRT and Windows SDK
under `XWIN_DIR`, fetched once with the flake's hash-pinned, unfree
`windows-sdk` package (SDK 10.0.26100, CRT 14.44.17.14). These components retain Microsoft's terms and are not
redistributed here. Git is needed for the synthetic Git fixtures in tooling
tests; it is not needed to derive the archive's plugin build identity.

See [build.md](build.md#source-archives-without-git-metadata) for build identity
without `.git`. The checksum establishes content integrity, not producer
authenticity or a legal compliance conclusion.
