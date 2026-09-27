Status: frozen design baseline, 2026-09-13. No implementation changes.

# UI v2 proposal

> Archived 2026-09-22. Superseded as a work plan by [Alpha preparation](../plans/alpha-preparation-2026-09-22.md). The tasks and status claims below are historical; unchecked items are not active commitments. Consult current code and component documentation for implemented behavior.

Implementation is tracked in the [implementation plan](ui-v2-implementation-plan.md).
Keep this baseline fixed; record implementation decisions and proposed design
departures in that plan. Enchantment appearance experiments remain deferred.

References: [design principles](../ui-design-principles.md) and
[current UI assessment](../checkpoints/ui-assessment-2026-09-13.md).

## 1. Direction

One Studio workspace for inspecting armor, composing effects, tuning their
drivers, and building masks. Keep the current stack/inspector relationship.
Bring saving, output inspection, and preview state into that workspace.

Every selection should answer:

1. What am I looking at?
2. What controls it, and what else uses it?
3. What can I change or try here?

The worn armor remains the live result. Texture previews explain that result.
A separate embedded 3D renderer is not required by this proposal.

## 2. Keep, change, remove

| Keep | Change |
| --- | --- |
| Ordered layers, drag reorder, inspector | Make outputs and layers the main recipe navigator. |
| Typed fields and compatible references | Add clear Connect/Edit actions, numeric sliders, units, and ranges. |
| Source, mask, and composite previews | Keep the relevant preview beside its properties; allow enlargement. |
| Live signal values and dependency details | Show drivers and named consumers, with Back navigation. |
| Paint offers, terms, Keep/Discard, history | Open mask editing inside the inspector, retaining its destination. |
| Output board and contribution facts | Make them a Studio overview rather than a separate-page prerequisite. |
| Freeze, scrub, speed, Fire, solo, mute | Collect active preview state in a visible Try bar. |
| Recipes and Setup pages | Use Recipes for files; Setup for preferences and diagnostics. |

Remove the mandatory Compose/Paint mode switch, the permanent resource-bottom
allocation, nested detail-modal chains, and generic `...` actions where a named
action fits. Remove bare actor-value text as the primary input-discovery method.
Keep expert text entry available.

Save/Revert should no longer require leaving Studio. Repeated generic Clear
buttons become scoped actions such as Remove output or Clear mask terms. Routine
edits use Undo; reverting a dirty file explicitly describes the discarded work.

## 3. Full layout

Illustrative content and values below are mock data. Brackets indicate controls.
The three columns resize; their widths are not fixed to the drawing.

```text
+--------------------------------------------------------------------------------------+
| [Studio] [Recipes] [Setup]                                                            |
| Wearer [Player v]  Armor [Daedric Armor v]  View [3rd person v]                         |
| Recipe [Spine glow v] * Unsaved                   [New] [Save] [Undo] [Redo] [Actions v]|
+--------------------+-------------------------------------+---------------------------+
| RECIPE             | INSPECTOR                           | ARMOR / PREVIEW           |
| [Find...         ] | [Back] Emissive > Layer 1            | Geometry [Torso v]        |
|                    |                                     | [Final] [Mask] [Regions]  |
| [Output overview]  | Pattern   [@spine_pattern] [Edit]    |                           |
| v Material         | Placement [@spine_trim   ] [Edit]    | +-----------------------+ |
|   v Emissive       | Opacity   [@hit_response ] [Edit]    | | Selected texture      | |
|     > Layer 1      |                                     | | preview               | |
|     + Add layer    | Color     [swatch] [1.0, 0.4, 0.1] | | [Enlarge]             | |
|   Roughness        | Blend     [Add v]                   | +-----------------------+ |
| > Shell            |                                     | On armor: Final result    |
| > Lights           | Hit -> Decay -> Layer opacity       | [Highlight selection]     |
| [+ Output]         | Live opacity: 0.42                  |                           |
|                    | [Edit response] [Try hit]           | A  Torso                  |
| > Sources (3)      |                                     | B  Pauldrons              |
| > Masks (2)        | Used by: this layer                 | [Inspect contributions]   |
| > Signals (4)      | [Solo layer] [Mute layer]           |                           |
| > Curves (1)       |                                     | Selected recipe applies   |
| [Recipe settings]  |                                     | to 2 geometries.          |
+--------------------+-------------------------------------+---------------------------+
| TRY  [Fire hit]  [Freeze] [Step]  [More v]                  [Return to live]            |
| Active: none.  Application: applied.  [Problems: 0]                                    |
+--------------------------------------------------------------------------------------+
```

The right column contains texture inspection and controls for the visible armor;
it is not an embedded armor viewport. Regions/highlighting arrive after runtime
overlay support. Until then, show geometry names, coverage, and existing previews.

### Space and navigation

- Keep wearer, armor, recipe, unsaved state, and preview state visible.
- Each main pane scrolls independently. Preserve scroll and selection on Back.
- Following a resource normally replaces inspector content and preserves Back.
  Use modals for short, contained tasks such as choosing an input, renaming a
  recipe, picking a color, or enlarging a preview. Close returns to the originating
  control. Avoid nested modals for sustained dependency exploration.
- The right column follows the inspected item. Pin its preview when comparing
  a source with the final composite; the pinned subject remains labeled.
- Collapse the right column first on narrow windows. Then turn the navigator into
  a drawer. Do not squeeze three columns into unreadable fields.
- Provide a compact Try view that leaves more of the game visible. It retains
  context, active overrides, and Return to live.

```text
+------------------------------------------------------------+
| Player / Daedric Armor / Spine glow *       [Save] [Undo]    |
| [Browse] Emissive > Layer 1             [Preview drawer]     |
+------------------------------------------------------------+
| Inspector: full available width                            |
| Pattern [@spine_pattern] [Edit]                             |
| ...                                                        |
+------------------------------------------------------------+
| TRY: hit response held at 1.0             [Return to live]  |
+------------------------------------------------------------+
```

## 4. Core components

### 4.1 Property control

Literal values get a slider and exact entry where meaningful. Connected values
show the driver and live result. Editing the driver is a deliberate action.

```text
| Glow strength                                      [Connect] |
| 0 -----------o---------------- 5       [2.00]                 |
| Slider range [0 .. 5]     Limits: none declared               |

| Glow strength                                     [Change v] |
| Driven by [@stamina_response]                 Live: 2.00      |
| [Edit driver] [Show uses] [Try a value]                       |
```

Use property-specific units and limits. A slider range is a working range, not an
invented bound. Unknown or unbounded values say so. Sliders preserve exact entry;
colors keep swatches; vectors keep labeled components. One drag is one undo step.

### 4.2 Connect an input

Open this browser from a property or from Signals. From a property it retains
the destination and offers compatible inputs. From Signals, select an input and
then choose a destination through Show compatible properties.

```text
+----------------------------------------------------------------+
| Connect to: Layer 1 opacity                           [Cancel]  |
| Search [stamina                                            ]   |
| [Actor values] [Actor state] [Events] [Enchantment] [Time]       |
|                                                                |
| Stamina     Current [42]   Maximum [100]   Unit: points          |
| Measure [Current v]    [Explain measurements]                   |
|                                                                |
| Use as: [Direct value] [Fraction of maximum] [Condition]        |
| Condition [Empty v]                                            |
| Creates: stamina input + empty condition + opacity connection   |
|                                                     [Connect] |
+----------------------------------------------------------------+
```

List inputs supported by the plugin, with searchable descriptions. A stamina
fraction is a composed setup, not a newly invented engine measurement. Handle a
zero maximum explicitly. Enchantment identity and magnitude/cost are distinct;
do not imply the current enchantment signal provides every enchantment fact.

### 4.3 Driver strip and response editor

The strip is a compact, navigable explanation. It summarizes real references;
complex expressions can show Multiple inputs and open their details.

```text
| [Stamina: 0] -> [Empty: 1] -> [Glow strength: 2]               |
| Response [Empty only v]       [Edit expression]               |
|                                                              |
| glow  2 +*                                                   |
|         ||                                                   |
|       0 +o--------------------------------                   |
|         0              stamina points ->                     |
| Off [0.0]  On [2.0]     [Try empty] [Try above zero]           |
```

Offer exact exhaustion, below-threshold, and smooth low-resource responses.
An event response uses elapsed-time axes instead. Being empty keeps a condition
active; becoming empty can launch an event. Name that choice.

Graphs edit supported curve/signal configurations. Arbitrary expressions remain
editable as text, with controls for supported operands. Numeric operands can be
tuned in place or promoted to named signals. Preserve the formula's meaning;
do not approximate unfamiliar expressions to fit a graph.

### 4.4 Expression inputs and relationships

```text
+----------------------------------------------------------------+
| Signal: glow_level                                             |
| Formula [@strength * @hit_response                           ]  |
|                                                                |
| INPUTS                                                         |
| strength       0 ------o--------- 5  [2.0]    [Open]            |
| hit_response   Live: 0.42                     [Open] [Try hit]  |
|                                                                |
| USED BY                                                        |
| [Material / Emissive / Glow strength]                          |
| [Light / Intensity]                                            |
| Editing this signal affects both properties.                   |
+----------------------------------------------------------------+
```

Expose referenced inputs, including their own drivers, and editable literal
operands. Selecting an operand highlights its occurrence in the formula.

```text
| Formula [2.0 * @hit_response]                                  |
| Operand: 2.0     0 ----o---------- 5 [2.0] [Promote to signal]  |
| Promote as [strength]                                         |
| Result: @strength * @hit_response       [Create and replace]   |
```

Tuning a literal changes only that occurrence. Promotion creates a constant
signal and replaces the selected occurrence in one undoable edit. References to
existing signals retain their shared-use warning. Resolve operands from parsed
expression structure; stale selections after text edits must not alter another
occurrence. Following references retains a Back path to the original field.

### 4.5 Mask editor in context

Keep Paint's term builder and temporary session. Replace the page transition with
an inspector task whose heading names its destination.

```text
+----------------------------------------------------------------+
| [Back] Edit mask: spine_trim                                    |
| Destination: Material / Emissive / Layer 1 / Mask               |
|                                                                |
| TERM                  COMBINE             [Solo] [Mute]         |
| Spine bone coverage   Start                                    |
| Upper-body region     And                                      |
| [+ Add term]   [Undo] [Redo]                                    |
|                                                                |
| Selected term: Spine bone coverage                             |
| Bones [NPC Spine2 ...]                         [Inspect coverage]|
|                                                                |
| Offers [Find...       ] [Bones] [Parts] [Materials] [Patterns]   |
| Name                   Coverage       [Add to mask]             |
|                                                                |
| Preview: temporary mask on selected armor                      |
| [Keep mask and assign] [Discard draft]                          |
+----------------------------------------------------------------+
```

Coverage remains visible alongside offers. Shared masks show their consumers
before Keep. Back must not silently discard a draft: retain it with a visible
Resume mask draft action until Keep or Discard. Saving the recipe excludes the
draft and says so. Keep finishes authoring; Save persists the recipe.

### 4.6 Pattern chooser

```text
| Pattern for: Layer 1                       [Images] [Procedural] |
| +--------------+ +--------------+ +--------------+              |
| | preview      | | preview      | | preview      |              |
| | Band         | | Ripple       | | Saved mask   |              |
| +--------------+ +--------------+ +--------------+              |
| Selected: Band                                                |
| Width  ----o--------- [0.15]     Position ----o--------- [0.40]  |
| Coordinates [UV v]              [Inspect construction]         |
| [Use as source] [Use as mask]                                  |
```

Start with supported images, masks, and small recipes assembled from existing
sources and expressions. Show only available patterns. Noise, lightning, and
flames join this chooser when their spatial implementations are proven; a noise
signal over time is not a spatial noise texture.

Travel adds a driver to position. Establish coordinate space, direction, and
coverage first. A UV band is not an arbitrary spine path; path authoring is a
separate extension, not a promise made by this chooser.

### 4.7 Armor regions and contribution inspection

```text
| Armor inspection: Daedric Armor                                 |
| Region basis [Geometry assignments v]   Overlay [On]             |
| A [color] Torso            [Highlight] [Inspect]                 |
| B [color] Pauldrons        [Highlight] [Inspect]                 |
|                                                                |
| Selected: Torso                                                |
| Contributions, in composition order:                           |
| [Imported sheen / Shell / Diffuse]                             |
| [Spine glow / Material / Emissive]                              |
| [Use selection in mask] [Restrict selected output]              |
```

Separate geometry assignments from inferred texture clusters. Label clusters as
Cluster 1, etc., unless the user names them; do not guess metal/leather/cloth.
Pair colors with labels and selection outlines. Keep IDs/colors stable for the
current analysis; report when reanalysis changes the grouping.

Use existing selection/coverage lists first. Add runtime overlays next. Direct
clicking on the armor is optional future picking support. Show only actions the
selected region can support: output selectors and texture masks have different
expressive limits. An overlay is temporary and must restore the normal result.

### 4.8 Try bar

```text
+----------------------------------------------------------------+
| TRY: Player / Daedric Armor / Spine glow       [Return to live]  |
| [Fire hit]   Stamina [Live v]   Glow strength [Live v]           |
|                                                                |
| Active: Stamina override = 0 points                            |
| Stamina   0 o------------------- 100 [0] [Release]              |
| [Freeze] [Step] Speed [1.0x]  Time ----o-------- [0.50 s]        |
| [Solo: Layer 1] [Clear solo/mute]                               |
+----------------------------------------------------------------+
```

Collapsed, it always names active overrides/isolation. Expanded, it holds only
controls relevant to the selected effect plus explicit clock scope. Start with
existing Fire, solo/mute, and clock controls. General value holds follow later.

Override plugin evaluation, not the actor's actual stats or equipment. Scope
overrides to the preview subject; clear them on subject change or Studio close.
Return to live releases overrides, isolation, and clock changes made for audition.
Navigation within the same subject preserves the audition and its visible status.

Max glow needs a declared test value. For an unbounded property, ask for a hold
value rather than inventing a maximum. A coordinated peak requires an explicit
group of held values; it cannot be inferred from one signal.

### 4.9 Deferred: enchantment appearance experiments

Set aside at the user's request. Keyword spoofing, simulated enchantment
contexts, keyword-to-shader mappings, and combining appearance contributions
are outside the current UI v2 scope. No selection or reduction approach is chosen.
Revisit only as a separate design discussion.

Ordinary recipe keys, matching inspection, and existing enchantment/EFSH signal
editing remain available. Actor-value and event auditioning remain in scope.

## 5. Output overview, files, and empty states

The current board becomes Output overview in Studio. Preserve original/written
slot facts, refusal reasons, scalars, thumbnails, and contribution order.

```text
| OUTPUT OVERVIEW       Geometry [Torso v]                        |
| Slot          Material                 Shell                   |
| Emissive      [preview] 1 layer         [+ Add output]          |
| Roughness     [preview] 2 layers        Not available            |
| Diffuse       Unchanged                [preview] 1 layer        |
| Lights        [1 output: inspect]                              |
| Select a cell to edit; inspect its reason when unavailable.     |
```

Recipes retains loaded files, paths, keys, diagnostics, reload, and explicit
reapply/baseline tools. Selecting a file shows details; Open in Studio selects it
with its applicable preview subject when available. Unmatched recipes should be
editable without pretending they have a live armor result. This requires separating
document selection from the current applied-piece selection.

Setup retains INI controls and logs. Runtime internals belong in expandable
diagnostics; concise application failures remain visible beside the affected work.

| State | Show and offer |
| --- | --- |
| No armor available | Explain the missing live subject; allow file inspection/editing. |
| Armor has no matching recipe | New recipe for this armor, with scope shown. |
| Output has no layers | Add layer with a visible starting source/color. |
| Selector matches nothing | Show its rule and offer Edit placement. |
| Invalid field | Keep the typed draft and inline error; retain the last valid effect. |
| Change pending or refused | Show pending/applied/refused beside the edited context; provide the reason. |
| Save fails | Keep Unsaved and show the file/error; never imply success. |

## 6. Model integration

Use the current recipe, snapshot, field-binding, edit, and intent architecture.
Pattern/placement/behavior are views of existing records, not new file sections.

```text
 Recipe + live armor
         |
         v
 Published snapshot ---> selection + property descriptions
                                    |
                                    v
                         inspector / graph / slider
                                    |
                  +-----------------+-----------------+
                  |                 |                 |
               Navigate           Author             Try
                  |                 |                 |
             editor state     RecipeEdit batch   preview command
                                    |                 |
                                    +--------+--------+
                                             |
                                   runtime acknowledgment
                                             |
                                      next snapshot
```

| Shared addition | Purpose |
| --- | --- |
| Resolvable property/resource address | Let selection, drivers, consumers, and controls refer to the same subject; reject stale targets after deletion/reorder. |
| Property/input descriptions | Reuse types, units, supported measurements, range meanings, and compatible operations across controls. |
| Dependency queries | Derive driver and consumer links from existing recipe references. |
| Gesture transaction | Preview slider changes continuously; commit one meaningful undo step; cancel restores the starting value. |
| Scoped audition state | Hold values without modifying saved recipes or game stats; restore on exit. |
| Region identity and visualization command | Link coverage, legend, and temporary armor overlay. |

Reuse `FormField` bindings and validation. Extend their metadata rather than
teaching each widget its own parsing rules. Multi-record helpers submit one edit
batch. UI drafts may appear immediately, but applied results come from runtime
acknowledgments. Latest slider requests must not be overwritten by older results.

## 7. Delivery order

| Pass | Deliverable | Dependency |
| --- | --- | --- |
| 1. Navigation and quick wins | Studio Save/dirty/Revert; scoped action labels; output overview; resizable panes; Back-based details; compact active-preview bar | Mostly existing models and controls. |
| 2. Direct tuning | Numeric sliders plus exact entry, known ranges, input descriptions/browser, visible live values, driver/consumer navigation | Property addresses/metadata and gesture history. |
| 3. Connected authoring | In-context mask task, operand tuning/promotion, expression input shelf, simple response graphs, exhaustion/hit helpers, supported pattern chooser | Existing recipe constructs, parsed operand identity, atomic edits, draft lifecycle. |
| 4. Live inspection and audition | Region overlays, general value holds, scoped restoration, peak-value controls | Runtime visualization and evaluation overrides. |
| 5. Extended patterns | Proven spatial noise/flame/lightning patterns, optional coordinated peak | Separate capability work; integrate into the pattern chooser and Try bar. |

Pass 1 is useful on its own. Do not block layout improvement on overlays or new
procedural rendering. Actor-value fraction helpers must use supported measurements;
specialized pattern/response names ship only when their behavior is verified.

No full node editor, independent 3D viewport, general A/B system, or format-1
redesign is proposed. Freehand paths and direct armor picking remain optional
extensions. These limits keep the first implementation reviewable.

## 8. Acceptance walkthroughs

| Task | Success condition |
| --- | --- |
| Stamina exhausted -> glow | Discover stamina from opacity/strength, connect the condition, inspect boundary values, save without leaving Studio. |
| Hit -> glow | Create or find the response, fire a test hit, watch the result, find its consumers. |
| Tune an expression | Adjust a literal or referenced constant; promote one operand without changing the result; see shared uses; undo each gesture in one step. |
| Mask the spine | Find coverage, build a mask, preview it, Keep into the original layer, return with selection intact. |
| Travel along armor | Verify available coordinates/direction, tune a supported moving pattern, scrub progress, then connect time/event behavior. |
| Maximum glow | Hold an explicit value, see its scope, return to live without recipe changes. |
| Explain an unexpected color | Inspect armor contributions, solo explicitly, follow drivers, restore full result. |

Check layout in game at narrow and wide window sizes and different UI scales.
Verify keyboard focus, numeric entry, Back, draft preservation, undo, save/reload,
delayed updates, and preview restoration. Colors always have text labels. Source
inspection and successful builds do not establish visual correctness.
