Status: first-party permission adopted; publication and runtime acceptance remain open. Basis:
[publication review](../checkpoints/publication-provenance-2026-09-24.md).
The owner confirmed sole ownership of original project code; that does not
include third-party code incorporated in or referenced by the project.

# Publication source delivery — implementation and decision draft

Validation for completed steps is recorded in the
[implementation checkpoint](../checkpoints/publication-implementation-2026-09-24.md).

## Ordered implementation

1. **Implemented:** adopted the exact LGPL-2.1 API header at
   `QTR-Modding/SKSE-Menu-Framework-3-API@1dcb70179076aae4ab626f43c5baab2735ca5877`.
   Preserve upstream bytes, add the license, and update notice provenance and
   compatibility-profile fingerprints together. Check all used exports and
   build Windows. Keep the current runtime baseline preliminary until tested.
2. **Implemented:** adopted the first-party additional permission in `COPYING.md`
   after the incorporated-license scope review. The GPL text and upstream grants
   remain unchanged; no permission is implied on behalf of third-party authors.
3. **Implemented:** replaced the seven captured EFSH fixtures and their seven goldens with authored
   synthetic fixtures. Preserve meaningful coverage: fill vs. bare import,
   colors, animation, ignored flags, reference fallback, selectors, round trips, and
   malformed input. Document authorship/provenance. Validate the importer and
   parser suites in ordinary and sanitizer builds.
4. **Implemented:** `tools/source-inventory.json` defines a curated inventory including `src`, CMake configuration,
   profile JSON, build/install/setup scripts, schemas, templates, presets,
   fixture-safe tests, notices, and required build documentation. Review runtime
   logs separately; exclude caches, build outputs, local editor state, game data,
   and proprietary SDKs. Separately review history before exposing a Git remote.
5. **Implemented:** archive-friendly build identity from `SOURCE_PROVENANCE.json`;
   preserve the actual revision and source fingerprint without manufacturing a
   commit. Fail on profile/source drift by default. Intentional source edits require
   `BEEF_ALLOW_MODIFIED_SOURCE=ON`, record the original archive fingerprint,
   and compute a new build identity. Profile drift remains an error. Ordinary
   Git checkout behavior is retained. See [build instructions](../build.md#source-archives-without-git-metadata).
6. **Implemented:** export exact pinned dependency Git blobs and notices alongside the project
   snapshot. Record recursively needed submodules and generated-source inputs;
   use local FetchContent overrides to consume bundled dependency sources.
   Compiler/tool versions and CRT/SDK content fingerprints are recorded in the
   source-delivery checkpoint. The installed SDK version receipt is unavailable;
   retain that metadata on the next SDK provisioning pass.
7. **Validated:** built from a fresh extraction without repository metadata or pre-existing
   `_deps` content. Source inventory checks, native tests, Windows build,
   candidate packaging, and source/profile identity matching all passed: 102/102
   native tests and the full uncached Windows build. The
   [source-delivery checkpoint](../checkpoints/source-delivery-2026-09-24.md) keeps source and
   binary archive SHA-256 values together. Bit-for-bit reproducibility is a
   separate claim that requires evidence.
8. Freeze the candidate after runtime acceptance. Deliver exact source beside
   the binary or give clear, version-specific equivalent source access and
   retain availability. Publish only after the unresolved rights and runtime
   questions are settled.

## Adopted first-party policy

The owner accepts GPL or more permissive terms. The project now explicitly uses
GPL-3.0-only with the modding/linking permission in [COPYING.md](../../COPYING.md).
That notice replaces the draft previously recorded here. It covers rights in
original material only, preserves third-party terms and source obligations,
and permits downstream removal of additional permissions as GPL section 7 allows.

First-party C++ files and the generated plugin declaration refer to the notice.
The Community Shaders-derived material header separately identifies its upstream
terms. Required legal notices are an explicit exception to the coding convention
against explanatory comments. The adopted notice is part of the build identity
and is included in source, mod, symbols and staging inventories.

See the [adoption checkpoint](../checkpoints/licensing-policy-2026-09-24.md) for
scope review, verification and remaining release limits.
