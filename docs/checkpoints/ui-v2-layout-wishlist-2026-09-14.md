Status: record. The in-game feedback wishlist from the complete-editor build;
the implementation plan below sequenced it.

# UI v2 layout wishlist — 2026-09-14

In-game feedback from the complete-editor build, captured as it stands. This is a
working list; more is coming. When it settles it folds into the section 3
amendment of `../ui-v2-implementation-plan.md` plus a light implementation plan.
Each item names the code seam so the later plan is mechanical.

## Framing decisions (settled)

- The workspace stays **imperative**, not data-driven. A layout trial is a code
  edit, not a data edit.
- Both the **wide and narrow** paths stay. Not collapsed to one. The bug risk
  from having two is narrower than it looked (see mechanics below).
- **Structural** layout changes (which pane holds what, collapse order, a new
  page) land as a section 3 amendment. Button and within-pane ordering are free
  edits, noted in the plan's decision table only if they diverge from the mock.

## Mechanics to carry into the plan (settled)

| Item | Decision | Seam |
| --- | --- | --- |
| `FieldKey` identity | Re-key from `ImGui::GetID` to domain identity (recipe id + subject + field). Closes the container-relative divergence class for all six `MenuState` maps and fixes finding 1. | `Tuning.cpp:165`, `ExpressionShelf.cpp:76`, `MenuWidgets.cpp:47`; `Intent.h:78-91` |
| Split defaults | Move the workspace navigator/inspector shares onto the `Layout`/intent pattern `stackSplit` already uses: authored default in `Layout`, live value in `state.layout`, an intent with clamp, resettable. Session-only; nothing written to disk. | `View.h:138-150`, `MenuState.cpp:132-134,238`, `Workspace.cpp:604-605` |
| Cross-breakpoint scroll | Not restored. Pixel scroll is meaningless after the inspector reflows at a different width (~530px wide vs ~1000px narrow). The only correct systemic form is widget-anchored `SetScrollHereY`; left unbuilt until it's a felt problem. | `Workspace.cpp:573-574,587-588` |
| Transient ImGui state | Focus, open popups, and combo-open state reset on a resize across 1000px. Accepted; not worth reimplementing ImGui's state for a rare event. | — |

## Page structure

- **Board becomes its own top-level page**, beside `[Studio] [Recipes] [Setup]`,
  and stays the geometry-scoped composition view. This makes Studio explicitly
  recipe-specific. Not collapsible. The Board absorbs the cross-recipe (foreign)
  composition story that the layer stack's `below` rows show today. The
  recipe/overview navigator node drops the board and becomes recipe settings
  only, which also clears finding 7 (blank overview for a no-geometry recipe).
  Seams: `BoardPage.cpp:181-195` (drop the `Section` wrapper), `Workspace.cpp:436`.

## Navigator

The navigator becomes one persistent outline plus a tabbed resource area.

- **Output outline replaces the layer stack.** The stack table is dropped; its
  per-layer controls move onto the tree layer rows: `X` remove, `::` drag/reorder,
  `S` solo, `M` mute, type badge, blend combo, source name (shorten the source
  presentation). Every intent already exists.
  Seams: enrich `Workspace.cpp:142-154`; retire `StackPanel.cpp:80-159` (`DrawStackRow`).
- **Surface nesting: Surface → Output → Layer.** Add a Material/Shell level so
  surface settings attach one-to-one at the Surface node, folding the orphaned
  top-level "Shell settings" row into the Shell node.
  Seams: `Workspace.cpp:94,122,128`.
- **Output settings inline with the output topline.** The per-output `ScalarForm`
  (level, color, roughness, weight, densityRandomization) plus replace/selection
  move onto the output node.
  Seams: `Forms.cpp:673` (`ScalarForm`), `Workspace.cpp:238` (`DrawOutputHeader`).
- **Outputs are not collapsible.** The outline is a persistent indent, not
  `Section` headers. Trades vertical space for a stable structure; accepted.
  Seams: `Workspace.cpp:79-84,123`.
- **Resources move under a tab bar.** Sources/Masks/Signals/Curves become tabs,
  not four collapsible sections. Under an active search filter, flatten to
  matches (the filter path already replaces sections with dimmed titles).
  Seams: `Workspace.cpp:159-201`.
- **Composite:** already shown in the rightmost preview pane
  (`Workspace.cpp:482`, `PreviewOutput`). The stack's duplicate composite goes;
  its click-to-cycle-geometry shortcut migrates to the preview header or the
  Board page. Seams: `StackPanel.cpp:48-65`.

Finding cross-reference: the tree rows and output nodes carrying remove/rename
controls close findings 2 and 3 (rename/remove unreachable). Findings 5, 6, 8
are separate and still open.

## Fields and inputs

- **No naked buttons below a field.** Frequent controls sit inline on the row;
  occasional ones become an inline opener plus an anchored popup (the color-picker
  pattern already chosen for special inputs).
  Seams: `FormDraw.cpp:278-279,332-333` (`DrawTuning`, `DrawExpressionShelf` render
  below today).
- **Direct-drag stays.** The tuning slider is inline and draggable; the exact-entry
  text box beside it can be smaller. Range-setting (when no range is known) and
  expression literal editing/promotion collapse behind the inline opener.
  Seams: `Tuning.cpp:46` (the below-field "Set slider range" button).
- **No per-field "Connect input" button.** Connect is reached through the field's
  reference combo (below) and the standalone wizard, not a stacked button.

## Input wizard (signal creation)

A guided, stepped wizard over the existing `ConnectInput`/`ConnectionBuilder`
logic (`kMeasure`/`kFraction`/`kExhausted`/`kHitResponse`): choose the driver →
pick an actor value from the searchable catalog (3A: descriptions, units, live
samples) → choose the mapping and its parameters (guard zero denominators, 3B) →
confirm. Engine-free connection logic is done; this is presentation over it.

- **Advanced** swaps the guided steps for the searchable table (today's flat
  `InputBrowser` catalog), so power users keep one-click connects.
- **Two entry points, one wizard:**
  - Standalone button beside "add signal" in the Signals tab → create-and-name
    only (no field to bind).
  - **"New input"** at the bottom of a connectable field's reference combo →
    create-and-bind (the full `ConnectInput` path). Living in the combo satisfies
    the no-buttons-below rule.
- **"+resource" splits into per-tab add buttons** (add source/mask/signal/curve)
  under each resource tab, replacing the combined `DrawResourceAddMenu`.
  Seams: `Workspace.cpp:87-117`, `InputBrowser.cpp:96-118`, `InputConnections.cpp:29-73`.

## Second feedback pass

- **Slider always has a usable default range.** No field gates its slider behind
  "Set slider range" first. Known `range`/`workingRange`/`units` win; otherwise a
  **value-relative** default, `span = max(|v|, eps)`, range `[v - span, v + span]`.
  Symmetric around the value so negatives get a negative-extending range, never a
  hardcoded 0 minimum. The opener demotes to *adjust*.
  Seams: `Tuning.cpp:34-67`.
- **Header/action bars express scope, with labels.** Three groups, each labelled
  (Session / Recipe / Audition): session/global (wearer, armor, view, global clock,
  Return to live), recipe/model (New, Save, Revert, Undo, Redo, Rename), and
  audition/selection (Solo, Mute, Fire). Solo/Mute are audition state, not recipe
  actions, and move out of the recipe group. Section 3 amendment.
  Seams: `ContextRows.cpp`, `RecipeActions.cpp`, `StudioPage.cpp:127-155`.
- **Relationship panel becomes a labelled table** with a value column. Driven-by
  `| Property | Driver | Value |`, Used-by `| Consumer | Property | Component | Value |`,
  follow-links on cells. Value populated where live state resolves it, blank
  otherwise. Replaces today's `Dim`+`Follow` lists.
  Seams: `RelationshipPanel.cpp:87-124`.
- **"New ..." combo entries navigate to the authoring surface.** After create+bind,
  `Navigate` the inspector to the new resource's subject (reuse the reference-follow
  resolver). "New mask" jumps straight into the as-terms editor (`EditMaskAsTerms`),
  skipping the mask-inspector step; "New input" is the wizard.
  Seams: `FormDraw.cpp:247-249`, `Workspace.cpp:314-321,397-411`, `PaintPanel.cpp:578`.
- **Terms editor: controls in the row, no expanding section.** Each pattern-chooser
  offer row gets its own inline preview and add buttons; drop the expanding
  selected-offer detail section (`PatternChooser.cpp:135-165`). Terms table stays
  the place to tune a placed term ("..." settings).
  Offers carry tunable `TermForm` fields (`PatternChooser.cpp:72-73`): add with
  defaults, then tune a selected placed term in the **rightmost pane beside the
  composite**, against the live preview. This gives the rightmost pane a
  contextual tuning role inside the terms editor, distinct from the inline tuning
  used for layers (tree rows) and output scalars (output topline) elsewhere;
  intentional and mode-specific unless it later generalizes to layers/outputs.
  Seams: `PaintPanel.cpp:527-565`, `PatternChooser.cpp:37-165`, `Workspace.cpp:482` (preview pane).
- **Mask/term properties are as tunable as any input** — sliders, value-relative
  ranges, exact entry. They draw through a bare `FieldInput` today
  (`PatternChooser.cpp:68-88`), not the tuned pipeline. `TermField` already wraps a
  full `FormField` (`TermTemplates.h:41-42`), so this is routing, not a parallel
  schema. Wrinkle: term parameters feed `BuildTerm` to derive the term's expression
  and commit through the field's `apply` callback, not a recipe `PostField`. So the
  tuning widget must accept a **generic commit sink** (`FormField` + `apply`) instead
  of being hardwired to recipe edit intents. Needs stable domain-identity
  `FieldKey`s (mask + term index + parameter) so ranges and gestures persist.
  Seams: `PatternChooser.cpp:68-88,114-124`, `TermTemplates.h:41-45`, `Tuning.cpp`.
