# Source candidate delivery checkpoint

Local source-delivery validation following the
[publication implementation](publication-implementation-2026-09-24.md).
Publication and first-party license-policy adoption remain pending.

## Inventory and dependencies

`tools/source-inventory.json` explicitly lists first-party files and dependency
revisions. `tools/source-archive.py` collects those files and exports dependency
Git blobs at the recorded revisions. It rejects missing/nonregular inputs,
unsafe or duplicate destinations, missing build-fingerprint inputs, changed
candidate source, changed dependency pins, and tracked dependency edits.
Untracked dependency files and Git metadata never enter the bundle. Special
entries/submodules fail until reviewed; the current pins have no submodules.

The source archive has 2,876 payload files plus `SOURCE_MANIFEST.json`:

| Dependency | Revision | Bundled files |
|---|---|---:|
| CommonLibSSE-NG | `b93280e832f263dbef44e44cbe2936622a02f91a` | 2,073 |
| spdlog | `6fa36017cfd5731d617e1a934f0e5ea9c4445b13` | 174 |
| rapidcsv | `68f57cc6c83d5e0992904398822453489d8dfac1` | 159 |

CommonLib excludes 56 unused Flash/Scaleform files and three Address Library
test-data files. Exact exclusions are in the inventory and source manifest.
Its source tests remain, but their excluded-data-dependent execution is not
supported by this bundle; CommonLib tests are disabled in the plugin build.
All required C++/CMake sources and upstream notices are retained. spdlog's
generated fmt headers and embedded notices are included. Existing vendored API,
JSON, and Community Shaders reference provenance stays in the license inventory.

The project snapshot includes synthetic fixtures, tests, schemas, templates,
presets, build/install/setup tools, and current build/component documentation.
It excludes Git history, historical documents and runtime logs, editor/agent
state, caches, binaries, game assets, peer DLLs, and Microsoft SDK contents.
Some historical links in retained documentation intentionally have no bundled
target; see [source archive instructions](../source-archive.md).

## Validation

- Five new archive boundary/integrity tests passed, including missing inputs,
  link/path/duplicate refusal, pinned exports, untracked-file exclusion, and
  altered/omitted/extra payload detection. Three profile tests passed after
  removing their dependency on the enclosing project's Git checkout.
- Created and verified the source archive, retaining script executable modes.
- Extracted into `/tmp/beef-source-release-z7w840m3`, with no `.git` or prior
  build/`_deps` directory. `CCACHE_DISABLE=1`; at most four compiler workers.
- Full native build and **102/102 CTest tests passed** from the extraction.
- Separate fresh extraction entered `nix develop --offline` successfully.
- Windows configured with `-C bundled-dependencies.cmake`. All three
  FetchContent source paths point inside the extraction;
  `FETCHCONTENT_FULLY_DISCONNECTED=ON`.
- Full uncached Windows Release build and candidate packaging passed. Both
  mod and symbols archives passed the package integrity check. The rebuilt
  identity manifest exactly equals the original candidate identity.
- Final source verification passed; every curated first-party file still
  matches the archived bytes and executable mode.

Logs are retained in the extraction as `validation-0.log` through
`validation-4.log`; they are not included in the source archive.

## Toolchain and identity

[Toolchain fingerprints](source-toolchain-2026-09-24.json) record clang/LLVM/lld
21.1.8, CMake 4.3.4, Ninja 1.13.2, Python 3.14.7, xwin 0.9.0, and the exact
`flake.lock` hash. Microsoft CRT headers identify 14.44.35220.0. The original
Windows SDK version receipt is unavailable; no SDK release number is inferred.
The CRT and SDK content hashes identify the installed inputs actually used.
They hash sorted regular-file paths relative to the splat root and file bytes,
each preceded by its length as an unsigned 8-byte little-endian integer,
matching the build-identity fingerprint convention. These inputs are external
prerequisites and are not redistributed in the archive. Preserve the vendor
version receipt on the next SDK provisioning pass.

Candidate identity: `1303a555e66b-3e41cb276a187006-Release-ce036c51`.
The revision is the base commit; the source fingerprint identifies modified
build inputs. The archive checksum also covers tests/docs/tools outside that
fingerprint. This is not an assertion that the dirty tree equals its commit.

Source archive SHA-256:
`24910da1148ba5025572cc3cf4ce9163bd5cd3dad83439da5a6251a8f867e3ef`.

Original binary candidate hashes are retained in the
[implementation checkpoint](publication-implementation-2026-09-24.md).
Fresh-extraction archive SHA-256 values:

- Symbols: `9bd8c7a39b580acc1223614596db337fcc97d2bca16cc1ef07d10ec9c10122b9`.
- Mod: `2436c2560ff0e19ae470eaac1d6fea5f32a450f48a4f7080e3e505f062df7e76`.

Logs, rebuilt archives and the combined source/binary/toolchain `receipt.json`
are retained under
`dist/source-validation/1303a555e66b-3e41cb276a187006-Release-ce036c51/`.
The different archive hashes are not treated as a failure: equality of DLL/PDB
bytes across build paths was not an acceptance condition.

## Remaining gates

First-party linking-policy adoption, history publication review, source hosting
and retention arrangements, and in-game acceptance remain open. The bundle is
a local candidate; no installation or publication was performed. Build-identity
equality is not a claim of bit-for-bit DLL/PDB reproducibility or a legal
compliance conclusion.
