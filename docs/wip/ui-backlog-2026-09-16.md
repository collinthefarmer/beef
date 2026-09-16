# UI backlog — 2026-09-16

Captured mid-flow, not yet scheduled. Each item: the ask, where it lives now,
and the rough direction. Anchors are current as of this date.

## 1. Clean up the Revert Recipe modal (and its action bar)

**Ask.** The modal's close control sits as a plain button at the bottom. Give
the modal a `Rule` header and make close a **right-aligned `[X]`** on that rule
— the same remove control the inspector rules use (`RemoveButton`). Also
sanity-check the modal's action buttons.

**Where.** The modal is `DetailModal` (`menu/MenuWidgets.cpp:826`), which today
draws the body then `ImGui::Button("close")` at the bottom. Its one caller for
revert is `DrawRecipeFileActions` (`menu/RecipeActions.cpp:71`) — the
"revert-file" modal (`:99`) with **Revert edits** (`:103`, →
`RevertRecipe`), sitting near **Save** and **Revert to file** (`:96`).

**Direction.** Give `DetailModal` a titled `Rule` with the `[X]` in its trailing
slot (`Rule(spec, RowButtonWidth(), [&]{ if (RemoveButton(0)) CloseCurrentPopup(); })`)
instead of the bottom "close" button; every `DetailModal` caller inherits it, so
check the other callers too. Then review the revert/save action set for wording
and destructive-action confirmation (revert discards edits).

## 2. Channel inputs (`FieldKind::kChannels`)

**Ask.** Render channel selection as **inlined per-channel checkboxes** rather
than a text field. Support **overridable channel names** (`xyz` reads better
than `rgb` for some slots, e.g. a normal/offset vector) and **omitting the alpha
channel** where it does not apply.

**Where.** The value is `ChannelSet` (`recipe/Recipe.h:570`, `ChannelSet::Parse`
`:575`, `ChannelsOf(Slot)` `:787`); the field is `FieldKind::kChannels`
(`studio/Fields.h`), rendered today through the plain text input path
(`kFieldKinds` maps it to a text widget). A `SlotRow`/output carries a
`ChannelSet channels`.

**Direction.** A `DrawChannels` widget over a `ChannelSet`: one checkbox per
channel drawn inline, labelled from a per-field name set (default `r g b a`,
overridable to `x y z (w)`), with the alpha/4th channel omitted when the field
declares 3-component. Commit as the same `ChannelSet` the text path parses, so
the recipe model is unchanged. Decide where the name-override + arity live —
likely a small spec on the `FormField` (channel labels + count), fed by the
`*Form` builder that knows the slot.

## 3. A standard "text + info tooltip target" component

**Ask.** A reusable component: a **label paired with an info affordance** that
points the user to details relevant to that label.

**Where.** Today there is `HelpMarker(text)` (`menu/MenuWidgets.cpp:947`, a
"(?)" with a hover tooltip) and `Tooltip(text)` (`:958`, attaches to the prior
item). There is no single "label + its help" unit; callers hand-assemble
`TextUnformatted` + `HelpMarker`/`Tooltip`.

**Direction.** One helper — e.g. `LabelWithHelp(label, help)` — that draws the
label and the info marker together with consistent spacing, so a form/rule label
and its explanation are one call. Consider letting the "details" be richer than a
tooltip string (a navigate target or a longer popup) so it can *point to*
relevant detail, per the ask, not only describe it. Fold in the existing
tooltip-deferred convention (one pass over all tooltip text when the UI is final).

## 4. Discovery combos for game-object inputs (events, actor values, forms)

**Ask.** Inputs for resources that refer to **game objects** (events, actor
values, and any form reference) should **help the user discover valid values**,
not require typing an editor ID or event name blind. Combos backed by discovered
candidates.

**Where.** The pattern already exists for **actor inputs**: `InputCatalog.h`
(`ActorInputInfo`, `DescribeActorInput`, `InputSample`, `InputMatches`), the
snapshot's `actorInputs` (`studio/Snapshot.h:316`, filled by
`BuildActorInputCatalog` engine-side), `CanConnectInput` (`InputConnections.h`),
and the "New input" wizard `InputBrowser.cpp`. Form references today go through
plain editor-ID text fields (`FormRef::From`).

**Direction.** Generalise the actor-input catalog idea to other game-object
kinds: the engine publishes a candidate list per input kind (events on the
selected actor, actor values, keywords/forms by type) into the snapshot; the
menu offers a filterable combo (reuse the `ReferenceCombo`/`ChoiceCombo` +
`NewInput` idiom) instead of a bare text field, while still allowing a typed
value. Scope per kind: actor values are a fixed enum (cheap), events are
per-actor/animation-graph (needs engine discovery), forms are by editor-ID
lookup (a catalog keyed by form type). Engine-free `studio/` stays the vocabulary;
the engine fills the catalogs, mirroring `actorInputs`.

**These conveniences should supersede the "New input" wizard, not sit beside
it.** The wizard (`menu/InputBrowser.*`: `OpenInputWizard`, `DrawInputWizard`,
`DrawSignalWizardButton`, reached from `SignalCombo`'s "New input" entry) is a
multi-step modal specialised to *actor-value* semantics — a `WizardStep` flow
that picks a **measure** (current / max / fraction / exhausted, `InputSample`)
and builds an `InputConnectionSpec` (`kHitResponse` / `kFraction` / `kExhausted`
/ `kMeasure`) via `ConnectInput`. That specialisation is exactly what does not
generalise across events and forms. The migration: the combo picks the
candidate inline, and the connection-semantics that the wizard exposes as steps
become inline options (or a small anchored popup) next to the combo, or a
sensible per-kind default — no dedicated wizard. Preserve what the wizard does
well (live-value sampling, the measure choices, create-and-connect in one
gesture); drop the multi-step modal shell. Retire `InputBrowser` once the combo
covers the actor-value case, then extend the same combo to events and forms.

**Candidate lists must be dynamic, not hard-coded — a `GameObjectService`.**
Hard-coded value tables (a fixed actor-value enum, a baked keyword/event list)
drift as the game and its mods change, and they make sibling-mod support
brittle: a combo can only offer what its static table knows, so forms, keywords,
magic effects, or events another mod adds are invisible. Prefer a service that
enumerates from the **live loaded game data** at runtime, so the combos reflect
whatever load order is actually present. This is the existing `InputCatalog` /
`actorInputs` / `BuildActorInputCatalog` pattern widened: one engine-side
`GameObjectService` that, per kind, reads from `RE::` (data-handler form arrays
by form type, the actor-value list, the selected actor's animation-graph events)
and publishes plain candidate records — `{ display, form key / name, kind }` —
into the `Snapshot`, exactly as `actorInputs` is published today. Layering holds:
the service lives in `engine/` (it needs `RE::`), `studio/` stays engine-free and
just carries the catalog records, and the menu renders the filterable combo.
Open questions for the pass: these lists are large, so scope discovery by
relevance (events for the selected actor, forms filtered by type/keyword) and
refresh on demand rather than every frame; and each kind names where it sourced
its candidates so a missing value is diagnosable.

## 5. Fully expose and semantically label surface-output inputs

**Ask.** A pass over the surface outputs to make sure every input a slot has is
exposed in the inspector and labelled with meaning (name, units, help) rather
than a generic field. Same theme as item 4 (expose + label inputs), aimed at a
surface output's own parameters instead of its game-object references.

**Where.** The per-slot input vocabulary is `ScalarField` (`recipe/Recipe.h:708`)
+ `ScalarsOf(Slot)` (`:780`) + the `kScalarFields` spec table (`recipe/Words.h:178`).
The output inspector builds these fields via `ScalarForm(const LayerStack&)`
(`studio/Forms.cpp:639`), with labels/help from `kScalarFields`/`kFieldKinds`
and units/working-range on the `FormField`. The **source of truth** for what
inputs a slot actually consumes is the GPU write interface `SlotTarget`
(`render/Binding.h:33-54`): `WriteTexture`, `WriteEmissive`, `WriteFuzz`,
`WriteHeightScale`, `WriteGlint(GlintParameters)`, `WriteCoat(roughness, level)`,
`WriteSubsurface(color, thickness)` — and their parameter structs.

**Direction.** For each `Slot`, enumerate its true inputs from the `SlotTarget`
writer signatures and their parameter structs, then confirm each one: (a) maps
to a `ScalarField` that `ScalarsOf(slot)` includes, (b) is surfaced by
`ScalarForm`, and (c) carries a semantic name + units + help + working-range,
not a generic one. Fill the gaps — an input a slot *writes* but the form does
not *surface* is exactly the bug this pass catches, and the writer set is the
coverage oracle. This folds in item 2 (a slot's channel input is one of these)
and item 3 (the "text + info tooltip" is how each input should read once
labelled).

**Decided: labels are slot-contextual.** The same `ScalarField` (`kWeight`,
`kLevel`, `kStrength`, `kColor`, `kRoughness`…) appears in several slots via
`ScalarsOf(slot)` and reads differently in each — `kLevel` is "Coat strength"
under coat, `kColor` is "Subsurface tint" under subsurface — so the label,
help, and units are keyed by **(Slot, ScalarField)**, not by `ScalarField`
alone. The flat `kScalarFields` table cannot be the source. Still single-source
it, just at the right key: a per-`(slot, field)` label table (or each slot's
spec carrying its fields' labels), consumed by `ScalarForm` — which already
knows the slot from the `LayerStack` — when it builds each `FormField`. Not
re-authored per form, and not a global string per field.

## 6. Bring the Recipes and Setup pages onto the standard components

**Ask.** Review both pages and adapt them to the standard UI components the
standardization pass introduced; the pass focused on the Studio page and only
touched these two incidentally (they got the shared table styles and, for
Recipes, one `DrawDiagnostics` call), so they still carry page-specific idioms.

**Where — Recipes** (`menu/RecipesPage.cpp`):
- `DrawStoreActions` (`:121`) — a loose row of `ImGui::Button` (Reload recipes /
  Re-apply all / Retire all), not the standard action-bar/rule-trailing idiom.
- `DrawResolved` (`:108`) / `DrawResolvedGeometry` (`:92`) — raw `BulletText`
  and `TextWrapped`, not standard rows/labels; candidates for a real table +
  the item-3 label + info component.
- `DrawLoadedTable` (`:33`) already uses `kGridTable` and `Problem`/`Warn`/`Ok`
  status; confirm its cells (`TextWrapped` keys/path) read consistently.
- `DrawRecipeFile` (`:139`) is already on `Rule` + `DrawDiagnostics` — the model
  the rest of the page should match.

**Where — Setup** (`menu/SetupPage.cpp`):
- `DrawSaveBar` (`:120`) — Save INI / Reload INI / Re-apply, a save-actions
  strip. This is the same shape as the recipe file bar (`RecipeActions.cpp`) and
  the modal action bars (item 1); they should converge on one action-bar / rule
  idiom (the Case 1 "status + action bars adopt the rule idiom" question).
- `DrawValueTable` (`:177`) + `Widget` (`:91`) — settings rendered as raw
  `ImGui::Checkbox`; move to the standard toggle/field idioms, and give each
  setting a label + info (item 3) for what it does.
- `Shown` (`:54`) still filters dead settings out of the table — the
  "58 of 68 settings do nothing" pruning in `docs/wip/deletions.md` is the real
  fix and should land before or with this pass, not be papered over by the
  filter.

**Direction.** Same standard vocabulary as the Studio page: `Rule` (with the
leading status / trailing action slots) for section headers and action bars, the
shared `k*Table` styles, `DrawDiagnostics` for problem lists, `Problem`/`Warn`/
`Ok`/`Dim` for status, and the item-3 label + info component for explained
labels. Ties to item 1 (the action-bar / modal idiom the save bars share).

## 7. Scheduled usability passes — Recipes page, Setup page

**Ask.** Item 6 makes these pages *consistent* with the standard components;
this schedules a pass over **each** to make sure it is actually *usable* — the
workflow and clarity, not just the widgets. Two separate passes, folding the
item-6 adoption in.

**Recipes pass** (`RenderRecipes`). The page's job: see which recipes are loaded
and their health, understand what is applied to the current selection and why
(the "Resolved for the selection (merge order)" view), inspect what a recipe
writes (the embedded board), and drive the store (Reload / Re-apply / Retire) and
a recipe's file (Save / Revert). Questions the pass answers: is the merge-order
view legible enough to explain *why* a piece looks the way it does; are
problem/held-back recipes obvious and recoverable; do the store actions
communicate their blast radius (all actors? baseline?); is the loaded table
scannable at a glance (counts, keys, state, file).

**Setup pass** (`RenderSetup`). The page's job: view and change plugin settings,
save/reload the INI, re-apply, and read the log. Questions: does each setting say
what it does and what changing it costs (a re-apply?) — the item-3 label + info;
is the save/reload/re-apply flow clear about pending vs saved; is the dead-setting
noise gone (item 6 / `deletions.md`) so only live settings show; is the log
useful or just noise.

**Direction.** Run each as a real review of the page against its job, not a
widget swap — the component adoption (item 6) is an input, usability is the goal.
"Usable" is a felt property, so gate each pass on an in-game check (the user
runs it) rather than a static read.

## 8. Dissolve the recipe-keys popup into a collapsible table

**Ask.** The recipe's matching keys are edited through a modal-ish popup; make
them an inline **collapsible table** in the recipe inspector, the same shape as
the "Used by" / "Driven by" relationship tables (Case 3). Adding a key should
help the user **discover** valid values rather than type an editor ID blind —
this is a Game Object Service (item 4) consumer, since nobody knows keyword or
form editor IDs off the top of their head.

**Where.** After the UI standardization move, the keys editor lives in the
recipe inspector: `DrawRecipeSettings` (`menu/ContextRows.cpp`) draws a "Recipe
keys" button that opens `KeysPopup` (`:81`). `KeysPopup` draws `DrawKeysTable`
(the current keys), an "add a key the piece carries" combo fed by
`a_piece.keys`, and a **raw `keyword editor id` text field + "Add keyword"**
(`:102-112`) — the blind-typing path. The collapsible-table model to mirror is
`DrawUsedBy`/`DrawDrivenBy` (`menu/RelationshipPanel.cpp`, Case 3).

**Direction.** Replace the popup with a collapsible "Keys" table inline in the
recipe inspector: each row a key (`kind: operand`) with the row-remove control,
under a collapsible `Rule` like the relationship tables. Fold the two add-paths
into the table's add affordance: the "keys the piece carries" combo stays as a
cheap local source, and the raw keyword/form field is replaced by a discovery
combo backed by the **Game Object Service** (item 4) — keywords, forms, and
enchantments enumerated from live game data, filterable, with a typed value
still allowed. Ties to item 3 (collapsible relationship tables) and item 4 (the
service and the "supersede blind editor-ID fields" theme).

## 9. Remove referenced resources by walking their uses

**Ask.** Removing a resource (mask, source, signal, curve) should be possible
even when it is referenced — walk its references and delete or replace each use,
rather than refusing outright.

**Where.** `RemoveButton` disables at `references > 0`
(`menu/MenuWidgets.cpp`), and the edits themselves refuse when
`CountReferences` finds a use: `Edit(RemoveMask)` / `Edit(RemoveSource)`
(`studio/Edits.cpp:803,819`, `"referenced in N place(s)"`), and the sibling
resource removals. So a referenced resource is currently un-removable from the
UI at all.

**Direction.** A "remove and clean up" path: enumerate the references (the same
`CountReferences` / the relationship-panel data), then per use either clear the
field to its default/empty or remove the consuming row, behind a confirm that
lists what will change. The `PaintPanel` mask-draft discard now does a narrow
version of this (removes the not-yet-referenced mask it created,
`DiscardMaskDraft`); this generalises it to referenced resources and to every
resource kind. Consider whether "replace with" (repoint uses at another
resource) is worth offering alongside "delete uses".
