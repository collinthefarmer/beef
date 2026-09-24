# Publication preparation — implementation checkpoint

Follow-up to the [provenance review](publication-provenance-2026-09-24.md).
The first-party permission proposal remains unadopted. No installation or
publication occurred.

## Changes

- Adopted the exact API header from
  `SKSE-Menu-Framework-3-API@1dcb70179076aae4ab626f43c5baab2735ca5877`,
  SHA-256 `48416e8220ca777e2fffc2ef2baf21f699ab2e6c409d437f44eec5e311c3524c`.
  Its LGPL-2.1 license, inventory origin, wrapper notice, and profile hash
  changed together. Upstream CRLF bytes are preserved, which makes ordinary
  Git diffs large; `git diff --ignore-space-at-eol` shows the API changes.
  Removed declarations are unused by this project. Runtime baseline remains
  3.13.0 and unverified; the separately installed framework keeps its own terms.
- Replaced all seven captured EFSH records and seven derived goldens with
  synthetic cases. Independent assertions cover fill/bare imports, neutral/dark
  color fallbacks, alpha fallbacks, form-key identity, tiling and scrolling;
  goldens cover complete imported recipes and round trips. See
  [fixture provenance](../../tests/fixtures/README.md). Captures remain in Git
  history, whose publication remains a separate review.
- Added explicit archive provenance to build identity. Producers write
  `SOURCE_PROVENANCE.json` from the checkout and effective profile. Matching
  archives produce identical manifest/header bytes without Git metadata.
  Source/profile drift and malformed/missing records fail; intentional source
  edits require an explicit option and retain the original archive hash while
  computing a new identity. See [build instructions](../build.md#source-archives-without-git-metadata).

## Validation

- Native and ASan/UBSan: `recipe_importer`, `recipe_recipe`, and `recipe_efsh`
  passed (3/3 each). The importer passes 170 checks, including seven goldens.
- License inventory: 2 tests; menu exports: 1; compatibility: 3;
  packaging: 8 — all passed.
- Generated-output integration test passed: unchanged archive/check-out
  equality, incremental no-op, source drift, explicit modification, profile
  drift, malformed/missing provenance, and a Git parent outside the archive.
- Windows Release configure/build passed. Existing missing-field initializer
  warnings in Menu.cpp and Workspace.cpp remain; no new build error.
- Real project copied without `.git`, configured with the Windows preset and
  existing pinned dependency sources through FetchContent overrides, and built
  `BuildIdentity`. Its manifest exactly matched the checkout. This was not a
  full DLL rebuild in the extraction and did not test a dependency bundle.
- Candidate creation/integrity validation passed; both archives contain the
  new LGPL notice. This is an offline candidate, not runtime acceptance.

Build identity: `1303a555e66b-3e41cb276a187006-Release-ce036c51`.
The revision identifies the base commit; the fingerprint identifies the modified
build inputs. Source changes have not been committed or published.

| Archive | SHA-256 |
|---|---|
| Mod | `b090881d98ebe2374b410578036c25ade7e788bddf8ff920f737e05b20265654` |
| Symbols | `77d6be4ef87749f47978aae8d144d4096dc47174850a22ceedb900e0b7836b05` |

## Remaining work

The [source-delivery plan](../plans/publication-source-delivery-2026-09-24.md)
still owns first-party policy adoption, curated inventory and history/log
review, complete dependency-source collection, and a full clean-extraction
build/test/package run. In-game acceptance remains pending. Archive provenance
alone is neither source-delivery completion nor proof of reproducibility.
