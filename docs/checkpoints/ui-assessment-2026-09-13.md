Status: record. The source review of the then-current UI against the design
principles.

# Existing UI assessment

Source review on 2026-09-13 against the [design principles](../ui-design-principles.md).
The working tree includes an ongoing cleanup pass. Findings describe inspected
code, not a fixed release or verified in-game appearance. No implementation was
changed for this assessment. Historical regression reports are not treated as
proof of current defects.

## Overall finding

The UI already surfaces substantial information and several useful contextual
actions. Its strongest foundations are typed fields, layer inspectors, live
signal values, texture previews, dependency drill-down, mask offers, and undo.
The main navigation gap is continuity between discovering information,
understanding its possible uses, connecting it, and auditioning the result.

Layout changes can expose existing capabilities more coherently. Input catalogs,
semantic range information, general audition overrides, and linked material
overlays require support beyond rearranging controls.

## Current organization

- **Studio / Compose:** recipe and output context, stack/settings, layer
  inspector, resource tabs, and timeline. `DrawGeometryBody` allocates resources
  a share of remaining height; the stack/inspector split is draggable.
- **Studio / Paint:** mask terms, geometry-derived offers, preview, term details,
  and Keep/Discard. It uses a dedicated paint session and layout.
- **Recipes:** loaded-file table, resolved contributions, output board, and the
  selected recipe's Save/Revert and problems.
- **Setup:** preferences, persistence/reapply actions, and logs.

Evidence: [StudioPage.cpp](../../src/menu/StudioPage.cpp),
[RecipesPage.cpp](../../src/menu/RecipesPage.cpp),
[SetupPage.cpp](../../src/menu/SetupPage.cpp), [View.h](../../src/studio/View.h).

## Assessment by principle

| Principle | Existing support | Gap and layout implication |
| --- | --- | --- |
| 1. Information with action | Layer fields offer compatible references and detail actions. Paint offers include descriptions and coverage. | Save/Revert and the board are on Recipes; detailed properties often require generic `...` buttons. Bring the action relevant to the inspected information into its context and name that action meaningfully. |
| 2. Start from appearance or cause | Output/slot selection leads to a stack and property inspector; fields can lead to signals. | No general input-to-consumer navigation or visible-region-to-control workflow was found in the menu. Support both navigation directions without presupposing a node editor. |
| 3. Reveal relationships | Signal expressions expose referenced signals through nested details; Paint terms expose reads; resource rows carry reference counts; stacks show foreign contributions. | Counts do not identify consumers, and nested modal drill-down does not provide a persistent account of the connection. Show driver and uses for the current selection, including shared-edit scope. |
| 4. Understand values | Type badges explain accepted values; signals display evaluated values; actor-value signals offer a measure choice. `FormField` has optional range metadata. | Actor-value names are free text. General value widgets use text/reference selection and color picking, not numeric sliders; range metadata is not consumed there. No common units/typical/observed-range presentation was found. Add semantics before treating every number as the same slider. |
| 5. Pattern, placement, behavior | Sources/masks, output selectors, layer properties, and signals represent much of this separation. | Navigation emphasizes record kinds; users assemble the relationship themselves. Expose the selected effect's pattern, placement, and driver together while retaining access to records. |
| 6. Audition | Timeline freeze/step/speed/scrub, recipe/output/layer isolation, mute, Paint preview, and a Fire popup for event-bearing signals already exist. | Controls are distributed; no general temporary actor-value override, coordinated peak hold, or enchantment-context audition control was found. Group the meaning and scope of active preview controls; new simulation behavior needs separate design. |
| 7. Connect visible result | Composite/source/mask thumbnails, enlarged previews, output board facts, Paint coverage, and application status are present. | Texture inspection does not by itself identify a region on the worn armor. No linked unique-color material overlay/legend was found. Define region identity and selection before choosing an overlay implementation. |
| 8. Useful starting points | Paint offers, mask presets, default rows, and field-level resource creation give starting points. | These do not yet constitute a discoverable intent workflow such as stamina exhaustion to glow. Present supported building blocks by use, with their resulting connections inspectable. Do not assume all requested procedural patterns are available. |
| 9. Context and reversibility | Shared selection state, recipe undo/redo, mask undo/redo, Keep/Discard, and explicit solo/mute controls are substantial foundations. | Page changes, nested details, Paint transitions, and separate persistence actions fragment the task. Preserve origin and reveal edit versus preview state. Slider gesture grouping remains an acceptance requirement, not verified behavior. |
| 10. Workflow-led layout | Context rows, resource filtering, a draggable stack split, and mode-specific layouts offer a workable base. | Resources receive a default 35% share; timeline is drawn after the body; details and key actions are distributed by page/type. Evaluate space by the active task rather than simply adding more permanent panels. |

## Evidence anchors

- [ContextRows.cpp](../../src/menu/ContextRows.cpp): `DrawRecipeHeader`,
  `DrawRecipeContext`, `DrawEditContext`, `DrawRecipeSettings`, `KeysPopup`.
- [FormDraw.cpp](../../src/menu/FormDraw.cpp): `FieldInput`, `PostField`,
  `DrawSignalReads`, `DrawSignalDetail`, `FirePopup`, `DrawSelector`.
- [MenuWidgets.cpp](../../src/menu/MenuWidgets.cpp): `ValueWidget`, `Badge`,
  `ReferenceCombo`, `DetailButton`. The value widget selects references or edits
  text/color; it does not read `FormField::range` to draw numeric sliders.
- [Forms.h](../../src/studio/Forms.h) and [Forms.cpp](../../src/studio/Forms.cpp):
  typed field descriptions, bindings/creators, optional ranges, and
  `ActorValueFields` with a text name and measure choice.
- [ResourcePanels.cpp](../../src/menu/ResourcePanels.cpp): `DrawSignalRow`,
  `DrawSignalEditor`, `DrawSources`, `DrawMasks`, `DrawResourcesRule`.
- [StackPanel.cpp](../../src/menu/StackPanel.cpp): `DrawComposite`,
  `DrawForeignRow`, `DrawInspectorFields`, `DrawDetailModal`, `DrawStack`.
- [PaintPanel.cpp](../../src/menu/PaintPanel.cpp): `DrawOffers`, `DrawTermReads`,
  `DrawTermSettings`, `DrawMaskPicture`, `DrawMaskRule`, `KeepMaskPopup`.
- [BoardPage.cpp](../../src/menu/BoardPage.cpp): `CellTooltip` and cell actions expose
  original/written slot facts, scalar values, reasons, selection, and isolation.
- [StudioPage.cpp](../../src/menu/StudioPage.cpp): `DrawApplication`,
  `DrawGeometryBody`, `DrawFooter`, `HistoryKeys`.
- [RecipesPage.cpp](../../src/menu/RecipesPage.cpp): `DrawSelection`,
  `DrawRecipeFile`, `DrawLoadedTable`.

## Workflow walkthroughs

### Glow when stamina is exhausted

Current building blocks: an actor-value signal with a manually entered name and
measure, expression/curve editing, a glow-related scalar or layer field accepting
a compatible signal, and live signal display. The user must discover and assemble
the condition and mapping. There is no guided exhaustion connection or general
temporary stamina control visible in the reviewed UI.

Design test: from glow strength, discover the supported stamina input, understand
its measurement, define an empty-state response, and audition zero and slightly
above zero. The connection and return to live state must remain visible.

### Hit-triggered glow and maximum glow

Current building blocks: event-bearing signals expose Fire with payload controls;
signal details and timeline support investigation. This is an existing audition
capability worth making easier to find. Holding a clock is not a general substitute
for holding an arbitrary event-driven value or coordinated effect peak.

Design test: find the trigger from the driven property, fire it, inspect the
response, and distinguish editing that response from temporarily holding it.

### Pattern traveling down the spine

Current building blocks: Paint offers geometry-related terms, sources/masks,
per-term properties and reads, previews, plus signals and timeline controls.
These do not establish that an arbitrary authored spine path or every desired
procedural pattern is supported. Bone coverage, UV direction, and a deliberate
travel path should not be presented as interchangeable.

Design test: keep placement visible while adjusting pattern and travel progress;
make the coordinate space/direction understandable before connecting animation.

### Reuse a pattern across enchantments

Current building blocks: recipe keys, reusable resources within recipes, and
enchantment signal forms. Recipe matching is not itself a simulated enchantment
context. No general context audition surface was found.

Design test: preserve the pattern while trying existing health/stamina/fire-resist
responses and authoring missing responses. Show which changes affect a shared
pattern versus one response. Keep this within existing format/non-goal constraints
unless scope is explicitly reconsidered.

## Recommended next planning pass

1. Map the information, possible uses, and application action for one selected
   property and one selected resource. Use stamina-to-glow as the first example.
2. Sketch alternate arrangements using existing capabilities: selection context,
   driver/uses, property controls, previews, and save state. Keep panel placement
   provisional; compare the number of context changes needed for each workflow.
3. Specify missing shared support separately: input descriptions and units,
   range semantics, consumer navigation, temporary audition values, and material
   region identity. These are not solved by moving widgets.
4. Validate the candidate layout in game for usable size, scrolling, modal depth,
   preview visibility, and selection continuity. Source inspection cannot establish
   readability, click comfort, or rendered correctness.

No build or runtime tests were needed for this documentation-only pass. Actual
implementation should verify meaningful undo gestures, preview restoration,
selection continuity, and save/reload behavior alongside in-game layout checks.
