# UI design principles

Agreed direction, 2026-09-13. Refine navigation and layout first, using workflows
to identify a common core before committing to panels or implementation details.
The [existing UI assessment](ui-assessment-2026-09-13.md) applies these principles
to the current source.

## Central principle

Surface information, show what it enables, and make applying it discoverable in
context. More information is useful when users can understand and act on it.

## Principles

1. **Keep information and action together.** A value, pattern, or material region
   should expose its meaning, relevant uses, and available actions nearby. Users
   should not have to remember information from one page to apply it on another.
2. **Support starting from either the appearance or its cause.** “Make this glow
   when stamina is empty” and “What can stamina control?” should converge on the
   same connection. Navigation must work from visible armor, effect properties,
   and driving inputs.
3. **Reveal relationships, not just inventories.** Show what drives a property,
   where a resource is used, and what an edit will affect, including shared uses.
4. **Make values understandable before making them adjustable.** Show meaning,
   units, live values, and useful ranges. Distinguish hard limits from typical or
   observed values. Pair sliders with precise entry; expose meaningful expression
   inputs alongside their formulas.
5. **Separate pattern, placement, and behavior while making them easy to connect.**
   Users should be able to preserve a pattern while changing its armor region,
   animation, or enchantment response. These are independently understandable
   aspects of one effect.
6. **Make auditioning a first-class activity.** Support triggering an event,
   simulating an input, holding a peak, scrubbing progress, and trying another
   enchantment context. Clearly distinguish temporary audition values from
   authored settings and live game inputs.
7. **Keep the visible result connected to the editing context.** Material overlays,
   mask previews, selection highlights, and contribution inspection should connect
   the armor's appearance to its controls. Preview mode and scope remain apparent.
8. **Offer useful starting points with an inspectable path to detail.** Common
   setups can introduce an effect without requiring expressions, while preserving
   access to their underlying controls and relationships.
9. **Preserve context and make exploration reversible.** Following references or
   editing a mask retains the originating armor, recipe, and property. Undo
   reflects meaningful gestures; temporary previews have an obvious return to
   normal. One slider drag should become one undo step.
10. **Let workflows determine the layout.** Give persistent space to context and
    frequent actions; reveal detail according to selection. Judge the layout by
    whether users can discover, connect, tune, and audition without losing place.

## Workflow reference

| User intent | What the workflow must make understandable and actionable |
| --- | --- |
| Tweak expression variables or source/mask properties with sliders | Find tunable inputs, understand their ranges, adjust precisely, see the result. |
| See the live material, possibly with unique colors per material | Identify a region, connect it to authored contributions, use that selection. |
| When the player gets hit, the armor should glow | Find an event, shape its response, connect glow strength, fire a test event. |
| Make glow travel down the spine | Establish placement and direction, tune the pattern, scrub progress, connect a driver. |
| See the armor at maximum glow | Hold a relevant value or coordinated effect state without rewriting the recipe. |
| Give glow a noise, lightning, or flame pattern | Choose a starting pattern, tune meaningful properties, place and animate it. |
| Try a liked pattern on this armor with health, stamina, or fire-resist enchantments | Preserve the pattern, audition existing enchantment responses, author or tune missing responses. |
| Discover which actor values can drive an effect | Browse supported inputs by meaning and see compatible uses. |
| Understand the expected range of a property | Distinguish defined bounds, typical values, and observed values; show units and measurement mode. |
| Glow when stamina is exhausted | Work backward from glow to stamina, define the condition, map its response, audition the boundary. |

## Distinctions to preserve

- Enchantment identity and live actor state are separate inputs.
- Being exhausted is a condition; becoming exhausted is an event.
- Pattern, placement, and behavior can vary independently.
- Tuning an authored value, mapping an input, and overriding a live input are
  different actions even when each uses a slider.
- Maximum glow could mean one property's maximum or a coordinated authored peak.
- Mesh/material assignments and texture-derived regions such as metal, leather,
  and cloth are different possible meanings of “material.” Their presentation
  and selection behavior remain to be resolved.

## Scope

These principles do not commit to a sidebar, node graph, unified Compose/Paint
page, separate 3D viewport, or particular pattern implementation. Procedural
lightning/flames, context simulation, and overlays are desired workflows whose
technical support still needs assessment. Preserve the format-1 contract and
existing project non-goals unless deliberately revisited; auditioning does not
implicitly authorize a general A/B or preset-system expansion.
