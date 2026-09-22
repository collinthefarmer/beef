# UI v2 wishlist — implementation plan, 2026-09-14

Turns [`ui-v2-layout-wishlist-2026-09-14.md`](ui-v2-layout-wishlist-2026-09-14.md)
into sequenced, buildable slices over the existing scaffolding. The wishlist is
the agreed scope and cites the seams; this plan verifies them against the tree,
splits each item into reuse versus new, and orders the work so shared pieces land
first. Design is settled: the workspace stays imperative, both wide and narrow
paths stay, structural changes are section 3 amendments to
[`../ui-v2-implementation-plan.md`](../history/ui-v2-implementation-plan.md).

Reconciled against `cleanup/stage-0` @ `59d0924` on 2026-09-14: the intervening
71 commits were deferred Plan B/D refactors (source-kinds, `MenuState.h`
extraction, catch-alls) that moved seams but not the design. The seam citations
below are the reconciled lines; slice sequencing, dependencies, and the
additions are unchanged.

Reuse discipline (from the plan's reuse audit): no parallel field schema, no
second selection store, no duplicate history stack, no second rendering path.
Every slice extends an existing owner. Intermediate UI usability is explicitly
not required (plan amendment); check native decisions as they land and batch the
in-game passes.

## Additions the implementer should not rederive

Settled in the wishlist; treat as given, do not re-litigate:

- **Domain-identity `FieldKey`.** Replace the `ImGui::GetID` widget-id key with
  `recipe id + subject + field`. For term parameters the identity is `mask +
  term index + parameter`. One key type across all `MenuState` field maps.
- **Generic commit sink.** The tuning widget accepts a `FormField` plus an
  `apply` callback, not only recipe edit intents, so term-parameter rebuilds
  (which commit through `BuildTerm` and the field's `apply`, not `PostField`)
  can drive it.
- **Value-relative default range.** When no `range`/`workingRange`/`units` is
  known, `span = max(|v|, eps)`, slider range `[v - span, v + span]`, symmetric
  so negatives get a negative-extending range. No slider gates behind "Set slider
  range"; the opener demotes to *adjust*.
- **Board is its own top-level page**, beside Studio / Recipes / Setup. The
  recipe/overview node drops the board and becomes recipe settings only.
- **Surface → Output → Layer** navigator nesting; surface settings attach at the
  Surface node.
- **Output outline replaces the layer stack.** Per-layer controls move onto tree
  rows. Intents `RemoveLayer`, `PickLayer`, `MoveLayer`, `SoloLayer`, `MuteLayer`,
  `SetLayerBlend` already exist.
- **Output scalars inline on the topline** through `ScalarForm`.
- **Outputs are not collapsible** — a persistent indent, not `Section` headers.
- **Resources under a tab bar** (Signals / Curves / Sources / Masks) with
  **per-tab add buttons** replacing the combined `DrawResourceAddMenu`.
- **Header/action bars grouped and labelled by scope**: Session / Recipe /
  Audition. Solo and Mute are audition state and move out of the recipe group.
- **Relationship panel is a labelled table with a value column**; follow-links on
  cells; value blank where live state does not resolve it.
- **"New ..." combo entries navigate to the authoring surface.** New mask →
  `EditMaskAsTerms`; New input → the wizard.
- **Input wizard** over the existing `ConnectInput` / `ConnectionBuilder` logic:
  two entry points (standalone in the Signals tab; "New input" in a field's
  reference combo), Advanced → today's searchable table.
- **Terms editor** with per-offer inline preview and add; per-term tuning in the
  rightmost preview pane; mask/term parameters as tunable as any field.

## Seam corrections against the current tree

Reconciled against `cleanup/stage-0` @ `59d0924`. The Plan D `MenuState`
extraction moved the field maps, `FieldKey`, `TuningGesture`, and
`pendingIndexedEdit` out of `Intent.h` into a new `src/studio/MenuState.h`, and
shifted the surviving `Intent.h` structs up. Corrected seams so the implementer
trusts them:

| Prior citation | Corrected | Note |
| --- | --- | --- |
| six `MenuState` maps, `Intent.h:78-91` | now in `MenuState.h`: `tuningRanges` (63), `expressionDrafts` (64), `textBuffers` (72), `numberBuffers` (73), `comboMode` (75); two scalars `activeField` (74), `focusField` (76); `FieldKey`/`kNoField` at `MenuState.h:24-25`; `struct MenuState` at `:53` | all seven are re-key targets |
| stackSplit pattern `MenuState.cpp:157-159,198-199`; `SetStackSplit` intent `Intent.h:132` | split preserved across mode reset at `MenuState.cpp:158-160` (`endSession`) and `197-200` (`SetMode`); `struct SetStackSplit` at `Intent.h:61`; clamp in the reducer at `MenuState.cpp:263-264` | |
| `StackPanel.cpp:80-159` (`DrawStackRow`) | `BeginLayerTable` at `79`, `DrawStackRow` at `109-159`, `DrawComposite` at `46-70` | unchanged; table has every per-layer intent |
| `BoardPage.cpp:181-195` (drop `Section`) | `DrawBoard` at `181`, `Section("Board", true)` wrapper at `185` | unchanged |
| `Tuning.cpp:165` | `const auto key = ImGui::GetID("tune")` at `165` | unchanged; finding 1 |
| `Forms.cpp:673` (`ScalarForm`), `TermTemplates.h:41-45` (`TermField`), `Workspace.cpp:604-605` (share statics), `MenuWidgets.cpp:46-47` (`KeyOf`), `ExpressionShelf.cpp:76` | unchanged, exact | |

`KeyOf` at `MenuWidgets.cpp:46-47` (`ImGui::GetID(Literal(a_key))`) is the single
factory most field keys pass through; `Tuning.cpp:165` and `ExpressionShelf.cpp:76`
call `GetID` directly. Re-keying is those three sites plus the `KeyOf` signature.

`ResourceTab` (`kSignals`/`kCurves`/`kSources`/`kMasks`) at `Intent.h:20`,
`kResourceTabs` at `:26`, `ResourceTabName` at `:29`, the `ShowResource` intent
at `:67`, and `MenuState::resource` at `MenuState.h:71` already exist; the
tab-bar infrastructure is half-built and the navigator draws the four as
`NavigatorSection`s today (`Workspace.cpp:159-201`).

## Slice sequence and dependencies

Foundational first: the four shared pieces below unblock several items and close
the widest defect class. `FieldKey` identity (S1) is a prerequisite for stable
ranges, drafts, and term tuning; the generic commit sink (S2) and value-relative
range (S3) build on it; split-on-`Layout` (S4) precedes the navigator restructure.
Structural page/navigator/header changes (S5–S8) are section 3 amendments and
depend only on the foundation. Field, relationship, wizard, and terms work
(S9–S13) layer on top.

```text
S1 FieldKey identity ─┬─ S2 generic commit sink ─┐
                      ├─ S3 value-relative range ── S9 inline openers
                      └───────────────────────────── S13 terms editor
S4 split on Layout ──── S6 output outline
S5 Board page   S6 output outline   S7 resource tabs   S8 header scope bars
S6 ─ S12 "New ..." navigates        S11 input wizard   S10 relationship table
```

| # | Slice | Amendment? | Closes |
| --- | --- | --- | --- |
| S1 | Domain-identity `FieldKey` | no | finding 1 |
| S2 | Generic commit sink for tuning | no | — |
| S3 | Value-relative default slider range | no | — |
| S4 | Split defaults onto `Layout`/intent | no | — |
| S5 | Board as a top-level page | yes | findings 7, 8 |
| S6 | Output outline replaces the layer stack | yes | findings 2, 3, 4, 6 |
| S7 | Resources under a tab bar + per-tab adds | yes | findings 4, 5 |
| S8 | Header/action scope bars | yes | finding 6 |
| S9 | Inline openers, no naked buttons | no | — |
| S10 | Relationship panel as a labelled table | no | — |
| S11 | Input wizard | yes | — |
| S12 | "New ..." combo entries navigate | no | — |
| S13 | Terms editor inline + per-term tuning | no | — |

## Foundational slices

### S1 — Domain-identity `FieldKey`

**What.** Re-key every `MenuState` field map from the ImGui widget id to a domain
identity (recipe id + subject + field; for terms, mask + term index + parameter),
so a field keeps its range, buffers, drafts, and gesture across the wide/narrow
container divergence. **Reuse.** `FieldKey` alias and `kNoField` (`MenuState.h:24-25`);
the five maps and two scalars listed above; `KeyOf` as the single derivation point
(`MenuWidgets.cpp:46-47`). **New.** A domain `FieldKey` derivation (hash of recipe
id + `InspectorKey`-style subject string + field name; `InspectorKey` already
exists at `Workspace.cpp:34-58`) threaded to the three id sites (`KeyOf`,
`Tuning.cpp:165`, `ExpressionShelf.cpp:76`). Keep `FieldKey` a `std::uint32_t` so
the maps are untouched. **Verify.** `menustate_tests`, `fields_tests`,
`widgets_tests` natively; a case that a range set in one container is present
after simulating the other identity. In-game with S3/S9 (the `glossBoost`/
`glossBoost`-style resize repro).

### S2 — Generic commit sink for tuning

**What.** `DrawTuning`/`UpdateTuning` commit only through `gesture.bind →
RecipeEdit` today (`Tuning.cpp:101-103`). Accept a `FormField` plus an `apply`
callback (`std::optional<T>(const std::string&)`) so the same widget drives
term-parameter rebuilds. **Reuse.** The gesture machinery (`BeginGesture`/
`UpdateGesture`/`EndGesture`, `TuningGesture` at `MenuState.h:42`); `TermField`
already pairs a `FormField` with an `apply` (`TermTemplates.h:41-45`). **New.** A
commit-sink parameter on `DrawTuning`/`UpdateTuning` (a variant of "post a recipe
edit" versus "invoke `apply` and rebuild the term"); the recipe path stays the
default. Depends on S1 for a stable key when the sink is a term parameter.
**Verify.** `gesture_tests`, `termtemplates_tests` natively. In-game with S13.

### S3 — Value-relative default slider range

**What.** No field gates its slider behind "Set slider range". `TuningRange`
(`Tuning.cpp:34-67`) falls through to the button today; add a value-relative
fallback `span = max(|v|, eps)`, range `[v - span, v + span]`. The button becomes
*adjust*, reached through the S9 inline opener. **Reuse.** `TuningRange`'s known-
range precedence (`workingRange` then `range` then a stored `tuningRanges` entry).
**New.** The symmetric default and the *adjust* relabel. **Verify.** `fields_tests`
or a small `Tuning` decision test for the span math (extract the range choice as a
pure helper so it is native-testable); in-game with S9.

### S4 — Split defaults onto `Layout`/intent

**What.** The workspace navigator/inspector shares are `static float` locals
(`Workspace.cpp:604-605`), lost on reflow and unclamped. Move them onto the
`Layout` record with an intent, as `stackSplit` already is. **Reuse.** The
`stackSplit` pattern end to end: `Layout` field (`View.h:147`), authored default
in `kLayouts` (`View.h:151-157`), `SetStackSplit` intent (`Intent.h:61`),
preserve-across-mode-reset (`MenuState.cpp:158-160,197-200`), clamp in the
reducer (`MenuState.cpp:263-264`), `Split` call posting the intent
(`StackPanel.cpp:327-332`). **New.** Two
`Layout` fields (`navigatorShare`, `inspectorShare`) and a `SetWorkspaceSplit`
intent (or one intent carrying which split), session-only, nothing to disk.
**Verify.** `menustate_tests` for clamp and reset; in-game resize check with S6.

## Structural slices (section 3 amendments)

### S5 — Board as a top-level page

**What.** Register Board beside the three pages (`Menu.cpp:280-282`,
`AddSectionItem`), drop the `Section("Board")` wrapper (`BoardPage.cpp:185`), and
remove the board from the recipe/overview node so `DrawSubject`'s `RecipeSubject`
arm draws recipe settings only (`Workspace.cpp:434-437`). Board stays geometry-
scoped and absorbs the foreign-composition story the stack's `below`/`above` rows
show. **Reuse.** `DrawBoard`/`DrawBoardPage`, `BuildBoard` (`Menu.cpp:202`), the
board table. **New.** A `RenderBoard` entry point; recipe/overview no longer calls
`DrawBoardPage`. Clears finding 7 by construction (the empty overview was the
board returning silently with no geometry). Ride-along **finding 8**: the
recipe-settings node now also draws `RecipeRow::problems` (dropped rows and the
held-back reason), which today render only on the Recipes page
(`RecipesPage.cpp:152-158`), so a parser-dropped row is visible in Studio.
**Verify.** `board_tests`; in-game that a no-geometry recipe's overview shows
settings, the Board page names why it is empty, and a recipe with a dropped row
shows its problem in Studio.

### S6 — Output outline replaces the layer stack

**What.** The largest slice. The navigator becomes Surface → Output → Layer:
fold "Shell settings" (`Workspace.cpp:122`) into a Shell surface node; enrich each
layer tree row (`Workspace.cpp:142-154`) with the stack's per-layer controls
(`X` remove, `::` drag, `S` solo, `M` mute, type badge, blend combo, shortened
source); draw the per-output `ScalarForm` and replace/selection inline on the
output node (`DrawOutputHeader`, `Workspace.cpp:238`; `ScalarForm`,
`Forms.cpp:673`); make outputs a persistent indent, not `Section`
(`Workspace.cpp:123`). Retire `DrawStackRow` and the stack table
(`StackPanel.cpp:79-159`); the duplicate composite and its cycle-geometry
shortcut (`StackPanel.cpp:46-70`) migrate to the preview header or the Board page.
**Reuse.** Every per-layer intent already exists (`RemoveLayer`, `PickLayer`,
`MoveLayer`, `SoloLayer`, `MuteLayer`, `SetLayerBlend`); the drag/drop payload and
`BlendCombo`/`Badge` widgets; `ScalarForm`; the composite in the rightmost preview
pane (`DrawPreview`, `Workspace.cpp:482`). The row name field and remove button
that findings 2 and 3 report unreachable (`RowNameField`, `RemoveButton`, still
only in `ResourcePanels.cpp`) move onto the tree rows and inspectors here.
**New.** The enriched tree-row renderer; the output-node inline layout; deletion
of `DrawStackRow`. `ResourcePanels.cpp` can be removed once its name/remove
behaviours land (finding 2/3 owner). Ride-along **finding 4**: an add
(layer/output) selects its new row — reuse S12's create-then-`Navigate` so the
follow-up subject lands on the new index instead of leaving `pendingIndexedEdit`
unselected (`AddLayer`, `StackPanel.cpp:161-166`; `pendingIndexedEdit`,
`MenuState.h:60`). Ride-along **finding 6**: the newly reachable rename lands
correct — fresh text on open, the store's refusal surfaced, and the selection
following the renamed id (the same correctness S8 applies to the recipe-rename
popup). Depends on S4 (share is on `Layout`). **Verify.** `panels_tests`,
`resolveoutput_tests`, `selection_tests` for the reorder/delete invalidation;
in-game the full workspace acceptance (rename and remove a resource, reorder a
layer, solo/mute from the tree, add-a-row-selects-it, rename-follows-new-id).

### S7 — Resources under a tab bar with per-tab adds

**What.** Replace the four `NavigatorSection` resource blocks
(`Workspace.cpp:159-201`) with a tab bar over Signals / Curves / Sources / Masks;
under an active search filter, flatten to matches (the filter path already dims
titles). Split the combined add menu into per-tab add buttons
(`DrawResourceAddMenu` call at `Workspace.cpp:117`; `DrawOutputAddMenu` at
`87-112`). **Reuse.** `ResourceTab` (`Intent.h:20`), `kResourceTabs` (`:26`),
`ResourceTabName` (`:29`), `ShowResource` (`:67`), `MenuState::resource`
(`MenuState.h:71`) — already present;
the existing per-resource creators behind `DrawResourceAddMenu`. **New.** Draw the
tab bar; route each tab's add button to the matching creator. Ride-along
**finding 4** for resources: a per-tab add selects its new row (same
create-then-`Navigate`). Ride-along **finding 5** (still live after source-kinds):
`Edit(Recipe&, const SetSource&)` (`Edits.cpp:642`) runs
`CheckSource(rows, Source{name, kind})` (`Signals.cpp:775`) and returns the first
error as a `Refusal`, so a default image/ripple/distance kind — the blank
`DefaultSourceKind` record the kind chooser posts (`Forms.cpp:349`,
`BindSourceKindChoice`) — is still refused (image `'path' is empty`, distance
`'distance' needs a node name or a point`, ripple's trigger/scalar checks;
`Signals.cpp:782-810`). The fix: store `source->kind` and admit those as row
problems instead of refusing the edit. Source-kinds (Plan B) reshaped the model
(the variant `SourceKind` now travels on `SetSource`) but left this refusal in
place. An independent engine-free fix, native-tested, carried here because it is
source editing and predates the wave.
**Verify.** `menustate_tests` for tab selection; `edits_tests` for the `SetSource`
kind change; in-game that each tab adds only its kind, search flattens, and a
source's kind can change.

### S8 — Header/action scope bars

**What.** Group the header into three labelled bars. Session: wearer, armor, view,
global clock, Return to live (`SelectionCombo`/`RecipeCombo`,
`ContextRows.cpp:51-89`; `DrawTryStatus`'s Return to live, `StudioPage.cpp:128-147`).
Recipe: New, Save, Revert, Undo, Redo, Rename (`DrawRecipeHeader`/`DrawStudioContext`,
`ContextRows.cpp:475-505,581-601`; Save/Revert, `RecipeActions.cpp:71-115`).
Audition: Solo, Mute, Fire — move `SoloRecipe`/mute out of the recipe group
(`IsolateCheckbox`, `ContextRows.cpp:90-107`) into the audition bar with the Try
controls. **Reuse.** All actions and intents exist; this is regrouping and
labelling. **New.** Three labelled containers; move the solo/mute draw site.
Ride-along **finding 6**: while the recipe-rename button is regrouped, fix its
popup (`ContextRows.cpp:453-472`) — it seeds stale text, hides the store's
refusal, and the selection does not follow the new id.
**Verify.** No native decision changes; in-game that scope labels read correctly,
audition controls sit apart from recipe actions, and a recipe rename shows a
refusal and follows the new id.

## Field, relationship, wizard, and terms slices

### S9 — Inline openers, no naked buttons below a field

**What.** Frequent controls sit inline on the field row; occasional ones become an
inline opener plus an anchored popup (the color-picker pattern). `DrawTuning` and
`DrawExpressionShelf` render below the row today (`FormDraw.cpp:279-280,333-334`).
Keep the slider inline with a smaller exact-entry box; collapse range-*adjust*
(from S3) and expression literal editing/promotion behind the inline opener; drop
the per-field "Connect input" button (reach connect through the reference combo,
S11/S12). **Reuse.** The anchored-popup convention already chosen for special
inputs; `DrawExpressionShelf`'s draft machinery (`ExpressionShelf.cpp:69-`).
**New.** Inline opener placement; move the range-set popup off the naked
`SmallButton` (`Tuning.cpp:46`). Depends on S3. **Verify.** `fieldcheck_tests`,
`expressionrename_tests` unchanged; in-game the field-row layout at both sizes.

### S10 — Relationship panel as a labelled table

**What.** Replace the `Dim` + `Follow` lists (`DrawRelationships`,
`RelationshipPanel.cpp:87-124`) with two tables: Driven-by `| Property | Driver |
Value |`, Used-by `| Consumer | Property | Component | Value |`, follow-links on
cells, value column populated where live state resolves it and blank otherwise.
**Reuse.** `recipe->relationships` and the `SubjectOf`/`Follow`/`OwnerName`
resolvers already used here; the driver/consumer traversal (`PropertyLocation`,
`panels_tests`). **New.** Table layout and a value lookup from live state (blank
when absent). **Verify.** `panels_tests` for the traversal; in-game follow-links
and value population.

### S11 — Input wizard

**What.** A guided, stepped presentation over the existing engine-free connection
logic: choose driver → pick an actor value from the searchable catalog → choose
mapping and parameters (guard zero denominators) → confirm. Advanced swaps to the
flat catalog for one-click connects. Two entry points: a standalone button beside
"add signal" in the Signals tab (create-and-name only) and "New input" at the
bottom of a connectable field's reference combo (create-and-bind). **Reuse.**
`ConnectionBuilder`/`Response` (`InputConnections.cpp:29-76`) — the
`kMeasure`/`kFraction`/`kExhausted`/`kHitResponse` logic is done and native-tested;
`DrawInputBrowser`'s catalog and `ConnectButton` (`InputBrowser.cpp:96-118`) become
the Advanced table; `CanConnectInput` (`InputConnections.cpp:79`). **New.** The
stepped presentation and its two openers; the field-combo "New input" entry.
Engine-free connection logic is not touched. Section 3 amendment (new pane/flow).
**Verify.** `inputconnections_tests` unchanged; in-game connect stamina exhaustion,
a fraction, and a hit response through both entry points.

### S12 — "New ..." combo entries navigate to the authoring surface

**What.** After create-and-bind, `Navigate` the inspector to the new resource's
subject. New mask jumps straight into `EditMaskAsTerms`, skipping the mask-
inspector step; New input is the S11 wizard. **Reuse.** The creator branch in
`PostField` (`FormDraw.cpp:246-248`); the reference-follow resolver
(`DrawLayerFields`, `Workspace.cpp:267-323`); `EditMaskAsTerms` (`PaintPanel.cpp:579`,
already invoked from `Workspace.cpp:350,411`); `Navigate` (`Workspace.cpp:316-322`).
**New.** After a successful create edit, resolve the new subject and `Navigate`.
Depends on S6/S7 for the navigation targets. **Verify.** `navigation_tests`,
`navigationintegration_tests`, `edits_tests` for the create batch; in-game that
"New mask" lands in the terms editor and "New source" opens its inspector.

### S13 — Terms editor: inline offers + per-term tuning in the preview pane

**What.** Each pattern-chooser offer row gets its own inline preview and add
button; drop the expanding selected-offer detail section (`DrawPatternDetails`,
`PatternChooser.cpp:129-169`). The terms table stays the place to tune a placed
term ("..." settings). Tune a selected placed term in the rightmost pane beside
the composite, against the live preview. Route term parameters through the tuned
pipeline (sliders, value-relative ranges, exact entry) instead of the bare
`FieldInput` used today (`DrawPatternControls`, `PatternChooser.cpp:68-88`).
**Reuse.** `TermField` already wraps a full `FormField` plus `apply`
(`TermTemplates.h:41-45`) — routing, not a parallel schema; `BuildTerm`/`AddTerm`;
`DrawMaskStack`'s offer/terms tables (`PaintPanel.cpp:528-565`); the rightmost
preview pane (`DrawPreview`, `Workspace.cpp:482`). **New.** Per-offer inline row
controls; route `DrawPatternControls` through the S2 generic commit sink with S1
domain keys (mask + term index + parameter); the contextual per-term tuning role
for the preview pane. This role is mode-specific and distinct from inline layer/
output tuning; intentional unless it later generalizes. Depends on S1, S2, and
S6 (preview pane). **Verify.** `termtemplates_tests`, `mask_tests`,
`paintsession_tests`, `suspendedpaint_tests`; in-game add a term with defaults,
then tune a placed term against the live preview, Keep, save, reload.

## Verification summary

- **Native, per slice:** `menustate_tests` (S1, S4, S7), `gesture_tests` (S2),
  `fields_tests`/`fieldcheck_tests` (S1, S3, S9), `board_tests` (S5),
  `panels_tests`/`resolveoutput_tests`/`selection_tests` (S6, S10),
  `navigation_tests`/`navigationintegration_tests` (S12),
  `inputconnections_tests` (S11), `termtemplates_tests`/`mask_tests`/
  `paintsession_tests`/`suspendedpaint_tests` (S2, S13). Extract the S3 span math
  as a pure helper so it is native-testable. Run through `tests/run-native.sh`.
- **DLL build + in-game:** every slice that changes drawing (S5–S13) needs a
  `./build.sh Release -j 4` and an in-game pass; batch them into the complete-
  editor checkpoint rather than one pass per slice. S1–S4 are checkable natively
  first; S1/S3/S9 share the resize/range in-game repro.
- **Gate:** `tools/gate.sh` (format, native suite, clang-tidy baseline, layers,
  no-comment check) before each commit; new files update the compile DB and the
  `CMakeLists.txt` source lists.
- Intermediate UI usability is not a gate (plan amendment). Keep native decisions
  green throughout; do not defer all integration feedback to the end.

## Cross-reference to core-checkpoint findings

- **Finding 1** (custom slider ranges keyed by layout) — closed by **S1**.
- **Findings 2, 3** (row rename/remove unreachable) — closed by **S6** moving the
  name field and remove button onto the tree rows and inspectors; delete
  `ResourcePanels.cpp` once done.
- **Finding 7** (blank overview for a no-geometry recipe) — closed by **S5**.
- **Finding 4** (add does not select the new row) — closed by **S6** (layers,
  outputs) and **S7** (resources), reusing S12's create-then-`Navigate`.
- **Finding 5** (`SetSource` refuses a kind change) — closed by **S7** as an
  independent `Edits.cpp:642` fix carried with source editing.
- **Finding 6** (rename popup: stale text, hidden refusal, selection does not
  follow) — closed by **S6** for resource rename and **S8** for the recipe-rename
  popup.
- **Finding 8** (dropped row invisible in Studio) — closed by **S5** drawing
  `RecipeRow::problems` in the recipe-settings node.

All eight core-checkpoint findings are folded into slices; none remain out of scope.
