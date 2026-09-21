# Recipe override mechanics (2026-09-21)

Gate-5 item 9 and UI backlog item 17. A recipe gains author-set control
over how it combines with other recipes that contest the same worn
piece, replacing the store's silent first-loaded-owner-wins. This is a
**format-1 change**, so the record field lands before the format freeze;
the merge behaviour and the Recipes-page UI follow.

## The current model (two levels)

1. **Key ownership** (`engine/RecipeStore.cpp`): when two recipes carry
   the same key (`effectShader:EnchArmorMagickaFXS`), the first loaded
   owns it; a later recipe does not resolve by that key and is warned as
   unresolved. First-loaded, not author-controlled.
2. **Slot merge** (`recipe/Merge.cpp`): among recipes that do match a
   piece, their outputs on one slot form a priority-ordered `chain`
   (`SlotPlan.chain`); a per-output `replace` flag cuts the chain,
   dropping lower-priority work. So stacking and replace exist, but
   per-output and priority-ordered, never per-recipe and never sampled.

## Decided model (2026-09-21)

A per-recipe `override` field says how this recipe combines with the
**lower-priority** recipes that contest the same piece. Per recipe, not
per output (a recipe cannot sample half an effect). Default `stack` is
today's behaviour, so an un-annotated recipe is unchanged.

| Mechanic | Meaning |
|---|---|
| `stack` (default) | This recipe's outputs stack over the lower-priority contributions on each slot — the current merge. |
| `replace` | This recipe drops the lower-priority contributions on the slots it writes — the existing per-output `replace`, promoted to the whole recipe. |
| `sampled` | This recipe joins a per-piece **sample pool** with every other `sampled` recipe matching the piece; per actor exactly one pool member contributes, chosen by `hash(actor form id) % pool size`. Deterministic and stateless, so an NPC's effect is stable across save/reload and ticks while a crowd shows variety. Non-`sampled` recipes are unaffected. |
| `lerp` | Reserved, not in the first cut. Blending one recipe's composited slot result with the next by a factor needs a blend-factor field and cross-recipe result blending; the author's original ask marked it tentative. Add later if wanted. |

Sampling decided: **deterministic by actor form id** (stable, no stored
state). Scope decided: **per recipe**.

## Data

- `enum class OverrideMode { kStack, kReplace, kSampled, kLerp };` in
  `recipe/Recipe.h`, `kLerp` last and inert until implemented. Word
  table `kOverrideModes` in `Words.h` (`stack`/`replace`/`sampled`/
  `lerp`), `static_assert(Complete(...))`.
- `Recipe` gains `OverrideMode overrideMode = OverrideMode::kStack;`.
- Format-1 field at the recipe top level:
  `"override": "stack|replace|sampled|lerp"`, omitted at the default.

## Increments

1. **The format field** (this increment, engine-free, native-tested):
   the enum + word table, the `Recipe` field, parse (`RecipeRead.cpp`),
   serialize omit-at-default (`RecipeWrite.cpp`), schema enum, the
   schema-agreement test, and a round-trip test. No behaviour yet —
   `stack` is the only mechanic the merge honours, which is the current
   behaviour, so the plugin is unchanged. This unblocks the freeze.
2. **Merge behaviour** (engine-free planners + resolve, native-tested):
   `replace` promotes to a recipe-level chain cut; `sampled` builds the
   per-piece pool and picks one member by the actor-id hash. Both are
   pure over the placement inputs, so they test without the engine.
3. **The Recipes-page UI** (backlog 17): a choice widget on the recipe
   header (`RecipeHeaderForm`) and a readout of what wins on a contested
   slot. Follows the field's shape.

## Open, minor

- Whether `sampled` and `replace` interact when both appear in one
  piece's contest set (proposal: resolve the sample pool first, then the
  winner participates in the priority chain where `replace` cuts apply).
  Settle at increment 2.
