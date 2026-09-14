# UI v2 implementation plan

Status: core editor integrated, validation in progress. The early framework
checkpoint was accepted on 2026-09-13. See the [core checkpoint](wip/ui-v2-core-checkpoint-2026-09-13.md).

Design baseline: [frozen UI v2 proposal](ui-v2-proposal.md).
Evidence: [current UI assessment](ui-assessment-2026-09-13.md).

## 1. Scope and working rules

Deliver stages 1-3 as one coordinated editor replacement. Intermediate work need
not leave a usable in-game UI. Keep changes small enough to review and test, but
avoid temporary adapters, duplicate navigation, and repeated old/new UI wiring.
Do not wait for new patterns or overlays to finish the core editor.

- Preserve recipe format 1, validation, matching, and runtime ownership contracts.
- Keep engine-free decisions in `studio/` or `recipe/`; drawing stays in `menu/`.
- Use existing field bindings, edit batches, snapshots, and application reporting.
- Preserve useful modals for short tasks; use inspector navigation for exploration.
- Keyword spoofing, shader mapping/blending, and enchantment-context experiments
  are deferred. Existing recipe keys and EFSH editing stay available.
- Do not fold unrelated cleanup or incremental runtime rebuilding into UI work.

The cleanup pass changes shared menu/runtime files. Before production edits,
record its resulting revision and working-tree fingerprint, inspect remaining
changes, and establish a build/native-test baseline. Do not reset or overwrite
another agent's work. The cleanup checkpoint reports successful local checks but
pending in-game ownership/preview acceptance; preserve that distinction.

## 2. Delivery map

```text
0 Baseline
    |
Core replacement: stages 1-3
    Shared selection, document, property, and command contracts
    Workspace shell + early framework check
    Inspectors, tuning, expression tools, mask tasks
    Complete-editor acceptance
    |
4 Scoped audition and armor overlays
    |
5 Additional patterns and coordinated peaks
```

Stage numbers 1-3 below group the work; they are not separate releases or usability
gates. Each row remains a bounded implementation slice with recorded evidence.
Stages 4-5 remain separate runtime/capability work.

### Core implementation order

1. Define shared contracts together: document/subject selection (1B/1F), property
   descriptions and relationships (2A/2B), gesture protocol (2C), and suspended
   mask-draft lifecycle (3E). Audit expression operand support for 3C here.
2. Build the workspace shell (1C/1D) with representative real fields, a retained
   preview, and Back navigation. Run the early framework checkpoint below.
3. Connect recipe actions and the existing Try controls (1A/1E), then sliders and
   relationship views (2D/2E) against the shared contracts.
4. Complete the input browser/helpers, expression and response editors, mask task,
   and supported pattern chooser (3A-3F). Their reducers and data builders can be
   developed before their final drawing code.
5. Remove superseded page/mode/modal routes, reconcile source lists, and run the
   complete-editor checkpoint. Retain reusable controls and engine services.

Keep a recorded baseline for comparison. Do not maintain two operational editors
or build a compatibility layer solely to make intermediate changes deployable.

### Early framework checkpoint

Use a deliberately incomplete but buildable workspace to verify resizing,
scrolling, keyboard focus, short-task modals, Back, texture submission/lifetime,
and game visibility. Exercise a representative existing edit and its snapshot
round trip. Check narrow/wide sizes and UI scaling. Missing tools are expected;
this checks framework assumptions before all inspectors depend on them.

## 3. Stage 0: establish the integration baseline

| Slice | Work | Completion evidence |
| --- | --- | --- |
| 0A | Record source revision, outstanding cleanup changes, framework capabilities, and baseline checks. | Exact tested build identity and known failures recorded. |
| 0B | Capture current Studio/Recipes/Paint flows at narrow and wide sizes. Record page changes, selection behavior, preview state, and save behavior. | User-run in-game baseline; no visual pass inferred from logs. |

Read the [cleanup checkpoint](wip/cleanup-checkpoint-2026-09-13.md) before touching
previews. Reuse its retained texture/submission contract; pinning a preview must
not keep an unowned raw texture pointer after its snapshot expires.

### Existing-code reuse audit

Reviewed against the active `src/` tree on 2026-09-13. The core replacement replaces
UI organization, not the editor foundations. Extend existing owners in place;
extract helpers only where a second consumer needs them. Do not add a parallel
field schema, recipe editor, history stack, Paint protocol, or rendering scheduler.

| Area | Already implemented | Actual remaining work |
| --- | --- | --- |
| Fields | `Forms.h` has kinds/type rules, choices, values, detail kinds, bindings, creators, and optional ranges. `Forms.cpp` builds layer/source/signal/light/shell forms. | Add semantic units/range distinctions and target identity; reuse the forms in new inspectors. |
| Resource creation | `Forms.cpp::CreateValue` already promotes whole scalar/color fields, creates constants/expressions, and binds their references. `CreateImage` and curve/mask creators do equivalent resource setup. | Surface existing creators clearly; extend with input/response helpers and promotion of one expression operand. |
| Validation and batches | `FormDraw::PostField` submits creator batches; `PrepareEdits` preflights recipes; `RecipeEditor::ApplyEdits` rejects errors/no-ops before rebuilding. | Reuse this path for Connect and promotion; add operation results where the UI cannot yet observe refusal. |
| History and scheduling | `History<Recipe>`, `History<MaskStack>`, `RecipeEditor`, `SessionQueue`, and `ApplicationService` already own undo and queued application. | Add gesture grouping/coalescing and edit-target revision checks. Keep the history container and queue. Application revisions are not automatically document revisions. |
| File actions | `RecipeEditor::SaveRecipe`, `RevertRecipe`, Undo/Redo, and `RecipesPage::DrawRecipeFile` already implement actions; recipe rows carry dirty/history state. | Move/share controls. Publish file-operation success/error instead of relying on logged save failures. |
| Selection | `Selection`, `ResolveSelection`, pick intents, and `MenuState` already track live subject and editor state. | Extend to resource/property targets, Back, and independent document selection; do not create a second competing selection store. |
| Document projection | `BuildRecipeRow` already projects recipe fields, resources, outputs, diagnostics, and history without taking geometry. | Publish selected unmatched documents; build signal definitions without requiring live `SignalState`. Its current signal loop requires both graph and live state. |
| Relationships | `Edits.cpp` already walks signal/image/curve references for counting and rename; `Program` exposes references/curves. Signal details and Paint terms already drill into reads. | Enrich traversal with owner/property locations for consumers; reuse it for counts and navigation rather than maintaining another exhaustive recipe walker. |
| Expression editing | `Program::Parse/Check/Evaluate`, compiled operations, and `RenameInExpression` exist. | Public source spans/operand occurrence identity do not exist in the inspected header. Add minimal parser-backed support; whole-field promotion already exists. |
| Paint transactions | `PaintSession`, `PaintUpdateRequest/Result`, `PaintCommitRequest/Result`, `PendingPaintUpdate`, and acknowledgment reducers already handle IDs, revisions, pending work, errors, and stale replies. | Preserve this protocol; add suspended navigation, destination/assignment semantics, and resume behavior. Do not rebuild scratch acknowledgment. |
| Mask/pattern assembly | `TermForm`, `OffersOf/OffersOfRecipe`, `BuildTerm`, `MaterialiseTerm`, presets, and `SourcePlanBuilder::ReuseOrAdd` already build offers and source edits. | Re-present as a chooser, add previews/assignment as needed, reuse naming/remapping/source reuse. |
| Layout widgets | `Table`, width specs, `Split`, `ChooserRow`, `RuleWithFilter`, `DetailModal`, drag/drop, and solo/mute controls exist. `TextField` preserves drafts and shows validation errors. | Compose new pane layout and named actions from them; extend slider gestures without replacing text validation/focus handling. |
| Preview and coverage | `Thumbnail/ThumbnailButton`, retained preview submission, board/composite images, and geometry bones/partitions/islands/clusters already exist. | Add pin ownership, linked legend/identity, and actual armor overlays. No new thumbnail pipeline or duplicate mesh analysis. |
| Live inputs and Try | `ActorEnvironment::ActorValue` resolves names and supported measures; signals publish values. Fire and clock/isolation commands already work. | Add browsable descriptions/live samples for unconnected inputs and scoped general holds; retain existing evaluation rules. |

Evidence: [Forms.cpp](../src/studio/Forms.cpp),
[FormDraw.cpp](../src/menu/FormDraw.cpp), [Edits.cpp](../src/studio/Edits.cpp),
[RecipeEditor.cpp](../src/engine/RecipeEditor.cpp),
[RecipeSnapshot.cpp](../src/studio/RecipeSnapshot.cpp),
[MenuState.cpp](../src/studio/MenuState.cpp),
[Expression.h](../src/recipe/Expression.h),
[TermTemplates.h](../src/studio/TermTemplates.h),
[SourcePlan.h](../src/studio/SourcePlan.h),
[MenuWidgets.h](../src/menu/MenuWidgets.h),
[Environment.cpp](../src/engine/Environment.cpp).

## 4. Stage 1: connected workspace

### Implementation slices

| Slice | Change | Main existing seams |
| --- | --- | --- |
| 1A | Add recipe Save, dirty state, Undo/Redo, scoped actions, and file-operation results to Studio. Share behavior with Recipes. | `ContextRows`, `RecipesPage`, `RecipeEditor`, snapshots/intents |
| 1B | Extend selection to recipe settings, outputs, layers, resources, and properties. Add Back history with saved scroll/context. | `Selection`, `Intent`, `MenuState`, `Frame` |
| 1C | Build output/resource navigator and central inspector. Reuse stack rows, fields, source/mask previews, and the output board. | `StudioPage`, `BoardPage`, `StackPanel`, `ResourcePanels`, `FormDraw` |
| 1D | Add resizable preview column, narrow-window drawers, and named short-task modals. Remove fixed resource-bottom allocation. | `View::Layout`, `MenuWidgets`, `StudioPage` |
| 1E | Collect existing Fire, solo/mute, freeze/step/scrub/speed controls and active state in the Try bar. Label current scope accurately. | `View`, `FormDraw::FirePopup`, `StudioPage::DrawFooter`, `Menu::Dispatch` |
| 1F | Publish an independently selected document using `BuildRecipeRow`; extend its signal projection for no live state. Keep applied geometry facts optional. | `RecipeSnapshot`, `Snapshot`, `Selection`, `RecipeStore`, `ManagerSnapshot` |

For 1B, introduce a typed editor subject and resolver rather than adding a separate
selection mechanism for each new panel. Existing index-based layer references
must either follow supported edits or become visibly invalid, never silently
select a different layer after reorder/deletion.

Do not promise subject-local clocks in 1E while the runtime still applies a
global view. Show actual scope; Stage 4 supplies local audition semantics.
Connect Paint after the shared draft-lifetime contract is explicit; no temporary
Compose/Paint navigation bridge is required.

### Workspace acceptance cases

Run these at complete-editor acceptance, not as an intermediate release gate.

Create and select a recipe, edit/reorder a layer, inspect a resource, return with
Back, save, and reload it. Inspect an unmatched file without a fabricated preview.
Test empty/no-geometry states and save failure. Verify navigation alone leaves
armor appearance unchanged. Inspect layout at two sizes/scales and ensure active
preview state stays visible. Keep existing regression failures separately labeled.

## 5. Stage 2: direct tuning and relationship navigation

| Slice | Change | Main existing seams |
| --- | --- | --- |
| 2A | Extend field/property descriptions with units, hard limits, working range, and supported operations. Preserve unknown ranges explicitly. | `Forms`, `Fields`, `FieldParsing`, `FieldCheck` |
| 2B | Extend existing reference traversal with owning property locations; derive named consumers and driver navigation from that traversal. | `Edits::CountReferences`, existing reference visitors, `Expression`, `Names`, `Panels` |
| 2C | Add begin/update/commit/cancel grouping around existing history and edit application; use existing queues and reporting, extending result correlation as needed. | `RecipeEditor`, `History`, application service, intents/snapshots |
| 2D | Draw numeric sliders plus exact input from metadata; retain colors, vectors, and reference controls. | `MenuWidgets::ValueWidget`, `FormDraw` |
| 2E | Render driver strip and used-by inspector; follow links through the Stage 1 subject resolver. | `Panels`, `FormDraw`, inspector navigation |

### Gesture contract

```text
Begin: capture subject, recipe revision, and original value
Update: validate latest draft; submit bounded/coalesced live changes
Commit: finish with one history entry for the whole gesture
Cancel: restore original value through the same application path
Result: display acknowledged revision; ignore stale UI acknowledgments
```

Choose the concrete command protocol in 2C before wiring sliders. Do not route
every frame through ordinary history-pushing edits. Existing full actor rebuilds
remain the application policy; measure cost and coalesce requests rather than
introducing an unrelated incremental renderer. A refused update retains the last
valid result and draft error. Save/Undo, subject changes, load, and deletion must
finish or cancel the active gesture deterministically. External conflicting edits
must refuse/cancel safely rather than restore an outdated whole recipe.

### Tuning acceptance cases

Test pure gesture/relationship decisions as they land. Run the full in-game flow
at complete-editor acceptance.

Tune source and output values with sliders and exact entry. Undo one long drag
in one step; cancel another. Exercise delayed updates, rejected input, reorder,
deletion, and load during a gesture. Verify referenced properties remain driven
until explicitly disconnected. Follow both driver and consumer links and inspect
the effect of editing a shared signal.

## 6. Stage 3: connected authoring tools

| Slice | Change | Dependency / boundary |
| --- | --- | --- |
| 3A | Searchable actor-value/input catalog with supported measurements, descriptions, units, and live samples. | Engine supplies supported identities/live reads; pure catalog drives UI. No guessed universal ranges. |
| 3B | Extend current field creators with fraction/exhaustion/hit helpers and input-browser presentation. Reuse create-and-bind edit batches. | Existing signal/curve types and `CreateValue`; validate zero denominators and event/condition distinction. |
| 3C | Re-present existing referenced-signal details as an input shelf; add literal-occurrence tuning/promotion using existing constant creation semantics. | New parser-derived occurrence identity and revision; preserve formula meaning and untouched text. |
| 3D | Response graphs for recognized configurations, with live markers and explicit axes. | Keep arbitrary formulas editable; recognize supported structure before offering graph edits. |
| 3E | Move mask task into inspector; retain draft, destination, history, preview acknowledgments, Keep/Discard, and Resume. | `PaintSession`, `PaintCommit`, `Mask`, `TermTemplates`, `PaintPanel` |
| 3F | Supported pattern chooser with previews and meaningful controls; expose construction and assignment. | Existing sources/expressions/presets only; no implied flame/lightning support. |

The inspected `Expression.h` exposes references, curves, and compiled operations,
but no public tokenizer or editable source spans. Add source-location support to
the existing parser only as required; do not build a separate expression parser
in the menu. Promotion creates a constant and replaces exactly one selected
occurrence in one validated batch. Identical literals elsewhere stay untouched.

For 3E, leaving the mask inspector suspends its draft; it must not accidentally
run today's mode-exit cancellation. Keep commits only after acknowledgment.
Retain the existing session/revision/request checks and pending-update coalescing.
Save excludes the draft with a clear label. Define load, recipe deletion/rename,
subject change, and shared-mask behavior in the reducer before changing the page.

### Authoring acceptance cases

Build stamina-exhaustion glow through input discovery. Fire a hit response. Tune
an expression literal, promote it, and verify identical output before/after.
Edit a mask, visit another inspector, resume, Keep, save, and reload. Verify
unsupported graph/pattern cases retain working text controls. A supported moving
band must show its coordinate space and direction before animation is connected.

### Complete-editor checkpoint

Run all workspace, tuning, and authoring cases above as one integration batch.
The first complete UI must include recipe persistence, unmatched-document editing,
navigation, sliders/history, input connections, expression tools, mask draft
resumption, and existing preview controls. Check no old page route is required to
finish a task. Recheck preview ownership and navigation-only appearance stability.
Record missing runtime capabilities as Stage 4/5 work, not unfinished core tools.

## 7. Stage 4: scoped audition and armor inspection

| Slice | Change | Completion condition |
| --- | --- | --- |
| 4A | Define scoped audition state and begin/release lifecycle for existing view controls and temporary values. | Explicit wearer/piece/recipe scope, visible active state, deterministic restoration. |
| 4B | Add actor-input and property holds, test values, and Return to live. | Affects plugin evaluation only; never writes actor stats or persists into recipes. |
| 4C | Add region identity and linked legend using geometry/coverage first, then inferred clusters. | Named basis, stable identity for an analysis, explicit reanalysis invalidation. |
| 4D | Add temporary overlay/highlight rendering and legal region-to-mask/selector actions. | Reuses ownership/retirement rules; restores without overwriting another system's changes. |

Specify actor-input versus downstream-property override semantics before 4B.
An override must affect only its intended evaluation context even when several
pieces share recipes. Hold values have explicit working ranges; maximum is not
invented for unbounded properties. Return to live and Studio close release all
audition-owned state; switching subjects cannot transfer an override accidentally.

Region overlays require an engine/render design review. Reuse preview and binding
ownership rather than writing materials directly from menu code. Keep authored
edits distinct from overlay changes. Direct armor picking is outside this stage.

### Checkpoint 4

Hold stamina at zero and glow at a chosen peak, then release them. Repeat with two
actors/pieces sharing a recipe. Verify close, subject switch, unequip, reload,
and failures restore the intended live state. Highlight geometry and cluster
regions, build a supported mask, then disable the overlay. Confirm preview
texture ownership and external material takeover behavior remain correct.

## 8. Stage 5: optional extensions within the frozen scope

Prove spatial noise/flame/lightning patterns individually before adding them to
the chooser. For each, document coordinate space, tunable parameters, rendering
cost, and source/format compatibility. This is capability work, not a UI-only task.

Add coordinated peak holds only with an explicit group of properties/test values.
Do not infer a peak from arbitrary expressions. Freehand paths, armor picking,
and deferred enchantment appearance experiments are not implementation tasks here.

## 9. Validation and handoff

- Use existing native suites for changed decisions: selection/reducers, fields,
  edits/history, expression handling, mask/paint sessions, and application queues.
- Test behavior boundaries: stale targets, one-gesture undo, rejected edits,
  atomic helper creation, formula equivalence, and restoration. Do not add tests
  that merely mirror widget drawing.
- Follow repository build/check requirements inside `nix develop`: native suite,
  Release build with at most four jobs, formatting and relevant lint/tidy checks.
  Inspect current script usage before execution. Update source lists/compile DB
  when files are introduced.
- Record exact results and build identity once per completed slice. Run broader
  checks when changed scope or failures justify them.
- Intermediate UI completeness is not required. Keep native decisions checked
  throughout and compile changed adapters at coherent boundaries; do not defer
  all compiler or integration feedback until the full editor is drawn.
- For the core replacement, use the early framework and complete-editor game
  checkpoints. Add a focused check only if a new engine risk or failure warrants
  it. Stages 4-5 retain their own runtime acceptance checks. Install
  and give the user concise in-game steps and expected observations. Stop for the
  user's game run as required by repository instructions.
- Record actual build, install, and game-test status in the linked checkpoint;
  implementation status alone does not imply in-game acceptance.

## 10. Tracking and next action

Stages 1-3 are integrated for complete-editor validation. The frozen proposal is
unchanged. Runtime audition/overlays and new procedural capabilities remain later
work, following the user's game check.

| Work | Current state |
| --- | --- |
| Workspace | Searchable navigator, resizable panes, Back/property links, current-snapshot preview pinning. Early framework accepted. |
| Recipe actions | Shared Save/Revert/results; standalone creation and unmatched-document editing; no implicit apply when browsing. |
| Navigation | Typed subjects/property locations; indexed invalidation, pending gates, exact light/output selection. |
| Tuning | Scalar sliders, exact input, custom working ranges, coalesced gestures, one-step undo/cancel, revision guards. |
| Relationships | Driver/consumer links derive from the existing reference traversal. |
| Expressions | Parser-backed number occurrence editing/promotion; exact edits reject stale document revisions. |
| Inputs | Searchable supported actor values with live measures; direct/fraction/exhaustion/hit connection batches. |
| Responses | Graphs for recognized pulse/ramp/trigger definitions and simple curves; other formulas retain text editing. |
| Masks/patterns | Suspended draft, Resume/Keep/Discard, atomic layer assignment, supported pattern chooser using existing sources. |
| Source previews | Image thumbnails apply scroll/tiling/orientation through the existing sampling/render path. |
| Runtime additions | Scoped holds, armor overlays, new patterns, and coordinated peaks remain pending (stages 4-5). |

Source previews show the sampled source, before the layer's color, opacity,
normalization, and blend. They share the recipe clock; Freeze/Step and differently
sampled uses of the same image are part of the core game check.

The implementation retains useful Recipes/Setup pages and short-task modals.
Studio uses the workspace for mask tasks; no separate mode switch is required.


Plan amendment: at the user's request, intermediate UI usability is no longer a
requirement. The frozen design scope is unchanged; this changes implementation
ordering and checkpoint frequency only.

Record implementation decisions below without rewriting the frozen proposal.
Changes to user-visible scope need an explicit design amendment here.

| Decision | Status |
| --- | --- |
| Exact property-address identity and invalidation scheme | Typed subjects and property locations implemented; positional edits invalidate and gate until matching result. |
| Gesture scheduling/history protocol | Coalesced begin/update/commit/cancel; one history entry, revision checks, UI heartbeat and abandoned-gesture completion. |
| Parser support for editable operand occurrences | Existing parser now exposes numeric occurrence spans; replacement preserves other occurrences and grouping. |
| Suspended Paint draft lifecycle | Draft/session/history survive navigation; atomic Keep checks destination revision. |
| Subject-local preview and overlay ownership | Resolve before 4A/4D. |

Reuse audit outcome: no additional implementation stage is needed. Several tasks
are presentation or targeted extensions of existing services. The shared-contract
review should specify only the missing identities, metadata, and lifecycle rules;
it is not authorization to redesign all editor infrastructure.
