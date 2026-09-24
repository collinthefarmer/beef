# planners/

The [recipe resolution contract](../recipe-resolution.md) defines placement
priority, replacement groups, and shell/light precedence. Offline verification
and deferred in-game acceptance are tracked in the alpha plan.

The pure decision layer between a **recipe** and the engine. It matches
recipes against an actor's **geometries**, merges the matches into
per-geometry and per-light **plans**, and classifies those plans for
rendering: animated or static, and which surfaces need a **binding**. It is
engine-free: it compiles natively and is unit-tested through
`ctest --preset native`. It depends on `recipe/` and `mesh/` and on nothing
above. Every wave-3 shell (`engine/Manager`, `render/Compositor`,
`render/Binding`) is a thin adapter that owns the real `RE::` handles and
calls these functions with value records.

## What it owns

The **actor plan**: which geometries an actor has, which recipes matched
each one, and which **instance** (one recipe, one enchantment) backs each
match. From the plan it derives:

- a **geometry placement** — the **slot** stacks and **light** contributions
  one geometry's matches compose to, through `recipe/Merge`;
- a **stack** classification — `StackPlan` marks each stack link and slot
  animated or static;
- a binding decision — `BindingPlan` states which surface bindings a
  geometry needs and which **contributor** owns the **shell**.

It also owns small engine-adjacent primitives with no `RE::` dependency: the
**lease**-based `TargetPool` for render-target slots, `ConsumptionLeases`
and `TextureLeases` for GPU resource lifetime, `OwnedState` for restorable
field groups, `TransformStorage` for decoding an engine transform array
index, the texture-identity helpers (`ImageCacheKey`,
`IsPlaceholderExtent`) that `render/SourceSampling` builds on, the
cross-actor `ResourceCache` the render **Compositor** shares one rendered
**target** through, and the pure `EvictionFor` distance decision the engine
`Manager` sweeps far actors with.

## Data

### Handles

`ActorPlan.h` declares five handle types, each an `enum class : std::size_t`
over its own index space. The distinct types stop a caller from indexing one
table with another table's handle. No handle wraps a pointer.

| Handle | Indexes |
|---|---|
| `GeometryId` | A row of `ActorPlan::geometries`. |
| `InstanceId` | A row of `ActorPlan::instances`. |
| `PlacementId` | A row of `ActorPlan::placements`. |
| `RecipeId` | A row of the **recipe** store the caller passes as `std::span<const Recipe>`, never a stored pointer. |
| `OutputId` | A row of the matched recipe's `outputs`. |

### The actor plan

`ActorPlan.h` declares the **plan** and its rows. `ActorPlan` holds what the
planners know about one actor as three tables of value rows, and the rows
refer to each other by handle. `MatchActor` builds the plan, and the query
functions in `ActorPlan.cpp` read it.

| Table / row | Description |
|---|---|
| `Geometry` | One mesh on the actor: its `GeometryIdentity`, the armor's `WornPiece` match keys, and the `firstPerson` and `lost` flags. |
| `Instance` | One **recipe** applied to one enchantment: a `RecipeId` and an optional enchantment `FormKey`; it shares evaluation state, never composition priority. |
| `Placement` | One match. It joins an `InstanceId` to a `GeometryId`, records the `RecipeKey` and effective placement `priority`, and lists an `OutputPlacement` (`OutputId`, `selected`, `problem`) per output. |
| `PieceMatch` | The match view for one armor **piece**: an instance index, the `RecipeKey`, and the `priority`. `MatchesForPiece` builds it, and `engine/ManagerSnapshot.cpp` turns it into snapshot rows. |

### Geometry and light plans

`ActorPlanning.h` declares the two merged-**plan** records and the resolver
hook. Each record pairs a plan merged by `recipe/Merge` with the `PlacedRecipe`
rows it was merged from, and its `sources` vector aligns row for row with
`placed`, so a consumer can trace a merged contribution back to its plan row.

| Type | Description |
|---|---|
| `GeometryPlacementPlan` | One **geometry**'s `PlacedRecipe` rows, the `PlacementId` each row came from, and the merged `GeometryPlan`. |
| `ActorLightPlan` | The actor's `PlacedRecipe` rows, the `InstanceId` each row came from, and the merged `LightPlan`. |
| `RecipeResolver` | The caller-supplied `(Geometry, GeometryId) -> std::vector<ResolvedRecipe>` function. `MatchActor` calls it once per geometry to find that geometry's recipes. |

`PlanGeometryPlacement` reads priority from each placement and load order from
its recipe-store index. `PlanActorLights` admits only live third-person
placements whose light selector matches (`LightEligible`), after preview
output filtering. Ineligible instances retain source indices but contribute
no recipe to the light planner. Light groups aggregate eligible placement
priority by recipe identity; that aggregate never alters surface priority.
`Manager::PlaceLight` reuses `LightEligible` for the actual geometry inputs.

### Stack classification

`StackPlan.h` declares the animation classification of one **geometry**'s
**slot** **stacks**. `PlanStacks` computes it from the `PlacedRecipe` rows and
the merged `GeometryPlan`. A link is animated when its own output animates or
when any link below it in the chain does.

| Type | Description |
|---|---|
| `StackLink` | One `SlotContribution` in a chain, with `selfAnimated` (this link's own output animates) and `animated` (this link or a link below it animates). |
| `SlotStackPlan` | One slot's chain of `StackLink`s, the `Surface` and `Slot` it belongs to, and whether the whole stack animates. |
| `GeometryStackPlan` | Every slot on one geometry: one `SlotStackPlan` per `SlotPlan` in the `GeometryPlan`. |

### Binding decision

`BindingPlan.h` declares the one record `PlanBinding` produces for a
**geometry**. The engine shells read it to decide which surface **bindings**
to install and which **contributor**'s **shell** to keep.

| Type | Description |
|---|---|
| `BindingPlan` | Whether the geometry needs a material binding, whether it needs a shell binding, and the `SlotContributor` that owns the shell. `shellOwner` follows priority then definition load order among surviving shell contributions, independently of slot enumeration. |

### Distance eviction

The engine `Manager::SweepEviction` drops a far actor's effects and restores
them as it comes back. `EvictionFor` is the pure decision that sweep runs per
actor; it holds no state. A hysteresis band stops an actor at the boundary
from thrashing between the two states.

| Type | Description |
|---|---|
| `EvictionAction` | The decision for one actor: `kNone`, `kEvict`, or `kRestore`. |
| `EvictionFor(distance, evictDistance, applied)` | A `constexpr` function. It evicts an applied actor past `evictDistance`, restores an evicted one only once it closes inside 80% of `evictDistance`, and returns `kNone` in the band between. An `evictDistance <= 0` disables eviction. |

### Resource lifetime

These four types tie a GPU resource's release to an observed condition, not to
elapsed time. None of them names an engine type: each takes its resource as a
template parameter or hands out a plain index, so the engine layers
instantiate them with the real GPU types.

| Type | Declared in | Description |
|---|---|---|
| `TargetPool` | `TargetPool.h` | Hands out render-**target** indices as `shared_ptr<const std::size_t>` **leases**. `Acquire` reuses an index once its lease expires, and returns an empty pointer when every index is held. |
| `ConsumptionLeases<Resource>`, `Ticket` | `ConsumptionLeases.h` | `Retain` holds a `shared_ptr` to the resource until the consumer calls `Ticket::Consumed`. It refuses new tickets when the pending list reaches the constructed limit. |
| `TextureLeases<Target>`, `TextureLeaseLookup<Target>` | `TextureLeases.h` | `Register` files a generated texture under its **presenter** address and generation. `Retain` returns a `TextureLeaseLookup` carrying the generation and the target, if the target is still alive. |
| `OwnedState<State>` | `OwnedState.h` | Remembers a field group's original value and the value last written. `Restore` returns the original only while the current value still equals the last write. |

### Cross-actor resource cache

The render **Compositor** shares one rendered **target** — a stack, cluster
map, mask, or bake — across same-armor actors through this cache. The cache
owns nothing: each entry is a `std::weak_ptr`, so a target frees when its last
holder drops it, which returns the target's **presenter** slot. It keys by a
caller string, unlike the `TextureLeases<Target>` above (an engine presenter
address) and the owning `RecipeTextureCache<T>` below (per-geometry
`shared_ptr`).

| Type | Description |
|---|---|
| `SharedResource<T>` | The result of an `Adopt`: the `shared_ptr` `value` and an `adopted` flag, true when an existing live entry was reused. |
| `ResourceCache<T>` | A `std::unordered_map<std::string, std::weak_ptr<T>>`. `Adopt(key, make)` returns a live entry, or runs `make`, publishes the result under the key, and returns it. `Clear` drops every entry; `LiveCount` counts the unexpired ones. |

### Texture keys and identity

These types name a texture by value, so caches and decoders work without an
engine handle. `render/SourceSampling` builds on the identity helpers.

| Type | Declared in | Description |
|---|---|---|
| `RecipeTextureKey`, `RecipeTextureCache<T>` | `RecipeTextureCache.h` | The key is a **recipe** name, a texture name, and a pixel count from `TextureSize`. The cache is a `std::map` from that key to a `shared_ptr`; `FindRecipeTexture` looks up one size, and `LargestRecipeTexture` returns the biggest stored size. |
| `ImageCacheKey`, `IsPlaceholderExtent` | `TextureIdentity.h` | `ImageCacheKey` lower-cases a path into a cache key. `IsPlaceholderExtent` reports whether a width or height is at or under `kPlaceholderTextureExtent` (4 texels). |
| `TransformStorage`, `TransformStorageIndex` | `TransformStorage.h` | `TransformStorage` records an engine transform array's base address, `count`, `stride`, and `offset`. `TransformStorageIndex` decodes a pointer into a row index, or nothing when the pointer is outside the array. |

## How a placement flows

```
Geometry[]                                            (mesh identity, per actor)
Recipe[] (the store)
  │
  ▼
MatchActor(geometries, store[, resolver])              ActorPlanning.cpp
  │  resolver: (Geometry, GeometryId) -> ResolvedRecipe[]  (default: Resolve on the piece's keys, recipe/Resolve.cpp)
  │  per match: InstanceFor dedups by RecipeId + enchantment FormKey
  ▼
ActorPlan{ geometries, instances, placements }         ActorPlan.h
  │  queries: PlacementsOfGeometry, PlacementsOfInstance,
  │           MatchesForPiece, FindInstance                 ActorPlan.cpp
  ▼
PlanGeometryPlacement(plan, store, geometryId, filter) ActorPlanning.cpp
  │  gathers this geometry's placements -> PlacedRecipe rows
  │  PlanGeometry(placed) -> GeometryPlan                   recipe/Merge.cpp
  ▼
GeometryPlacementPlan{ placed, sources, plan }
  │
  ├─▶ PlanStacks(placed, plan.plan) -> GeometryStackPlan     StackPlan.cpp
  │      LinkSelfAnimated reads IsAnimated(recipe, output)   recipe/Vocabulary.cpp
  │
  └─▶ PlanBinding(placed, plan.plan) -> BindingPlan          BindingPlan.cpp
         highest-priority Surface::kShell contribution -> shellOwner

PlanActorLights(plan, store, filter) -> ActorLightPlan       ActorPlanning.cpp
  (one PlacedRecipe per instance; PlanLights -> LightPlan)   recipe/Merge.cpp
```

The shell layers (`engine/Manager`, `render/Compositor`, `render/Binding`)
read `GeometryStackPlan`, `BindingPlan`, and `ActorLightPlan` to decide what
to render and bind. No planner takes or stores an `RE::` pointer.

## The files

| File | What it owns |
|---|---|
| `ActorPlan.h` / `.cpp` | The three-table `ActorPlan` model and its row and query functions: `GeometryAt`, `FindInstance`, `PlacementsOfGeometry`, `MatchesForPiece`, `ThirdPersonGeometriesOfInstance`, `RecipesOfInactiveInstances`. |
| `ActorPlanning.h` / `.cpp` | `MatchActor` (geometries + recipes to `ActorPlan`), `PlanGeometryPlacement`, `PlanActorLights`. |
| `StackPlan.h` / `.cpp` | `PlanStacks`: marks each `SlotPlan` chain link and slot animated or static. |
| `BindingPlan.h` / `.cpp` | `PlanBinding`: which surface bindings a geometry needs, and its shell owner. |
| `TargetPool.h` | The lease-based render-target index pool. `Acquire` returns a `shared_ptr` lease. |
| `ConsumptionLeases.h` | `ConsumptionLeases<Resource>`/`Ticket`: release on consumer acknowledgment, not on elapsed ticks. |
| `TextureLeases.h` | `TextureLeases<Target>`: register and retain a generated texture by presenter address and generation. |
| `OwnedState.h` | `OwnedState<State>`: restore a coupled field group only while every value still equals the last write. |
| `ResourceCache.h` | `SharedResource<T>` and `ResourceCache<T>`: a weak-keyed cross-actor cache; `Adopt` reuses a live entry or makes and publishes one. |
| `Eviction.h` | `EvictionAction` and `EvictionFor`: the pure distance-eviction decision with an 80% hysteresis band. |
| `RecipeTextureCache.h` | `RecipeTextureKey`, `RecipeTextureCache<T>`, `FindRecipeTexture`, `LargestRecipeTexture`. |
| `TextureIdentity.h` / `.cpp` | `ImageCacheKey`, `IsPlaceholderExtent`, `kPlaceholderTextureExtent`. |
| `TransformStorage.h` / `.cpp` | The `TransformStorage` layout record and the `TransformStorageIndex` pointer-to-row decode. |

## See also

- `REFERENCE.md` → *planners* — the three-table `ActorPlan` design decision,
  the `StackPlan`/`BindingPlan` classification rules, the opaque handle
  spaces, and why row projections stay out of this module.
- `REFERENCE.md` → *Generated texture references* — the `TextureRef`
  lifetime model `TargetPool` and `TextureLeases` support.
- `docs/conventions.md` → *Runtime identities and editor commits* — how
  `ActorPlan::geometries` lines up with `LiveActor::pieces`, and what
  `PlanGeometryPlacement`/`PlanActorLights` do and do not do.
- `docs/conventions.md` → *Component ownership* — where the engine shells
  wrap these plans (`Manager`, `TextureLab`/`RenderTargetPool`,
  `PbrMaterial`).
