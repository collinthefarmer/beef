# UI v2: core editor checkpoint

Status: implementation integrated; automated validation in progress. No new
in-game pass claimed. The user accepted the earlier framework checkpoint.

## Scope

This integration covers stages 1-3 of the [implementation plan](../ui-v2-implementation-plan.md).
Three agents implemented preview/relationship/pattern work, gestures/input/graphs,
and document/navigation/mask work. The coordinator integrated field controls,
expression editing, status reporting, and validation.

The prior dirty tree was preserved. A second-wave source/test baseline is under
`/tmp/ui-v2-wave2-baseline`. The proposal remains frozen.

## What changed

- Browse and edit a recipe without applying it to armor. Unmatched documents keep
  editable definitions and show unavailable live values honestly.
- Follow drivers and consumers to their properties; search the navigator and pin
  a preview subject. Pins resolve textures from current snapshots.
- Tune scalar properties with sliders and exact input. A drag creates one undo
  step; Escape cancels. Unknown ranges require user-chosen slider limits.
- Tune or promote one expression number. The parser identifies the occurrence;
  revision checks refuse stale edits instead of changing a different operand.
- Connect supported actor values, current/max fractions, exhaustion conditions,
  or a received-hit response in one validated edit batch.
- Inspect response graphs for supported pulse, ramp, and trigger definitions.
  Axes show elapsed seconds and output; referenced inputs are held at displayed
  values. Unsupported formulas retain their text controls.
- Leave a mask draft, edit elsewhere, then Resume. Keep can assign the mask to
  its layer atomically. Save excludes the draft. Changed destinations are refused
  or visibly invalidated; pending Keep blocks competing mutations.
- Choose supported mask patterns using existing terms, controls, and previews.
- Scrolling image-source thumbnails now apply source sampling transforms. They
  show the sampled source before layer color, opacity, normalization, and blend.

## Deliberate boundaries

Clock controls are still global. Scoped value holds, material-region overlays,
new flame/lightning generators, and coordinated peaks belong to stages 4-5.
The pattern chooser presents capabilities that already exist.

This is a texture preview, not an embedded armor renderer. The worn armor remains
the live result. Preview ownership continues through the existing retained
texture and draw-ticket path.

## Validation

Final build identity, test results, and installation status will be recorded here
after the integrated source checks finish.

## Complete-editor game check

Use a disposable copy of a working recipe for destructive edits.

1. **Documents:** create a recipe with an explicit key, select an unmatched file,
   edit an output/layer/source, Save, and reload. Browsing alone must not change
   the worn armor. Check no-geometry labels and two window sizes/UI scales.
2. **Navigation:** search for a source, follow its driver, follow a Used-by link,
   and use Back. Check the property/component, scroll, and exact output. Pin a
   preview, tune another property, then change recipe and reload a save.
3. **Tuning:** drag a scalar for several seconds, release, and Undo once. Redo,
   then cancel another drag with Escape. Try exact input, an invalid value,
   and a custom slider range. Close Studio during a drag and reopen it.
4. **Expressions:** edit one of two identical numbers, then promote that number
   to a signal. The other occurrence must stay unchanged; promotion must preserve
   the effect. Check driver/consumer links and Undo. Confirm refusals are visible.
5. **Gameplay inputs:** connect stamina exhaustion to glow opacity; check that it
   remains on while depleted. Try current/max and the received-hit helper. Fire
   the hit response and inspect a supported response graph.
6. **Masks:** open a layer mask task, add a supported pattern, change preview
   geometry, and edit another inspector. Save excludes the draft. Resume, Keep
   into the original layer, Save, and reload. Change/reorder the destination on a
   disposable recipe and verify stale assignment cannot edit a neighbor.
7. **Animation:** inspect a scrolling imported fill such as `VaporTile01`.
   Compare scroll, tiling, mirror/transpose, and Freeze/Step with its output.
   Compare two sources using the same image with different sampling settings.
8. **Restoration:** solo/mute, freeze/scrub, then Return to live. Repeat preview
   browsing, mask Keep/Discard, Studio close, and save-load while textures are
   visible. Check for stale images, stuck pending controls, or changed armor.

For failures, report the recipe, geometry, action, and visible result. Check the
startup build identity and `edit refused`/`save failed` records when present.
Absence of log errors does not establish visual correctness.
