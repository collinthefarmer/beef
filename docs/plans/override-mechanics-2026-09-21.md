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

Seeding the sample on the armour or item instead of the actor was
explored 2026-09-21 and set aside: Skyrim has no stable per-item id (two
actors in the same armour share one base `armor` form id), so the only
stable seeds are base form ids, and seeding on the armour/enchantment
form inverts the feature — a same-armour crowd becomes uniform, losing
the variety `sampled` exists to give. Item-intrinsic looks would suit a
unique artifact, but the actor seed is the crowd-variety case this
feature is for, so the seed stays the actor form id with no new field.

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
2. **Merge behaviour** — LANDED 2026-09-21. `replace` promotes to a
   recipe-level chain cut (`Merge.cpp`, the `Flagged` replace flag now
   ORs `overrideMode == kReplace`); `sampled` collapses the per-piece
   pool to one member in `Resolve` (`KeepOneSampled`, keyed by a seed
   parameter that the engine threads as the actor form id). Both are
   pure and native-tested (`tests/recipe/merge_tests.cpp`). The studio
   preview and the convenience `MatchActor` overload pass seed 0.
3. **The Recipes-page UI** (backlog 17): a choice widget on the recipe
   header (`RecipeHeaderForm`) and a readout of what wins on a contested
   slot. Follows the field's shape.

## Open, minor

- Whether `sampled` and `replace` interact when both appear in one
  piece's contest set (proposal: resolve the sample pool first, then the
  winner participates in the priority chain where `replace` cuts apply).
  Settle at increment 2.

## Backlog: stable per-item ids for item-intrinsic sampling

The `sampled` seed is the actor form id because Skyrim exposes no stable
per-item id — two actors in the same armour share one base `armor` form,
and an item's `ExtraDataList` (which distinguishes a physical instance)
is not a persistent identifier across saves. That ruled out
item-intrinsic sampling (a unique artifact whose look is stable across
whoever wears it, chosen once for the item).

Investigate whether a stable per-item id can be assigned and tracked, so
the seed could optionally key on the item instead of the actor:

- Where an id could live: a value written into the worn item's
  `ExtraDataList` (a custom `BSExtraData`, or an existing unique field),
  or an SKSE co-save map from an item handle to an assigned id.
- Persistence: it must survive save/reload, cell changes, and the item
  moving between inventories, or the "stable across wearers" promise
  breaks — the exact property `ExtraDataList` pointers lack today.
- Cost and safety: assigning on first sight, not leaking, and staying
  inert when absent (fall back to the actor seed). Engine-facing and
  memory-safety sensitive (rule 1), so it is a spike, not a quick add.

Payoff if it works: `sample: actor|item` becomes a real author choice
(the field explored 2026-09-21 and deferred), giving item-intrinsic
looks without losing the actor-seed crowd-variety default. Until then
the actor seed stands.
