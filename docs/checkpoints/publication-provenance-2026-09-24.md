Status: record. Menu API license alternative, cimgui baseline, source inputs,
and remaining delivery work.

# Publication and provenance review — 2026-09-24

Review base: `1303a55`. The owner confirmed sole copyright ownership of the
project's original code. Third-party material is outside that confirmation.
This pass records source evidence and a delivery plan; it does not change the
project license or clear a release for publication.

## Menu API: an alternative license exists, but the files differ

The [pinned framework README](https://github.com/QTR-Modding/SKSE-Menu-Framework-3/blob/c97cdce6dd207c7cf1401611bc82bd7e8f97a814/README.md)
expressly directs header users to a separate API repository. That repository's
[LICENSE at 1dcb701](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API/blob/1dcb70179076aae4ab626f43c5baab2735ca5877/LICENSE)
is LGPL 2.1. This is relevant evidence missing from the earlier notice audit.

The [API header at that revision](https://github.com/QTR-Modding/SKSE-Menu-Framework-3-API/blob/1dcb70179076aae4ab626f43c5baab2735ca5877/SKSEMenuFramework.h)
does not match our header byte-for-byte. After newline normalization, our copy
adds `<cstdint>`, full-path/rename/delete section helpers, an API-version helper,
and a path-separator comment. Current project code calls none of those added APIs.
The shared declarations and other wrapper implementations match.

Recommended next implementation: adopt the exact separately licensed API header,
retain its LGPL notice and pinned source provenance, update the profile hash,
and rerun export checks and the Windows build. Verify the pinned candidate peer
still provides every used export. This avoids claiming that an alternate license
necessarily covers later additions in the framework-tree header. No wrapper or
profile change was made in this review.

The framework DLL itself remains a separately installed dependency. Header
licensing and the permitted terms for the overall Skyrim/SKSE/plugin combination
are separate questions; dynamic export lookup alone settles neither.

## cimgui: matching source baseline identified

The framework's `include/cimgui.h` is byte-identical (SHA-256
`935161c5727f883d25dd9d25be62cb1d136a581c7d9aaa187fc432259a6f2459`) to
[cimgui 1.90.8dock](https://github.com/cimgui/cimgui/tree/7c16d31cdb9d2db3038b324fe967ffa76b02c8c4).
Its implementation differs only in the relative paths of the two ImGui includes
after newline normalization. The tag's MIT license is byte-identical to the
notice already shipped. The notice inventory now cites that matching revision,
replacing the earlier approximate attribution source.

This closes the matching-baseline/notice question. It does not prove which
historical generator invocation produced the framework files or reproduce the
framework's transformation into its exported C++ wrapper. Retain the generated
files and generator sources with source-delivery evidence where applicable.

## First-party permission: authority clarified, policy not adopted

GPL section 7 permits additional permissions for material whose rights the
grantor controls. The [FSF linking guidance](https://www.gnu.org/licenses/gpl-faq.en.html#GPLIncompatibleLibs)
also distinguishes an author's own permission from permissions needed for
incorporated code owned by others. Treat this as licensing guidance, not a
project-specific legal clearance.

The proposed policy is GPLv3 for original project code with an explicit,
limited permission for the intended Skyrim/SKSE/graphics runtime combination.
A reviewable [decision draft](../plans/publication-source-delivery-2026-09-24.md)
records scope and remaining dependencies. No additional permission has been
adopted. The existing Community Shaders exception governs its covered material;
it does not automatically license the rest of this project.

## Source-tree inputs beyond the binary inventory

The binary packages exclude game records, game textures, peer binaries, and the
historical runtime logs. That does not settle source distribution. The tracked
source tree includes:

- Seven captured EFSH JSON records and seven corresponding imported-recipe
  goldens. These contain game identifiers, texture paths, and captured values.
  `recipe_importer` reads the records and compares exact generated goldens;
  simply omitting them breaks the test distribution.
- Two startup evidence files under `docs/checkpoints/startup-evidence-2026-09-22`.
  They include local runtime observations and installed-content identifiers;
  review or omit them from a curated source archive. Retain honest historical
  references when a file is omitted.
- Literal game identifiers in other tests and documentation. These are not
  automatically equivalent to copied record datasets; do not label every
  identifier a redistribution violation.

No game textures, DLLs, fonts, or binary assets were found among tracked files.
There are no tracked `reference/` or `decompiled/` directories in this checkout.
The [machine-readable inventory](publication-provenance-2026-09-24.json) records
paths and hashes of the 16 source-review inputs above.

Recommended fixture path: replace captured test records with deliberately
synthetic data covering the same importer branches and regenerate their goldens.
Keep game captures as local acceptance inputs if needed. Renaming a captured
record alone does not establish independent provenance. Existing fixtures were
not removed or rewritten in this review, and this is not a determination that
their distribution is prohibited. Publishing repository history requires its
own review; a curated archive does not erase old Git objects.

## Matching-source delivery

GPL section 1 defines Corresponding Source, including relevant build/install
scripts; section 6(d) describes equivalent network access and the distributor's
availability responsibility. See the project's [GPL text](../../LICENSE) and
[official license](https://www.gnu.org/licenses/gpl-3.0.html).

Use a fixed source delivery for the exact candidate, with build identity,
compatibility profile, dependency revisions, notices, and a hashed inventory.
The three fetched dependency checkouts match the pins already recorded:
CommonLib `b93280e832f263dbef44e44cbe2936622a02f91a`, spdlog
`6fa36017cfd5731d617e1a934f0e5ea9c4445b13`, and rapidcsv
`68f57cc6c83d5e0992904398822453489d8dfac1`.

A plain `git archive` is not yet a tested source-delivery format:
`tools/build-identity.py` requires `git rev-parse HEAD`, and `Generated.cmake`
queries Git references. Add a reviewed archive-provenance fallback or choose a
reviewed Git delivery before claiming the source package rebuilds. Do not invent
a new Git commit locally and treat its generated identity as the original one.

Source acquisition must include complete needed dependency source and notices,
not merely fetched object files or a lockfile. Build scripts can be taught to use
bundled sources through CMake FetchContent overrides. Test that in a fresh source
extraction. Treat the compiler and Microsoft SDK as documented external build
prerequisites with their own terms; do not bundle the SDK or proprietary game.
A pinned dependency URL is provenance, not evidence that corresponding source
has actually been delivered and remains available.

## Outcome

The cimgui notice origin is corrected, the API licensing alternative is identified,
and the owner/scope and source-delivery work are concrete. Open work remains:
API-header selection, adoption of a first-party linking policy after third-party
review, fixture/history disposition, source-archive tooling, and a clean-extraction
build with candidate/source checksums. See the ordered implementation plan.

No license change, dependency swap, fixture deletion, upstream contact,
installation, or publication was performed. Downloaded evidence and normalized
diffs are retained locally under `/tmp/beef-publication-audit`.

Validation: both license-inventory tests and all eight package regression tests
pass after the notice-origin update. `git diff --check` passes. No C++ behavior
or vendored header bytes changed, so this review did not rebuild or replace
the previously recorded candidate archives.

Subsequent work: [implementation checkpoint](publication-implementation-2026-09-24.md)
records API-header adoption, fixture replacement, and archive-identity support.
The findings above describe the tree at the initial review.
