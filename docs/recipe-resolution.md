# Recipe selection and composition contract

Status: implemented and checked offline on 2026-09-23. The
[implementation checkpoint](checkpoints/recipe-resolution-2026-09-23.md)
records native, sanitizer, Windows and targeted analysis evidence. Rendered
acceptance remains pending; the alpha preparation plan owns that work.

The central rule is: **identity overrides a definition; matching keys select
independent recipes; merge settings compose their contributions.** Keys are
not exclusively owned by one recipe.

## Definitions and file precedence

A recipe's identity is its filename stem, as used by the current loader.
Its display name, directory, matching keys, and merge mode do not establish
a separate identity. Two files with the same identity describe successive
definitions of one recipe, not two sampled alternatives.

Keep the existing file traversal policy: sort discovered paths, load files
outside `recipes/user/` first, then files under `recipes/user/`. Within each
partition retain the sorted order. A successfully decoded later definition
of the same identity replaces the entire earlier definition and takes the
later file's precedence. It must not retain the earlier record's position.

For example, loading `A`, then `B`, then `user/A` produces the surviving order
`B`, `user/A`. It does not produce `user/A`, `B`.

Keep existing validation behavior: a file that cannot produce a recipe is
reported and skipped; a decoded recipe with recipe-level errors can replace
the earlier definition but remains held back from application. Report that
state rather than silently applying the overridden definition. Valid row
subsets continue to follow the existing row-diagnostic policy.

Saving or editing an already loaded recipe does not itself change its
precedence. A later reload uses the resulting file traversal order. Generated
imports retain their explicit insertion order; this change must not invent
duplicate imports for keys already covered by authored recipes.

## Matching and fallback

Evaluate each surviving, applicable recipe independently against a worn
piece. Several matching keys on one recipe still select that recipe once.
Two different recipe identities may both match an identical key, regardless
of merge mode. Do not emit an ownership warning for that overlap.

Every `keyword` entry is a required condition: the worn armor must contain all
listed keywords. A missing or unresolved keyword excludes the recipe even when
an armor, enchantment, or magic-effect key matches. Repeated keyword entries do
not require repeated keywords on the armor.

When non-keyword entries exist, at least one of them must also match. They remain
alternatives. When only keyword entries exist, satisfying all of them selects the
recipe. An empty key list never selects a recipe.

`magicEffect` matches any valid base effect in the worn item's effective
enchantment, including secondary effects. Duplicate base effects are collected
once. `effectShader` matches the shader of any valid enchantment effect, using
that effect's enchant visuals shader, then its enchant shader. Neither key
inspects active spells on the wearer.

When the winning key is `magicEffect` or `effectShader`, the `enchantment`
signal's magnitude and cost come from that matching effect. If several effect
entries match the winning key, use the highest-cost matching entry, retaining
the first on a tie. A missing selected effect produces zero. Other winning key
kinds identify no particular effect and use the enchantment's costliest effect.
Evaluation state and carried clocks distinguish selected effect keys, so one
recipe can safely select different effects on different placements.

Choose the strongest matching non-keyword entry by the default priorities below;
ties retain the first matching entry in recipe order. Keyword-only recipes report
their first keyword and default to priority 20. Keywords used as conditions do not
raise a material or default selector's priority. An explicit recipe
priority changes composition precedence, not which key is reported.

Preserve fallback behavior before sampling:

- An enchantment-derived match suppresses candidates whose selected key is
  `default`.
- A specific enchantment, magic-effect, or effect-shader match suppresses
  candidates whose selected key is `enchanted`.
- Armor, keyword, and material matches do not by themselves suppress either
  fallback. Fallback classification follows the selected matching key.

Keys match the worn piece; output selectors determine which of its geometries
receive each output. Selection occurs before output selectors and preparation.
A selected recipe whose outputs do not apply is not replaced by a different
sampled candidate. Preparation failure similarly reports failure without
rerolling or reviving contributions already displaced by replacement.

## Precedence

For each recipe placement, use the explicit integer priority when present;
otherwise use the selected matching key's default:

| Key | Default priority |
|---|---:|
| default | 0 |
| enchanted | 5 |
| material | 10 |
| keyword | 20 |
| armor | 30 |
| effectShader | 40 |
| enchantment | 50 |
| magicEffect | 60 |

Order contributions by ascending effective priority, then ascending surviving
definition load order. Later contributions have higher precedence. This same
ordering governs replacement, scalar ownership, and shell settings; iteration
order over geometries, instances, or slots must not supply a different tie-break.

Effective priority belongs to a placement. Sharing a recipe/enchantment's
signal state and clock across pieces does not share its placement priority.
If a recipe matches one piece at 10 and another at 60, the first remains at 10.

## Sampling

After matching and fallback filtering, collect all candidates with
`merge: "sampled"` into **one pool for that worn piece**. There are no separate
pools per key, priority, material slot, or output. Different IDs sharing a key
remain distinct alternatives. A candidate appears only once even if several
of its keys match.

Choose exactly one member of a nonempty pool. All non-sampled candidates
remain selected. The selected sampled recipe then composes like `stack`;
its explicit output replacement flags still apply.

The choice is deterministic from the actor's form ID and the pool's recipe
identities. Canonically order identities by their case-sensitive UTF-8 bytes,
then index that list using a fixed, explicitly implemented hash of the actor
form ID modulo pool size. Do not use implementation-defined `std::hash`,
random process seeds, addresses, priority, or file traversal order. The implementation uses 32-bit FNV-1a with offset basis `2166136261` and
prime `16777619`, over exactly four actor-form-ID bytes, least significant
byte first. Arithmetic wraps modulo 2^32. Golden vectors are:

| Actor ID | Hash |
|---|---|
| `00000000` | `4b95f515` |
| `00000001` | `fb69b604` |
| `00000014` | `0da8e9e1` |
| `12345678` | `a3649785` |
| `ffffffff` | `e3160fb1` |

With the same actor form ID and identity set, reordering files, changing
priority, editing output values, ticking, or reapplying must preserve the
choice. Adding/removing/renaming alternatives may change it. A load-order
change that changes the actor's actual form ID is outside that guarantee.
There is no persistent per-item choice or promise of an exactly even crowd.

All geometries of the same piece use the same selection. Two pieces on one
actor with the same pool choose the same alternative. Different actors may
choose different alternatives; collisions are expected.

Editor isolation and pinning remain explicit preview overrides. Normal
unisolated runtime views must report the actor's actual selected alternatives,
not resolve a multi-candidate pool with a default seed of zero.

## Surface composition and replacement

A surface target is one **geometry, surface, and slot**. Material emissive,
material diffuse, and shell emissive are three separate targets.

Group all selected outputs from one recipe placement that address the same
target. Keep their authored output order and each output's layer order.
Process recipe groups from lower to higher precedence:

- `stack` appends the whole group.
- `replace` clears preceding recipe groups on this target, then appends its
  own whole group.
- An explicit `replace: true` on any selected output requests the same clear
  for that output's target, even when the recipe is `stack` or `sampled`.
  It does not remove sibling outputs from its own recipe group.

A recipe only replaces targets to which it contributes a selected output.
No matching output means no replacement on that target. Later recipe groups
may still append after a replacing group. A later replacing group clears all
preceding groups on that target, including equal-priority groups that came
earlier by load order.

For each scalar property, the last surviving output explicitly specifying
that property owns it. An omitted property does not erase an earlier surviving
value. Within one group, a later output can therefore override an earlier
sibling's scalar while retaining both outputs' layer stacks.

Recipe/output replacement and layer `blend: "replace"` are different:
the former excludes recipe groups from the plan; the latter is a pixel blend
operation within the retained layer sequence.

One shared shell binding is used per geometry. Its shell settings come from
the highest-precedence recipe group contributing to any surviving shell slot.
Enumerating slots in another order must not change that owner. Other surviving
recipes may still contribute layers to the shell's slots.

## Lights

Lights use an explicit **actor-wide** replacement target. This preserves the
scope of the existing light planner; it is not a per-material-slot operation.
The existing one-light-output-per-recipe restriction remains in force.

Group eligible light contributions by recipe identity, retaining the separate
recipe/enchantment/effect-context instances needed for their signal inputs and placements.
For ordering this actor-wide group, use the maximum effective priority among
its contributing placements, followed by definition load order. This aggregate
is local to the light plan and never changes a surface placement's priority.

Both recipe `merge: "replace"` and the light output's `replace: true` clear
preceding light recipe groups, then retain the replacing group's contributions.
They do not clear surface contributions. A recipe without an eligible light
output cannot clear lights. A light group must have an eligible third-person
placement whose light selector matches to participate; an absent
or filtered-out group must not suppress another piece's lights.

Consequently a replacing light on gloves can suppress a lower-precedence
light contributed by boots on the same actor. This is intentional and must be
stated in author-facing replacement help. Bone placement, first-person rules,
and the number of physical nodes created by an output are not redesigned here.

## Observable results and diagnostics

Distinguish these outcomes: overridden definition, held-back definition,
nonmatching recipe, fallback suppressed, sampled out, selected recipe,
selector exclusion, replaced contribution, and preparation failure.

The editor's selected-piece view reports placement priority and merge order.
An actor-wide light summary may show aggregate light priority, but must not
present it as the piece's material priority. Replacement diagnostics identify
the winning recipe and affected target. Shared keys are normal candidates,
not a warning. Preview overrides must remain distinguishable from normal
selection. Existing load/edit/save diagnostics must continue to survive this
refactor; no report of selection or preparation implies verified rendered pixels.

## Acceptance examples

These examples guide native regressions and deferred in-game acceptance.
Unless specified otherwise, recipes have different IDs, match the
same piece, and have eligible outputs. Listed precedence runs low to high.

| Case | Expected result |
|---|---|
| Files load as `A`, `B`, `user/A` | One A definition survives, ordered after B. |
| A and B share the exact enchantment key, both stack | Both participate. |
| A and B share that key, both sampled | Both enter the pool; exactly one participates. |
| Sampled A/B plus ordinary C sharing that key | One of A/B plus C participates. C does not own the key exclusively. |
| Reorder A/B files or change their priorities | Sampling choice stays fixed for the same actor and pool. Composition order may change. |
| Keywords A and B, armor X; piece has X and only A | Nonmatching: armor identity cannot bypass B. |
| Keywords A and B, armor X; piece has A and B but not X | Nonmatching: keyword conditions do not replace the armor selector. |
| Keywords A and B only; piece has both | Select once at keyword priority 20. |
| Keyword A plus material selector; both match | Select at material priority 10; A is a condition. |
| Magic effect M appears as a secondary enchantment effect | Select the recipe; a valid specific match suppresses generic fallbacks. |
| Specific effect matches but a required keyword is missing | Nonmatching; does not suppress fallbacks or enter the sampled pool. |
| Same recipe matches several non-keyword entries | One candidate, strongest matching entry, no extra sampling weight. |
| A resolves at 10 on gloves and 60 on boots; B is 30 | Gloves compose A then B; boots compose B then A. Shared evaluation state does not alter this. |
| A/B/C target emissive; B replaces | B and C survive; A is displaced. |
| B replaces emissive while A also writes diffuse | A's diffuse survives. |
| B has two emissive outputs and recipe replace | Both B outputs survive in authored order. |
| Only B's second emissive output has output replace | Earlier recipe groups are cleared; both B outputs survive. |
| B's replacing output selector does not match this geometry | B does not clear that geometry's target. |
| Equal-priority A/B own different shell slots, B loaded later | B supplies shell settings regardless of slot enumeration. |
| A supplies fuzz color and weight; B specifies weight only, both stack | A owns color; B owns weight. |
| B replaces lights but has no surface output | Earlier light groups are cleared; surfaces are unchanged. |
| B replaces surfaces but has no eligible light output | Existing lights survive. |
| B contributes lights through two enchantment instances and replaces | Both B contributions survive; preceding light recipe groups are cleared. |
| The sampled winner has no matching output selector or fails preparation | Report the exclusion/failure; do not choose another candidate. |

## Compatibility and verification boundary

This is an intentional pre-alpha behavior change within recipe format 1;
the wire fields do not change. Keyword entries now form required conditions,
so recipes previously using keywords as alternatives become more restrictive;
use separate recipe identities for those alternatives. Magic-effect and
effect-shader keys now also match secondary enchantment effects, so existing
recipes may apply to more items. Their magnitude/cost signals follow the winning
effect key, which can change output values and split previously shared state. Mixed keyword/selector recipes derive priority and fallback classification
from their matching non-keyword selector. Review existing mixed-key examples
before accepting the release candidate. A differently named recipe sharing a key no
longer silently overrides another. To replace a definition, keep its identity.
To replace earlier contributions on selected targets, use merge/output replace.
To provide alternatives, give them distinct identities and sampled mode.

Existing fallback rules, selectors, validation, file safety, signal evaluation,
and resource lifetime guarantees remain required. This work does not promise
cross-load-order actor identity persistence or introduce per-item state.

Native tests must cover the examples, negative cases, and deterministic hash
vectors. Integration checks must cover loader precedence, runtime actor seeding,
piece diagnostics, editor previews, and light eligibility. Windows compilation
and sanitizer checks precede in-game verification of rendered composition,
shell settings, light replacement, save/reload stability, and restoration.
Those visual checks remain pending until Skyrim can run.
