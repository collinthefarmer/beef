# The recipe format's generating set

Ten ideas generate the format. Eight are the model; two are the domain it
sits in.

1. A named row referenced as `@name`.
2. A value that varies per tick — a signal.
3. A value that varies per texel — a source.
4. An expression over `@refs`. A curve is its one-argument case; a
   constant is its no-reference case.
5. A probe: a number only the engine can supply.
6. State across ticks — what a pure expression cannot compute.
7. A firing: a discrete event with a payload and a lifetime.
8. A fold of maps into a slot.
9. Matching and merge order.
10. The shell: a second copy of the geometry this plugin owns.

## What is redundant

`constant` and `ramp` are expressions. `plugin` and `event` trigger
origins glob-match the same id on the same bus. `counter` and
`accumulate` are one idea under two names. `uv` and `distance` are bakes.
`materialClusters` is a derived material channel. `masks` is a namespace
`sources` already subsumes. `lerp` is byte-identical to `replace`.
`target` belongs in the slot name, and `light` has no slot or stack and
does not belong in `outputs` at all.

Kept on purpose: `pulse` (its phase integrates, so a varying period does
not jump), `noise` (no expression op computes it, and adding one lands in
the GPU interpreter too), `gradient` (a table, not a computation),
`curves` (a function, not a value), `keys` versus `selectors` (a fact
about a piece versus a fact about a geometry), and the 9 slots and 11
scalar fields, which name the PBR shader's own parameters.

## What fell out for free

`Param = float | Ref` with its Vec2 and Vec3 forms and three `Resolve`
overloads — about twenty lines — animates 45 of the roughly 73 numeric
fields in `Recipe.h`. The 28 that are not `Param` are the ones that would
change what gets allocated or baked. The format's animation is nearly
free; its capability is nearly all machinery, with little in between.

## The deltas

- One spelling rule: a row written as a number or array is a constant, as
  a string an expression, as an object the kind its single key names.
  This is already how `curves` and `masks` behave.
- `masks` merges into `sources`.
- `target` folds into a qualified slot name (`shell.emissive`).
- Lights move out of `outputs` into their own block, which removes a
  `oneOf` between two unrelated object shapes.
- `blend` defaults to `replace` and `opacity` to `1`, which the C++
  already does and only the schema demanded.

The canonical example goes from 94 lines to 79, and the removals are the
lines that carried no decision.

## What it does not buy

Term count moves from 34 to 33. Every kind that collapses is one an
author never has to learn to write a working recipe; they meet it only on
opening the schema. The format's cost to a first-time reader is dominated
by irreducible domain nouns, not by redundant kinds.

Where it does pay: roughly 110 sites across the six passes that each
enumerate a closed set by hand, and two `oneOf` branches out of the
schema. What shortens a reader's first hour is different — four
namespaces become three, one spelling rule replaces three conventions,
and slot names become self-describing.

## What is lost

- `ramp` becomes `lerp(0, 1, saturate(time / 2))`, which is worse for an
  author who is not a developer.
- A round trip through the menu collapses `"0.5 + 0.5"` to `1.0` unless
  the serializer keeps the original text for expression rows.
- A source used as a layer mask currently loses its scroll and tile,
  because the layer shader samples masks with no transform. This is the
  one place the merge costs work rather than saving it.

## Rejected

Collapsing `Layer.curve` into an expression source, and collapsing the
whole layer stack into one per-texel expression. Both trade a
straight-line kernel for interpreter passes on the per-texel path.

## Effort

Eight to eleven days. The first three items — the spelling rule, the
redundant kinds, folding `uv`/`distance`/`materialClusters` — are
independent of the risky two, which are the `masks` merge and the
qualified slots. The latter stops `Output` being a variant, which splits
`Contribution` — the same fix `clusters.md` wants for the type that
carries two index spaces.
