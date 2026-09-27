Status: record. The design and landed implementation of the game-object
discovery service: the eight kinds, the request-driven keyed cache of
immutable catalogs, and the fold-in of the editor-ID index and the actor-value
path.

# Game Object Service — design, 2026-09-17

## Status — increment 1 landed (2026-09-17)

Built and green (full DLL build + every native suite). Regression-neutral;
no UI change yet.

- `engine/Tweaks.{h,cpp}` — the po3 editor-ID bridge (`EditorIdOf`,
  `TweaksEditorIdsAvailable`) lifted out of `EngineForms` into one named unit.
- `studio/GameObjects.{h,cpp}` — engine-free `GameObjectKind`,
  `GameObjectCandidate`, `GameObjectCatalog`, `CandidateMatches`,
  `GameObjectKindName`; native suite `tests/studio/gameobjects_tests.cpp`.
- `engine/GameObjectService.{h,cpp}` — per-kind builders (six form kinds +
  actor values + observed anim events), a mutex-guarded keyed cache of
  `shared_ptr<const GameObjectCatalog>`, `RebuildGameObjectCatalogs`,
  `GameObjectCatalogOf`, `AnimEventCatalogOf`, `NoteAnimEvent`,
  `ResolveEditorId`.
- Resolution folded: `RecipeStore::ResolveForm` now calls `ResolveEditorId`,
  load calls `RebuildGameObjectCatalogs`; the old `g_editorIds`/`IndexEditorIds`
  are gone. An index-only `TESObjectARMA` pass keeps resolution parity with the
  old set (ARMA is resolvable but not a discovery kind).
- Anim source hooked: `AnimationSink::ProcessEvent` calls `NoteAnimEvent`, so
  the anim-event catalog fills from tags the actor actually fires.

Decisions taken while building (were open questions 1-2): the AV-only fields
(`description`, `units`) do **not** ride `GameObjectCandidate`; they derive from
the value name engine-free, and live samples are a separate field — so the base
record stays lean and universal. Display fallback order is editor ID → full
name (via `TESFullName`) → form key; the source plugin is the `qualifier`.
No relevance cap yet (open question 3): catalogs publish all candidates and the
combo filters; revisit if a list reads too long in game.

## Status — increment 2 landed (2026-09-17)

Built and green (full DLL build, every native suite, `tools/layers.sh`).
Regression-sensitive: the actor-value wizard changed data source, so this wants
an in-game check. Not yet run in game.

- `Snapshot` now carries `catalogs` (array of `shared_ptr<const
  GameObjectCatalog>` by kind), `catalogEventActor`, and `actorValueSamples`;
  `actorInputs`/`actorInputActorID` are gone.
- `ActorInputInfo` split: `studio/InputCatalog` now holds `ActorValueSample`
  (name + live samples) and `ActorValueHelp` + `ActorValueHelpOf(name)` (the
  description/units derived engine-free from the name); `DescribeActorInput`,
  `InputSample`, `InputMatches` retired. `SampleAt` reads a sample by measure.
- `engine/InputCatalog` now builds `BuildActorValueSamples(actor)` from the
  published actor-value catalog (one name set), replacing
  `BuildActorInputCatalog`.
- `PublishSnapshot` publishes the actor-value catalog, the anim-event catalog
  for the watched actor, and the live samples every frame (all cheap:
  pointer + ~160 samples). Form-kind catalogs stay null until requested.
- `InputBrowser` runs on a local `ActorValueView` adapter over
  (catalog candidate + derived help + live sample); its filter matches identity
  and the derived meaning, preserving the old description search without
  putting help text on every candidate.
- `REFERENCE.md` updated: the service owns the reverse editor-id map and the
  catalogs; anim discovery is by observation.

## Status — increment 3 slice 1 landed (2026-09-17)

Built and green (full native suite + DLL). The always-published catalogs now
back searchable fields.

- `FormField` gained an optional `catalog` (GameObjectKind) marker.
- `menu/FormDraw.cpp` `DrawCatalogField`: an opener button beside the text
  field opens an anchored popup with a `LiveTextField` filter and the filtered
  candidates (`CandidateMatches`); selecting commits the candidate's value, and
  a typed value still commits through the field's own bind. `DrawFieldInput`
  branches to it when `catalog` is set and the snapshot/names are present.
- Wired to the **actor-value field** (`kActorValue`) and the **trigger event
  field** (`kAnimEvent`, with `hit.received`/`hit.dealt` seeded as the static
  built-in complement to the observed anim tags). Both catalogs publish every
  frame, so no request channel is needed for this slice.

## Status — increment 3 slice 2 landed (2026-09-18); GOS complete

The request channel proved unnecessary and was dropped. The six form catalogs
are already built at load (`RebuildGameObjectCatalogs` fills them for editor-id
resolution), so publishing them is `shared_ptr` pointer copies, not list builds.
`PublishSnapshot` now publishes every kind unconditionally (anim events per the
watched actor). No `Manager::RequestCatalogs`.

- Recipe-keys add (item 8): `DrawKeyAdd` searches the keyword, enchantment,
  magic-effect, effect-shader and armor catalogs **while a filter is typed**
  (capped at 40 results, "refine your search" past that), each result adding a
  `RecipeKey` of the matching kind; the piece's carried keys still show, and a
  typed value still commits as a keyword. Bounded work: catalogs are scanned
  only with a non-empty filter, only while the combo is open. The keys popup was
  already an inline collapsible table, so no restructure was needed.
- New-recipe modal (item 12): **dropped** at the user's direction.

The Game Object Service and every planned consumer except the retired item 12
are done: actor values, anim events, and form keys all discover from live game
data through one cached, published catalog set.

## The design

Status: open design. This is the architecture for UI backlog
item 4 (`docs/wip/ui-backlog-2026-09-16.md`), designed across every kind at once
so the shape is settled before the first commit. It supersedes the "static
first, events later" slicing floated in that backlog entry.

## What it is

A recipe references game objects by editor ID or event name in several places:
recipe keys (`RecipeKey`, `recipe/Recipe.h:82`), form references in selectors
(`SelectorClause`, `:102`), the enchantment/effect-shader signal inputs, and the
event a trigger signal fires on (`EventOrigin`, `:216`). Today each is a bare
text field: the author types an editor ID blind, and a typo is a warning at load
(`ResolveForm`, `engine/RecipeStore.cpp:158`) rather than something the UI could
have prevented.

The service enumerates the valid values from the **live loaded game data** and
publishes them as plain candidate records into the `Snapshot`, so a menu combo
offers a filterable, discoverable list instead of a blank field. It is the
`actorInputs` pattern (`studio/InputCatalog.h`, `engine/InputCatalog.cpp`,
`Snapshot::actorInputs`) widened from one kind to all of them.

Layering is unchanged from that pattern: the engine-free record lives in
`studio/`, the engine builder that reads `RE::` lives in `engine/`, the snapshot
carries the records, and `menu/` renders the combo.

## The eight kinds and where each is sourced

| Kind | Source | Candidate `value` |
|---|---|---|
| Actor value | `RE::ActorValueList` enum (`InputCatalog.cpp:25`) | enum name |
| Keyword | `GetFormArray<BGSKeyword>()` | editor ID |
| Enchantment | `GetFormArray<EnchantmentItem>()` | editor ID / form key |
| Effect shader | `GetFormArray<TESEffectShader>()` | editor ID / form key |
| Magic effect | `GetFormArray<EffectSetting>()` | editor ID / form key |
| Armor | `GetFormArray<TESObjectARMO>()` | editor ID / form key |
| Light | `GetFormArray<TESObjectLIGH>()` | editor ID / form key |
| Anim event | the watched actor's fired `anim.<tag>` events | event name |

The six form kinds are exactly the set `RecipeStore::IndexEditorIds`
(`engine/RecipeStore.cpp:123`) already walks for load-time resolution. The
service takes that pass over.

## The architecture, forced by doing all kinds at once

Designing every kind together surfaces two axes a static-forms-only design would
have missed, and both belong in the core rather than bolted on later.

**Axis 1 — cache key is `(kind, actor)`, not `kind`.** Form kinds are the same
for the whole session; their key is `(kind, 0)`. Anim events depend on the
watched actor's animation graph; their key is `(kind, actorID)`. One keyed cache
covers both; static kinds simply use actor 0.

**Axis 2 — the candidate list is separate from live per-actor data.** An actor
value carries live samples (current / base / max per measure) that change every
frame and differ per actor; the *list* of actor values does not. A cache of
immutable catalogs cannot hold the samples without going stale. So the catalog
holds only the candidate list, and the live samples stay a small per-frame field
alongside it. This is the refactor the fold-in asks for: today's `ActorInputInfo`
(`studio/InputCatalog.h:29`) bundles the descriptive list fields with the sample
array; it splits into a catalog record and a live sample record.

These two axes are why "all kinds at once" was the right call: a design tuned to
static forms would have baked in `kind`-only keys and an all-in-one record, and
both break the moment events and actor values arrive.

## Data types (engine-free, `studio/`)

```cpp
enum class GameObjectKind {
  kActorValue, kAnimEvent, kKeyword, kEnchantment,
  kEffectShader, kMagicEffect, kArmor, kLight,
};
inline constexpr std::size_t kGameObjectKindCount = 8;

struct GameObjectCandidate {
  std::string display;    // editor ID, else full name, else form key
  std::string value;      // what the field commits: editor ID or "0x..~plugin"
  std::string qualifier;  // source plugin, or graph, for disambiguation; may be empty
};

struct GameObjectCatalog {
  GameObjectKind kind;
  std::vector<GameObjectCandidate> candidates;
  bool truncated = false;   // a relevance cap dropped some; the combo says so
  std::string sourceNote;   // "keywords from N plugins"; names where these came from
};
```

Actor values need `label`, `description`, and `units` for the wizard's help
text. Those are three more strings, empty for the form kinds. Open question below
on whether they ride the base record or a per-kind extension; the samples do not
ride it either way.

The live actor-value sample record (the half of `ActorInputInfo` that is not
catalog data):

```cpp
struct ActorValueSample {
  std::string name;   // enum name, the join key to the catalog candidate
  std::array<std::optional<float>, kInputMeasures.size()> samples{};
};
```

## Request and cache mechanism

The menu already tells the engine what to build each frame through
`Manager::Watch` (`engine/ManagerSnapshot.cpp:390`), set under `snapshotLock_`,
and `BuildSnapshot` runs lock-free before the published copy is swapped in
(`:403`). The service rides the same channel:

1. A combo that opens flags the kinds it wants. The menu records them through a
   sibling setter, `Manager::RequestCatalogs(wanted, eventActor)`, under
   `snapshotLock_`, exactly as `Watch` records the watched piece.
2. `BuildSnapshot` reads the wanted set. For each wanted `(kind, actor)` not in
   the cache, it builds the catalog once and stores it as
   `shared_ptr<const GameObjectCatalog>`. It then attaches the cached pointers to
   the snapshot.
3. The snapshot carries only the pointers, so a frame that re-publishes a warm
   catalog copies a pointer, not thousands of candidate strings — the per-frame
   path stays lean (rule 3).

```cpp
// on Snapshot
std::array<std::shared_ptr<const GameObjectCatalog>, kGameObjectKindCount> catalogs{};
FormID catalogEventActor = 0;              // which actor the kAnimEvent catalog is for
std::vector<ActorValueSample> actorValueSamples;  // live, per frame, list-free
```

A null entry in `catalogs` means "not requested this frame". The menu reads
`snapshot.catalogs[static_cast<size_t>(kind)]`.

First open of a cold combo pays one build and shows the list on the next frame
(the watch window keeps the snapshot rebuilding while the menu is up). Warm after
that for the rest of the session.

**Invalidation.**
- Form kinds: never. `GetFormArray` is fixed after load.
- Anim events: rebuilt when `catalogEventActor` changes; a manual refresh
  affordance for the case where the actor gained graph events after first build.
- Actor-value samples are not cached; they are read fresh every frame, as
  `actorInputs` is today.

## Folding in the two existing paths

**`g_editorIds` (resolution) folds into the cache.** `RecipeStore` builds
`g_editorIds` at recipe load to resolve `FormRef` text to a `FormKey`. The
service's per-kind builder produces the same editor-ID → form-key mapping as a
by-product of enumerating candidates. One builder, one cache, two triggers: the
store requests the six form kinds at load (eager, so resolution keeps working
before any combo opens), the menu requests whatever combo opened (lazy), and the
keyed cache dedupes. The duplicate pass in `RecipeStore::IndexEditorIds` is
removed, not paralleled.

**`actorInputs` (actor values) is refactored, not kept.** `BuildActorInputCatalog`
splits: the descriptive list becomes the `kActorValue` catalog (cached,
actor-independent), and the sample read becomes `actorValueSamples` (live,
per frame, per watched actor). `ActorInputInfo` splits into a catalog record and
`ActorValueSample`. `Snapshot::actorInputs` / `actorInputActorID` retire in
favour of `catalogs[kActorValue]` + `actorValueSamples`. The `InputBrowser`
wizard joins the two by name where it shows live values.

## Anim-event discovery

The engine already sinks every animation event the watched actor fires:
`AnimationSink::ProcessEvent` (`engine/Events.cpp:94`) tags each as
`anim.<tag>` and queues it. Event discovery accumulates the distinct tags seen
per actor into the `kAnimEvent` catalog. This reuses live machinery and needs no
behavior-graph walking. It is incomplete by nature — it lists only events that
have fired since watching began — so the combo still accepts a typed value and
`sourceNote` says "events seen so far". Walking the `hkbBehaviorGraph` string
table for the complete set is the harder, more fragile alternative, deferred
unless the observed-set proves too thin in game.

## po3's Tweaks: no new dependency

The editor-ID mechanism the service needs already exists and already handles po3.
`EditorIdOf` (`engine/EngineForms.cpp:34`) reads the form's own editor ID, then
falls back to po3's Tweaks' `GetFormEditorID`, looked up at runtime via
`GetProcAddress` on the `po3_Tweaks` module (`TweaksLookup`, `:7`). No link, no
vendored header, no compile-time dependency; it is a soft runtime dependency the
plugin already detects (`TweaksEditorIdsAvailable`). The service reuses it as is.

Consequence to surface to the author: without po3's Tweaks loaded, editor IDs for
effect shaders, enchantments, magic effects, and armor are mostly empty in
memory, so those catalogs fall back to `GetFullName()` for `display` and the form
key for `value`, and read sparser. Keywords keep their editor IDs regardless.
This is already the documented behaviour of the resolution path; the service
inherits it.

## Consumers waiting on this

The record and combo API should let each drop in without rework:

- Item 8 — the recipe-keys table needs a keyword / enchantment / form combo.
- Item 12 — the new-recipe modal's key-value field needs the same combo.
- Item 11 — the signal wizard's event step needs the `kAnimEvent` catalog.
- Item 4 — the generic `FormRef` selector fields (`FormRef::From`,
  `recipe/Recipe.h:29`) supersede their blind editor-ID text with the form combo.

## Layering and files

- `studio/GameObjects.h` — `GameObjectKind`, `GameObjectCandidate`,
  `GameObjectCatalog`, `ActorValueSample`. Engine-free, native-tested.
- `engine/GameObjectService.{h,cpp}` — the per-kind `RE::` builders, the keyed
  cache, `RequestCatalogs`, and the load-time warm-up the store calls.
- `Snapshot` gains `catalogs`, `catalogEventActor`, `actorValueSamples`; loses
  `actorInputs`, `actorInputActorID`.
- `menu/` — the filterable combo over `snapshot.catalogs[kind]`, reusing the
  `ReferenceCombo` idiom, replacing the wizard's actor-value specialisation per
  item 4.

`tools/layers.sh` must allow `engine/GameObjectService` to include
`studio/GameObjects.h` and `engine/EngineForms.h`; `studio/GameObjects.h` stays
free of `engine/` and `RE::`.

## Sibling-mod actor values (Actor Value Generator)

The actor-value builder reads only the vanilla enum range `[0, kTotal)`. Actor
Value Generator (`ActorValueGenerator.dll`) gives its custom values
`RE::ActorValue` IDs at and beyond `kTotal` and patches in an enlarged
`ActorValueInfo` list, so the builder as written enumerates none of them, and
widening the loop past `kTotal` is unsafe when AVG is absent (out-of-range IDs).
Supporting them is a real integration, not a bound change. The runtime size is
not the lever: the engine's `ActorValueList` is a fixed `[kTotal]` array with no
count; AVG allocates a private larger vector and overwrites the engine's list
*pointer* (reloc 514139), so the extended length lives only in AVG's own
`avi_list.size()`. AVG's SKSE interface (`InterfaceVersion3`) exposes resolve
and delegate registration but no enumeration or count, so neither the engine nor
AVG's API yields the set of custom values or the size. Probing `GetActorValue`
past `kTotal` is out-of-bounds when AVG is absent and hits an ambiguous sentinel
when present.

AVG is name-first: its content references custom values through
`GetActorValueIDFromName(name)`, never by index, and evaluation of a typed name
already routes through AVG's engine hooks (functional values; adaptive values
compute through AVG delegates). So the clean discovery source is AVG's config
directory (`Data/SKSE/.../ActorValueData/*_AVG.toml`): scan the same TOMLs AVG
reads, harvest the declared names, list them as `kActorValue` candidates, and
let resolution go through AVG's hooked name lookup at use time. A file scan on a
stable format (like the recipe scan), safe whether or not AVG is loaded — the
worked example of the service's sibling-mod extension point.

## Open questions

1. **Extra AV fields on the base record or a side channel.** `label`,
   `description`, `units` matter only for actor values. Carry them on
   `GameObjectCandidate` (three usually-empty strings) or hang a per-kind detail
   off the catalog. Leaning toward the base record for v1; all strings, no
   illegal states.
2. **`display` vs `value` when the editor ID is absent.** Decide the exact
   fallback order for a form with no editor ID (full name? form key? both, with
   the key as `qualifier`?), so the combo reads consistently across kinds.
3. **Relevance caps.** Keyword and magic-effect arrays run large. Decide whether
   the builder caps candidates (and sets `truncated`) or publishes all and lets
   the menu's substring filter carry it. The snapshot cost is a pointer either
   way; the concern is the combo's own list length.
4. **Manual event refresh.** Where the refresh affordance lives, and whether the
   observed-tag set is enough in practice or the graph walk is needed for v1.
