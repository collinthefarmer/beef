Status: implementation complete; all 111 sanitizer/tool suites and the Windows
build passed. The additional historical-peek regression and incremental
Windows rebuild also passed.

# Material term editing

Term tuning previously retained every generated source in the live paint recipe
because the session's undo catalog doubled as its dependency list. Preview and
Keep now select sources referenced by current terms, including muted terms and the current peek.
Undo retains definitions and recreates sources as needed. Keeping over an
existing mask removes its abandoned source dependencies only when the prepared
recipe has no other references, in the same document edit transaction. Unrelated
unused sources, including older orphans without identifiable ownership, remain.

Repeated material offers across geometries create the same cluster operation.
The chooser now offers each once and reports when its appearance varies across
geometries. This does not merge the per-geometry clustering analyses or change
cluster terms into global material identities.

Diffuse previously affected clustering only through luma. CPU analysis and GPU
classification now also use mean squared RGB distance with `weights.color`,
default 1, range 0..10. Set it to 0 for the old distance calculation. Existing
cluster IDs can change because the default analysis now sees color differences.
The recipe parser, serializer, schema, source forms, term forms, source rows and
render cache identity include this setting.

Regression coverage includes repeated tuning, undo, muted sources, term removal,
saving replacements with shared and unrelated sources, offer consolidation,
equal-luma color separation, disabling color, and editor/recipe round-trips.
Source-cleanup-only changes passed all 34 studio suites before the material work.
Combined evidence: `/tmp/beef-material-editor-validation.log` (111 suites
passed, Windows DLL linked). Historical-peek follow-up evidence:
`/tmp/beef-material-peek-validation.log`.

In game: tune a cluster seed or weight repeatedly and inspect source rows;
undo and redo; mute/unmute; keep over a saved mask. Check the material chooser
with multiple geometries. On differently colored regions with similar RMAOS
and brightness, compare color 0 with color 1, then inspect the rendered cluster
mask against its analysis. Old unrelated orphan sources are not swept.

The current in-game testing session was preserved at user request, without
analysis, under `build/evidence/session-20260925T022309Z` and its adjacent ZIP. It includes
181 SKSE logs/trace files, installed recipe/configuration snapshots (including
SKSE Output user overrides), the candidate beef testing profile, build logs,
and a checksummed capture manifest. One live trace segment changed during copy;
that fact is recorded. The trace policy had already discarded intermediate
segments. Originals were not changed. The loaded game build is recorded in the
copied plugin log; the newly built editor fixes were not installed during capture.

Updated complete bundle: `build/demo/ThreeRecipes-material-editing-test.zip`. SHA-256: `d177743a1c74698a0f90bf629db46757b96bb021c5c203dece96536c049625e4`.
ESP and all three recipe JSON files are byte-identical to the previous bundle;
the DLL matches the final rebuild, and all 1024 presenter assets are included.
