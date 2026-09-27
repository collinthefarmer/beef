Status: history. The mask editor it reviews is rebuilt by
`docs/ui-v2-implementation-plan.md`, and the paint-era word "region" now means
an armor coverage area (`docs/conventions.md` glossary). Names and paths here
predate the critique remediation of 2026-09-14 (Plan C's file moves and Plan
D's renames); the root `README.md` lists the current set.

# Paint flow review and test coverage

Reviewed 2026-09-12 against the current working tree, including its existing
uncommitted changes. This is a review and coverage plan, not a production fix.
The reported symptom is that selecting an offer adds its term but sometimes
does not show the mask; Solo often, but not always, restores visibility.

Implementation follow-up: [first wave and validation](../checkpoints/first-wave-2026-09-12.md).
The findings and coverage matrix below preserve the diagnostic baseline;
the follow-up records the fixes and remaining work.

## Findings

### 1. Preview state is acknowledged before it is applied — high

`RebuildScratch` compares the desired expression with the held snapshot and
posts `ScratchRebuilt` immediately, even when no edit was generated. The game
thread has not necessarily applied previous edits or published their result.
Reproduction: snapshot says `0`; queue `1`; undo to `0` before publication.
The undo produces no correction and clears dirty. The queued `1` wins.
Solo/mute toggles use the same path. Refused scratch edits are logged by the
engine but do not restore dirty or expose a preview acknowledgment to the UI.

Evidence: [PaintPanel.cpp](../../src/menu/PaintPanel.cpp), `RebuildScratch`;
[TermTemplates.cpp](../../src/studio/TermTemplates.cpp), `ScratchEdits`;
[RecipeEditor.cpp](../../src/engine/RecipeEditor.cpp), `EditRecipe`.
The stale-snapshot sequence was reproduced with the current native functions.

### 2. Offer source creation and preview are separate transactions — high

`AddTermOfKind` builds sources from the published recipe, queues their edits,
then adds the UI term. Scratch is submitted separately. Two partition offers
built before a new snapshot both choose `partition`. Applying the second
source edit refuses the duplicate, but its term already references
`@partition`, now meaning the first offer's partition. This can produce a
wrong result even in Solo. Term-setting changes have the same structure.

Evidence: [PaintPanel.cpp](../../src/menu/PaintPanel.cpp), `AddTermOfKind` and
`DrawTermSettings`; [SourcePlan.cpp](../../src/studio/SourcePlan.cpp).
Reproduced using slots 32 and 33 planned against one source catalog.
An atomic edit for each click alone is insufficient: successive clicks must
also plan against pending edits or resolve source names on the owning thread.

### 3. Reference renaming can change the expression's meaning — high

Both Keep and preset materialisation rename references sequentially. If
source reuse maps `a -> b` and `b -> a`, the second replacement changes the
first replacement too. `@a - @b` becomes `@a - @a`, instead of `@b - @a`.
The resulting expression is valid and applies successfully, so ordinary
validation does not catch it. Keep requires differing source definitions
between the paint copy and target; presets can encounter this with existing
sources in an otherwise ordinary recipe.

Evidence: [PaintSession.cpp](../../src/studio/PaintSession.cpp), `KeepEdits`;
[TermTemplates.cpp](../../src/studio/TermTemplates.cpp), `MaterialiseTerm`.
Both paths reproduced. Remapping needs to operate once on original references.

### 4. Session transitions have no complete start/stop protocol — high

Keep acknowledgment and Discard clear the session but retain the Paint layout;
`PreparePainter` then automatically starts again. Continuing to author masks
may be intended, but restarting from the current frame's recipe is unsafe:
after Discard it may still be the transient `paint` recipe. A missing source
or failed transient insertion logs/returns without notifying the UI, leaving
it waiting for startup. The UI also accepts `BeginPaint` after switching to
Compose. The frame can produce this order when ModeBar posts the mode switch
and the still-active Paint body posts automatic startup later in the frame.

Evidence: [StudioPage.cpp](../../src/menu/StudioPage.cpp), `PreparePainter` and
`RenderStudio`; [MenuState.cpp](../../src/studio/MenuState.cpp), `endSession`
and `BeginPaint`; [RecipeEditor.cpp](../../src/engine/RecipeEditor.cpp),
`BeginPaint`. Reducer reproductions confirm restart conditions and a session
in Compose; the full SKSE scheduling sequence has not been executed.

### 5. Keep can retain an earlier session's name — medium

`LiveTextField` uses a persistent buffer. The proposed mask name is only a
hint, and session cleanup does not reset the buffer. Paint uses the same
transient recipe/widget scope each session. After typing a name once, a later
Keep popup can reuse it instead of the newly proposed name. `KeepEdits`
overwrites an existing mask with that name. The button does display the actual
name, so this is stale form state, not an invisible write target.

Evidence: [MenuWidgets.cpp](../../src/menu/MenuWidgets.cpp), `LiveTextField`;
[PaintPanel.cpp](../../src/menu/PaintPanel.cpp), `KeepMaskPopup`;
[PaintSession.cpp](../../src/studio/PaintSession.cpp), `KeepEdits`.
Source review; needs a widget-level or manual reproduction.

### 6. Expression limits silently omit terms — medium

`BuildMask` stops when its next combined expression exceeds 4096 characters
and returns the valid prefix. Both preview and Keep consume that prefix,
although the UI still displays all terms and says Keep writes every term.
A selected trailing term can therefore work in Solo and disappear from the
combined/saved result. Overflow should be reported, not treated as success.

Evidence: [Mask.cpp](../../src/studio/Mask.cpp), `BuildMask`;
[PaintPanel.cpp](../../src/menu/PaintPanel.cpp), `DrawMaskStack`/`KeepMaskPopup`.
Confirmed control flow; boundary regression remains to be added.

### 7. Geometry and render behavior need separate coverage

- Offers aggregate geometries, while component/chart/cluster terms retain
  local IDs without their originating geometry. Clicking an offer does not
  switch the viewed geometry. The same ID can mean something different, or
  be absent, elsewhere. `TermDetailOf` also selects the first matching kind,
  so its coverage can come from a different geometry. Applying masks to all
  geometries is explicitly documented by the UI; choosing automatic scoping
  would be a behavior change, not just a bug fix.
- Mesh masks bake raw UVs. Material inputs carry no material UV transform,
  while the checked-in Community Shaders reference applies material UV
  scale/offset. Nonidentity transforms are a likely alignment cause. Bake
  triangles outside the unit UV square also have no wrap-aware copies;
  overlapping UVs cannot represent independent regions in one texture.
- Failed mask preparation intentionally falls back to white/full coverage.
  That explains unexpected whole-surface visibility, not an invisible Solo
  term. Separately, Paint's main picture reports scratch problems but does
  not directly present the output's material/shell binding failure.
- Rendered-mask cache keys contain only mask name and size. All recipe stacks
  for a geometry receive the same `GeometryInputs`. Two contributing recipes
  with the same mask name can reuse the first recipe's rendered mask. This is
  especially relevant after leaving isolated Paint for normal composition.
  Cache lookup must distinguish recipe/evaluation identity too.

Evidence: [TermTemplates.cpp](../../src/studio/TermTemplates.cpp),
`OffersOfRecipe`/`TermDetailOf`; [PaintPanel.cpp](../../src/menu/PaintPanel.cpp);
[ShaderSource.cpp](../../src/render/ShaderSource.cpp), `BakeVS`;
[CompositorSource.cpp](../../src/render/CompositorSource.cpp), `MaterialInputs::From`,
`PrepareMask` and `PrepareRenderedMask`;
[ManagerApply.cpp](../../src/engine/ManagerApply.cpp), `PrepareChainStacks`;
[CS material reference](../../../../reference/community-shaders/TruePBR-SetupMaterial-excerpts.cpp).
These are source findings; native tests cannot establish on-screen D3D/CS behavior.

## Coverage design

Keep the existing pure tests, but add a native flow harness using the real
reducer, term/source planning, edit application, and selection resolution.
Provide independent operations to draw/collect a frame, dispatch intents,
drain game work, and publish snapshots. Advance them explicitly; do not use
sleeps. Inject start/edit/commit failures and duplicate or delayed results.
The frame orchestration and engine outcome protocol need a testable seam:
do not duplicate their decisions in a fake and then test only the fake.

Assert three things separately: displayed terms and status, the authoritative
scratch recipe/dependencies, and the original target recipe. Every accepted
preview revision must eventually converge; every refusal must be visible and
recoverable. Late work must not mutate a different session. Keep must preserve
expression meaning and commit once. Discard must leave the target unchanged.

### Native cases to add

| ID | Common action / fixture | Required assertion |
| --- | --- | --- |
| F01 | Enter Paint from a selected recipe; delay startup and publication | One start; target remains stable; offers become usable only when ready. |
| F02 | Add the first partition, bone, channel, preset, and saved-mask offer | Required sources exist; scratch matches the chosen term; target remains unchanged. |
| F03 | Add overlapping and disjoint regions using AND, OR, NOT | Evaluate known sample values, not only expression strings; verify intersection, union, and subtraction. |
| F04 | Solo A, Solo B, un-solo; mute/unmute; all muted | Preview reflects the effective stack; solo overrides mute consistently; all muted previews zero. |
| F05 | Reorder/remove the selected, soloed, or muted term | Selection and flags follow the term; no index points at a different term accidentally. |
| F06 | Edit term settings and raw text; clear; undo/redo | Model, dependencies and scratch converge; undo/redo restores the intended expression. |
| F07 | Queue change then undo before publishing either result | Final authoritative scratch equals the latest UI request, even when it equals the old snapshot. |
| F08 | Rapid distinct offers/settings using the same preferred source name | Each reference resolves to its intended definition; no accepted UI term points at a refused source edit. |
| F09 | Reject a source or scratch edit, then retry | Error is shown; pending/dirty state remains recoverable; no false acknowledgment. |
| F10 | Keep immediately after an offer or setting edit | Exact submitted full expression and dependencies are saved despite a lagging preview. |
| F11 | Keep while solo/mute is active | All authored terms are saved, as the UI currently promises; isolation affects preview only. |
| F12 | Keep a new mask; edit and overwrite an existing mask | Correct target/name; unrelated masks and outputs unchanged; one undo restores the prior target. |
| F13 | Refuse Keep; retry; duplicate/old acknowledgment; leave while pending | No partial target edit or duplicate commit; old results cannot close a new session. |
| F14 | Discard, successful Keep, exit during startup, missing source at startup | Explicit terminal/continuation state; no startup from transient `paint`; failure cannot strand the editor. |
| F15 | Change viewed geometry, lose the selected piece, unload actor/load save | Declared session ownership is preserved or ended explicitly; no accidental fallback target. |
| F16 | Switch material/shell, including failure | Mask unchanged; acknowledged surface matches output; binding error is visible. |
| F17 | Source aliases, swapped names, chained collisions in presets and Keep | Evaluate before/after reference remap with distinct source values; no cascading replacement. |
| F18 | Expression at/over length limit; 64 terms and attempted 65th | Complete expression accepted or explicit refusal; no successful prefix-only preview/Keep. |
| F19 | Two geometries with identical local IDs but different coverage | Declared cross-geometry semantics; detail and viewed geometry cannot silently misdescribe the offer. |

Place F03–F06/F18 in `mask_tests`/`menustate_tests`, F17 in
`termtemplates_tests`/`paintsession_tests`, and F19 in `termtemplates_tests`.
Use a new `paintflow_tests` suite for the protocol/frame cases after extracting
the orchestration seam. Test the complete offer operation across both UI and
recipe state, not just `SourcePlanBuilder`'s internal pending-name handling.

### Widget and in-game cases

| ID | Action / controlled fixture | Expected result |
| --- | --- | --- |
| V01 | Type a Keep name, finish, start another mask/recipe, reopen Keep | Fresh proposed/editing name; intentional overwrite remains possible. |
| V02 | Edit expression, press Enter; repeat using focus loss then Keep | Commit semantics are explicit; no unnoticed loss of the last typed edit. |
| V03 | First offer, second disjoint offer, OR, Solo, mute, clear, undo | Term list, thumbnail and worn-surface mask agree after processing settles. |
| V04 | Asymmetric UV checker with known region, identity then offset/nonuniform scale | Applied mask follows the intended mesh region; asymmetric fixture exposes flips and transposition. |
| V05 | UV seam, tiled/out-of-range UVs, overlapping charts | Defined wrapping behavior; unsupported ambiguity is visible rather than presented as a successful region selection. |
| V06 | Two geometries with different ID layouts; switch viewed geometry | Correct per-geometry picture and metadata under the declared scope. |
| V07 | Missing image/mesh, failed bake/interpreter/target allocation, unavailable emissive/shell binding | Distinguish empty coverage from failed preview; no silent whole-surface fallback presented as success. |
| V08 | Two active recipes, both defining `region` differently, same texture size | Each stack/thumbnail uses its own expression; reverse recipe order to detect cache reuse. |
| V09 | Frozen clock, animated mask, rapid toggles, save load/re-equip | Correct first frame and subsequent updates; no stale session, texture or suppression state. |

For V04–V08, assert sampled pixel values where a D3D harness is available;
otherwise record the fixture, action sequence, screenshot, plugin build and
diagnostic log. Native green tests are not evidence of renderer correctness.

## Suggested order and behavior decisions

1. Add the flow harness and failing regressions for F07–F09/F14; repair the
   source/preview/session protocol together.
2. Add semantic remapping regressions F17, then fix remapping; add F18 and
   reject expression overflow explicitly.
3. Cover Keep/Discard and form lifetime, then the routine authoring cases.
4. Validate geometry scope, UV handling and recipe-specific caches with render
   fixtures. Keep these independent from changing the default AND operator.

Before locking expected UI behavior, decide whether successful Keep/Discard
returns to Compose or leaves an idle/new Paint session, and whether a
geometry-labelled offer is local or deliberately re-evaluated on every shape.
Either choice needs explicit, stable ownership. Keep currently saves a named
mask; it does not automatically assign a new mask to a Compose layer.

## Verification performed

Temporary native reproductions confirmed the stale-preview sequence and
Keep/Discard restart conditions in the preceding diagnostic pass. This pass
also reproduced stale source-name planning, cascading renames in both Keep
and presets, and accepting startup after a mode exit. These reproductions
assert current defective behavior; they are not green regression tests of
the desired behavior and have not been added to the regular suite.

The preceding pass ran 29 `studio_menustate` and 20 `studio_paintsession`
checks. This pass ran 9 `studio_mask`, 10 `studio_termtemplates`,
15 `studio_selection`, and 9 `studio_sourceplan` checks. All passed: 92 checks
across six suites over the two passes. Their success does not cover the
protocol failures demonstrated by the temporary reproductions.

`git diff --check` passed and all 21 local source links in this document
resolve. No production changes or in-game validation were made during this
review. The coverage matrix above is planned work, not implemented coverage.
