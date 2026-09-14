# Engine-facing type survey — 2026-09-12

Status: history. The follow-ups it proposed were taken up by the 2026-09-12 and
2026-09-13 checkpoints listed in `docs/README.md`. Names and paths here predate
the critique remediation of 2026-09-14 (Plan C's file moves and Plan D's
renames); `docs/README.md` indexes the current set.

This surveys the active adapters after the `PbrMaterial`, `RenderPass`, and
`SessionQueue` changes. It proposes follow-up work; no runtime code changed during
this survey. Findings describe the current source, not additional crash causes
established from the supplied log.

The strongest next steps are settings publication, GPU resource ownership, and
scoped access to renderer metadata and mesh buffers. Several later opportunities
replace existing, working cleanup conventions with types that make those
conventions harder to bypass.

## Priorities

| Order | Boundary | Suggested type or extension | Rule to enforce |
| --- | --- | --- | --- |
| First | Menu settings → game thread | `SettingsPublication` | Readers hold a stable settings value; editing never mutates that value. |
| First | GPU initialization and cleanup | `GpuResources`, complete pipeline records; tighten `Lookup` | Every acquired COM reference has an owner; readiness means all required resources exist. |
| First | Engine texture metadata | `RendererAccess`, `TextureView`, `PresenterBinding` | Inspect or replace renderer metadata only inside the appropriate access scope. |
| First | Engine mesh → native mesh data | `MeshReadSnapshot`; extend `MeshIdentity` | Counts, descriptors, identity, and copied bytes describe the same captured mesh. |
| Next | Snapshot/preview → UI draw commands | `SnapshotTextureId`, `UiFrameResources` | A draw command retains its resource through consumption by the backend. |
| Next | Animation and light registration | `AnimationSubscription`, `RegisteredLight` | Teardown addresses the source or scene used at registration. |
| Next | Shell material and skin ownership | `ShellMaterial`, `PrivateSkinPose` | Only the selected material mode and privately owned pose data can be written. |
| Next | Restoring engine fields | `FieldOverride`, `FlagOverride` inside `SlotWriter` | Restore a field only while its current value still matches our last write. |
| Next | Published recipes → live effects | `RuntimeRecipeRevision`, constructed live instances | Runtime references and compiled state belong to one retained recipe revision. |
| Next | Engine execution context | `GameThreadContext`, scoped actor access | Engine operations require the correct execution context and an owning actor resolution. |
| Next | Native values → GPU input layout | `PackedGpuProgram`, checked upload records | Counts, indices, byte widths, and constant-buffer layout agree before submission. |
| Next | Engine callbacks and failed submission | Callback adapter, `RefreshReservation`, explicit startup state | Plugin exceptions are contained only after cleanup; abandoned reservations cannot suppress future work. |
| Next | Numeric values → engine fields | `LightParameters`, `ShellPose`, checked material writes | Values and derived arithmetic are finite and satisfy the destination field's contract before mutation. |
| Planned feature | GPU readback | `PendingReadback` → ready pixels/bytes | Submission, readiness, mapping, cancellation, and result ownership are explicit. |

“First” is implementation order, not a crash-severity classification.

## 1. Publish settings instead of exposing mutable shared storage

**Evidence:** [SettingsFile.cpp](../../src/SettingsFile.cpp#L124) returns const and
mutable references to the same `g_settings`, and `SetSettings` assigns it directly.
[SetupPage.cpp](../../src/menu/SetupPage.cpp#L239) obtains that mutable reference;
its `Widget` passes member addresses to ImGui. Game-side
[ManagerTick.cpp](../../src/engine/ManagerTick.cpp#L255) and
[ManagerApply.cpp](../../src/engine/ManagerApply.cpp#L456) read it concurrently under
the render-thread/game-thread split recorded in `REFERENCE.md`. There is no
publication lock or immutable ownership here. Posting a later reapply does not
synchronize the preceding direct writes.

**Proposal:** `SettingsPublication` owns synchronization and publishes immutable
settings snapshots. The menu edits a local draft, then publishes a checked value
or sends a value command to the game thread. Each tick/apply operation retains one
snapshot for its duration. Remove the writable-reference API. A small copied
value under a mutex is sufficient; a shared immutable object is another option.
Making individual fields atomic would still permit inconsistent combinations.

**Validation:** Native tests for old snapshots surviving publication and a whole
operation seeing one revision; ThreadSanitizer for concurrent publication/read
where supported. Validate tick-rate bounds at publication as well as parsing:
`Settings::TickIntervalMS` divides by the publicly writable `animationFPS`.

## 2. Own GPU resources and construct complete capabilities

**Evidence:** [RuntimeTexturesLab.cpp](../../src/render/RuntimeTexturesLab.cpp#L95)
has a default `TextureLab` destructor, while `Init`/`CompileShaders` acquire shader,
buffer, sampler, and pipeline-state references into raw members. There is no
matching cleanup for most of those members, including resources created before
an initialization failure. This is retained initialization/process-lifetime
storage, not evidence of a leak on every frame.

[Lookup](../../src/render/RuntimeTextures.h#L68) releases its raw texture and SRV
in its destructor but leaves implicit copying available. Current callers share
`shared_ptr<Lookup>`; an accidental value copy would duplicate release
responsibility. `RenderTarget` is already noncopyable through its `unique_ptr`
member, but its construction fields remain public and individually mutable.

**Proposal:** Use the existing `REX::W32::ComPtr` for owned COM references rather
than inventing another COM pointer. Build `GpuResources` locally and publish it
only after the required pipeline is complete. Represent optional bake,
interpreter, ripple, and classification capabilities with complete records of
their required resources. Give `Lookup` an explicit ownership policy and a
factory; make target resource fields private. Keep the engine device/context's
borrowed or retained lifetime policy explicit and separate from objects we create.

**Validation:** Compile-time copy/move restrictions; fail each creation step and
verify that previously acquired references are released once. Use a narrow fake
resource factory for cleanup tests, then D3D diagnostics in the integration run.

## 3. Extend renderer access beyond drawing

**Evidence:** The draw paths now use `RenderPass`, but `DataOf`/renderer-data casts
remain in the pool, readback, and compositor code. Both
[CompositorBake.cpp](../../src/render/CompositorBake.cpp#L18) and
[CompositorSource.cpp](../../src/render/CompositorSource.cpp#L31) inspect
`rendererTexture` and `resourceView` before calling the locked `ExtentOf` query.
[RenderTargetPool.cpp](../../src/render/RenderTargetPool.cpp#L70) replaces presenter
metadata; [target destruction](../../src/render/RuntimeTexturesLab.cpp#L70) restores
it. Neither operation locally requires the renderer lock. Pool deleters can also
destroy a returned target on whichever thread releases its last owner.

**Proposal:** Extract a small nonmovable `RendererAccess` shared by drawing,
readback, and metadata operations. `RenderPass` builds on it with state capture.
Resolve a `TextureView` inside that scope: retain the source, capture its SRV and
resource with correct COM ownership, and obtain dimensions/format together.
Diagnostics consume copied metadata instead of rereading engine pointers.
`PresenterBinding` owns original/replacement metadata and conditionally restores
the pointer under the required access policy.

**Limit:** A `NiPointer` retains the source object, not an immutable
`rendererTexture`. Merely returning a span or a reference from an unlocked
factory proves nothing about that metadata. Confirm the engine's lock and
shutdown contracts; defer destruction to the owning thread if required. Do not
hold pool/preview mutexes while acquiring the renderer lock without establishing
lock order.

**Validation:** Scope-order tests, replacement by another owner, and release from
both preview and game-side paths. Exercise real streaming and teardown in game.

## 4. Capture mesh identity and bytes together

**Evidence:** [BufferSet](../../src/engine/MeshReader.cpp#L44) stores a borrowed
`TriShape*` alongside separately derived counts/layout. `ReadBuffers` copies raw
CPU arrays or passes borrowed GPU buffers to `ReadBuffer`; the GPU method acquires
its lock only after the caller has fetched those pointers.
[MeshCache::Get](../../src/render/CompositorBake.cpp#L67) obtains identity and mesh
bytes in separate traversals. [MeshIdentity](../../src/engine/MeshReader.h#L19)
records partition/shape/vertex-buffer addresses and total vertices, but not the
index buffer, per-partition triangle count, or vertex layout. Changes to those
can be invisible to the current cache key.

**Proposal:** An engine-side `MeshReadSnapshot::Capture` retains the geometry and
skin owners, establishes the engine access scope, validates available metadata,
and produces owning bytes, bone metadata, and identity together. Extend the
existing identity type with the descriptors and buffers actually consumed.
Capture GPU references while their source pointers are protected. Give the
existing native `DecodePartition` owned/sized data after the capture completes.

**Limit:** CommonLib's `TriShape` exposes raw CPU pointers without allocation
lengths; `REFERENCE.md` already records that limit. A type cannot prove CPU bounds
by making a span from the same untrusted count. Either establish the engine's CPU
mirror invariant or use the descriptor-bounded GPU path when that proof is absent.
Address identity alone also cannot detect every in-place buffer-content change.

**Validation:** Native capture/identity tests for descriptor and index-buffer
changes, count limits, and replacement between stages. Keep decoder tests as-is;
validate engine lock coverage and CPU/GPU agreement in game.

## 5. Retain textures through UI consumption

**Evidence:** [RetainTexture](../../src/engine/ManagerSnapshot.cpp#L25) correctly
adds owning references to `Manager::Snapshot`, and
[RenderStudio](../../src/menu/StudioPage.cpp#L289) holds that snapshot while drawing.
However, `Studio::TextureHandle` is a raw pointer and `Menu::Frame` contains
independently supplied pointers into the snapshot. Their provenance is a calling
convention. [PreviewOf](../../src/menu/MenuWidgets.cpp#L64) drops its local target
owner when it returns the raw ImGui SRV handle. Safety after that relies on the
preview cache and its [eight-tick retirement delay](../../src/render/TexturePreviews.cpp#L70).

**Proposal:** Use snapshot-local texture IDs resolved by an owning snapshot view,
keeping the pure studio model free of engine pointers. A menu frame builder can
derive selected rows from that same owner. `UiFrameResources` retains preview
targets or COM views through backend draw consumption, making the draw handle an
adapter output. Do not release these owners merely when the page callback returns.

**Limit/validation:** First determine what frame-completion callback the menu
framework exposes. If none exists, document and retain the required delayed
release protocol rather than claiming a temporary C++ owner proves completion.
Test old snapshots, foreign IDs, menu closure, and retirement during a delayed
draw. Texture lifetime does not imply frozen pixel contents.

## 6. Make registration an owned relationship

**Evidence:** [WatchAnimationEvents](../../src/engine/Events.cpp#L104) registers on
the first available graph. `UnwatchAnimationEvents` later resolves the actor's
current graph collection rather than retaining the graph used for registration.
The sink itself is a process singleton, so this is not a dangling-sink finding;
the missing fact is which source owns the subscription after graph replacement.
[LightBinding](../../src/render/Light.cpp#L159) already retains its registering
shadow scene, but each entry's attach/register/remove/detach sequence is manual.

**Proposal:** A noncopyable `AnimationSubscription` retains the actual
`BSTSmartPointer<BShkbAnimationGraph>` and removes from its event source. Store it
with the live actor, replacing it only when the graph changes. A `RegisteredLight`
entry owns the scene, light, and attachment and performs rollback if registration
fails. Reuse the same small attachment abstraction for shells only if their
detach/reparent rules agree.

**Limit/validation:** Neither ownership nor event-source locking proves that
enumerating `manager->graphs` is synchronized; establish that contract separately.
Test graph replacement, failed registration, partial multi-light construction,
and external reparenting. Retaining an old graph must not become indefinite
retention of obsolete actor state.

## 7. Represent shell mode and private skin state explicitly

**Evidence:** [ShellBinding](../../src/render/Binding.h#L178) combines a material
owner, nullable vanilla pointer, and optional PBR writer. Its factories constrain
those combinations procedurally. More directly,
[Shell.cpp](../../src/render/Shell.cpp#L250) uses
`inflation.starts_with("own")` to decide whether skin data may be mutated.
`restSkinToBone_` stores full owning-layout `BoneData` records and manually clears
their weight pointers even though posing only needs transforms.

**Proposal:** `ShellMaterial = variant<PbrShellMaterial, VanillaShellMaterial>`
contains precisely the owner and operations needed for its alternative.
`PrivateSkinPose` is constructible only from a verified independent skin or a
successful deep copy, retains that skin, and stores plain rest transforms. The
diagnostic text is derived from its state rather than controlling it. Keep
`CopySkinData` as the allocator/ABI boundary; an uninitialized engine allocation
must be freed with the engine allocator until it can safely become a `NiPointer`.

**Validation:** Compile-time alternatives; inject failures at every skin/weight
allocation; confirm the original skin remains unchanged after pose and teardown.
The previous partial-copy cleanup is already present and should be preserved.

## 8. Track ownership of individual writes

**Evidence:** [SlotWriter::StillOwned](../../src/render/Binding.cpp#L256) checks
material attachment and written texture identities. `Restore` then restores
saved scalar values and the entire saved PBR flags word. Scalar records do not
hold the last value written. Another system can change only a scalar or an
unrelated flag while leaving texture identities intact, and the current restore
will overwrite that change. Losing one texture also suppresses restoration of
all other fields.

**Proposal:** Extend `SlotWriter` with small `FieldOverride<T>` records containing
original and last-written values. Use `FlagOverride` with a mask of bits owned by
this writer. Group inseparable writes such as emissive colour/storage/flags
explicitly. Keep material and geometry attachment checks outside the value
comparison. A write record should not be an independently escaping raw field
pointer whose owner might disappear.

**Validation:** Native tests for untouched fields, takeover of one scalar or
texture, unrelated flag changes, and restore after partial takeover. Define float
and NaN comparison policy deliberately. Equality is evidence of continued
ownership, not a way to detect another writer that wrote exactly the same value.

## 9. Retain one recipe revision with each runtime instance

**Evidence:** [LiveInstance](../../src/engine/LiveActor.h#L52) borrows a `Recipe*`;
[PreparedLayer](../../src/render/Compositor.h#L131) borrows a `Layer*` inside it.
[RecipeStore](../../src/engine/RecipeStore.cpp#L428) replaces published recipes and
grows/erases the published vector. `RecipeEditor` currently protects borrowers by
using the manager's retire-before-mutate helpers. That discipline is working
protection, but the free mutation APIs do not require it. Recipe, graph, signal
state, and environment are also independently nullable in `LiveInstance`.

**Proposal:** A retained immutable `RuntimeRecipeRevision` owns the recipe and
matching graph. Prepared layers use indices into that revision or copy their
small layer definitions. A live-instance factory assembles graph, signals, and
environment together and returns a ready instance or an explicit inert result.
Publish a new revision on edits; still retire/rebuild effects to enact the edit.
An epoch number by itself cannot keep the old storage alive.

**Validation:** Exercise ordinary edits, save/revert, new/renamed recipes, reload,
and transient paint insertion/removal while retaining an old prepared stack.
Test that graph and layer lookups always resolve against the same revision.

## 10. Make execution and actor access requirements visible

**Evidence:** `SessionQueue` protects submission bookkeeping, but its callbacks
are still arbitrary `std::function<void()>`. Manager/compositor operations rely
on the documented game-thread contract. Actor ownership is already handled well
in `ActorEnvironment` and tick/refresh; raw lookup remains in
[FireAt](../../src/engine/ManagerEvents.cpp#L29) and
[snapshot construction](../../src/engine/ManagerSnapshot.cpp#L125).

**Proposal:** Introduce a private-constructible, noncopyable `GameThreadContext`
at trusted hook/task entry points, backed by a runtime thread check. Require it
only for engine-touching operations. Resolve an owning actor plus its required
3D/root through a scoped accessor; use existing `ActorHandle`/`NiPointer` inside
it. Consider a closed set of value commands for actor refresh/retire/events so
that queue clients cannot capture borrowed engine data accidentally. Reuse the
studio intent vocabulary where it already expresses the command.

**Limit/validation:** An empty token is not synchronization; a resolved actor is
not guaranteed to remain loaded or attached. Preserve those live checks, and
keep event callbacks limited to the data safe to copy on their source thread.
Test command payload lifetimes natively and thread assertions in integration.

## 11. Give GPU inputs one checked representation

**Evidence:** [ProgramPass](../../src/render/RuntimeTextures.h#L85) carries fixed
arrays with separate mutable counts and an `isTexture`/index/value combination.
`RenderProgram` bounds the counts, while the preparation code maintains reference
indices. Constant-buffer structs are separately defined in
[RuntimeTexturesLab.cpp](../../src/render/RuntimeTexturesLab.cpp#L19) and
[RuntimeTexturesPass.cpp](../../src/render/RuntimeTexturesPass.cpp#L44), with another
layout in HLSL. Buffer-size and packing changes must currently agree by hand.
`BakeMesh` already checks byte-width narrowing before upload.

**Proposal:** `PackedGpuProgram` is built once from the checked core program,
bounded reference/texture/curve collections, and typed texture-reference indices.
Pack discriminated CPU references at that boundary. Share the C++ constant-buffer
definitions between allocation and upload and assert exact sizes/offsets against
the shader contract. A checked bake-upload record owns/spans validated vertices
and indices and provides descriptor byte widths without unchecked narrowing.

**Validation:** Native boundary tests at and beyond each capacity, mismatched
counts/indices, upload-size overflow, and packing assertions. Keep shader output
checks in the D3D integration checkpoint; types do not prove an independently
changed HLSL layout matches.

## 12. Model asynchronous readback states when addressing that debt

**Evidence:** [ReadMapping](../../src/render/RuntimeTexturesReadback.cpp#L69)
already unmaps successful reads reliably, and staging resources use `ComPtr`.
The call still immediately maps after copying, waiting on the GPU while holding
renderer access. `REQUIREMENTS.md` explicitly lists asynchronous readback as work
remaining from the frozen implementation.

**Proposal:** A move-only `PendingReadback` owns staging storage, expected
format/extent, and session/request identity. Polling returns pending, failed, or
owned ready bytes. Mapping lives in a shorter scope available only when ready.
CPU decoding receives an owning pixel/byte result, never a mapped pointer. The
request holds any resource needed to prevent pool reuse before capture/consumption.

**Validation:** Fake readiness/cancellation transitions, load during a pending
readback, repeated polls, failure, and exactly-once cleanup; real D3D checks for
stalls and pixel correctness. This changes scheduling and result delivery, so it
deserves its own integration checkpoint rather than a cosmetic type refactor.

## 13. Make callback failure and rollback explicit

**Evidence:** The plugin work in [PlayerUpdate::thunk](../../src/engine/Hooks.cpp#L10),
[animation events](../../src/engine/Events.cpp#L89), and the registered menu
callbacks performs allocating operations without a local exception boundary.
[Startup](../../src/main.cpp#L46) sets `initialized` before settings, recipes,
menu registration, and hook installation complete. These paths do not establish
a plugin-owned policy for a C++ exception or partially completed initialization;
the survey does not establish what each external caller would do with an escape.

There is also a specific rollback gap in
[SessionQueue::SubmitRefresh](../../src/engine/SessionQueue.cpp#L86): it reserves
the actor in `pending` before constructing/submitting the task, and removes that
reservation on a `false` return. If task construction or submission throws before
acceptance, that cleanup is skipped. If an outer caller catches the exception,
later refreshes for that actor only mark `rerun`; no accepted task exists to
consume it until the session resets.

**Proposal:** Keep a generation-aware `RefreshReservation` inside `SessionQueue`
that rolls back unless submission commits. Define whether a throwing submitter
can have accepted work; enforce an unambiguous acceptance contract. Give trusted
entry points one callback adapter with an explicit failure result and safe
reporting policy. Cleanup must precede containment: renderer state, UI Begin/End
scopes, and partially installed engine effects need scoped owners before merely
catching and continuing can be safe. Scope the player-hook adapter to plugin work
after the original function. Represent startup as unstarted, initializing, ready,
or failed; do not automatically retry partially registered hooks or sinks.

**Limit/validation:** Adding `noexcept` alone terminates on an exception. C++
exception containment does not repair invalid engine pointers or access
violations. Inject submission exceptions and verify the next refresh can run;
also test session changes, synchronous callbacks, and failure after partial
construction. UI failures require balanced scope restoration. None of these
failure paths is established as the cause of the supplied crash.

## 14. Validate numeric output at the engine write boundary

**Evidence:** [LightBinding::Update](../../src/render/Light.cpp#L235) writes colour,
fade, cutoff, and size directly to engine light fields. `IslRadius` has a finite
fallback for the computed radius, but that does not validate those other writes.
[ShellBinding::Pose](../../src/render/Shell.cpp#L412) clamps alpha and writes rim,
emissive, and derived skin transforms; [SlotWriter::WriteEmissive](../../src/render/Binding.cpp#L133)
accepts plain colour and multiplier values. A clamp alone does not reject NaN.
[ResolveLight](../../src/studio/ResolveOutput.cpp#L44) passes resolved values through,
and `SignalState::Resolve` returns literal parameters unchanged. These interfaces
do not state a finite-value contract, even when existing producers ordinarily
supply valid data. No corrupt numeric value was established in the crash log.

**Proposal:** Construct small checked `LightParameters`, `ShellPose`, and material
write records immediately before mutation. Validate finiteness and field-specific
bounds, including results of multiplication and transform construction. Decide
whether invalid input drops the update or produces a documented inert value.
Preserve valid HDR colour/intensity and the light cutoff sentinel; do not apply
one arbitrary range to all floats. Reuse the existing core value types internally
and keep engine-specific validation at the adapter.

**Validation:** Native checks for NaN, both infinities, extreme finite values that
overflow derived arithmetic, and valid boundary/sentinel values. Engine checks
confirm that rejected writes leave a consistent light/material/pose. This makes
the adapter contract explicit without reopening the tested signal evaluator.

## Smaller opportunities and deliberate exclusions

| Area | Note |
| --- | --- |
| Time | `NowMS`, snapshot watch deadlines, carried recipe time, preview ticks, and compositor ticks use plain integers. Small `RuntimeTime`/`Duration`/`FrameNumber` types can centralize wrap-safe arithmetic. `PublishSnapshot` currently compares `a_nowMS > watchedMS_ + kWatchWindowMS`, unlike the subtraction used in the queue. |
| Coordinates | `ToRootSpace`, `NodeBindPosition`, trigger positions, and ripple origins all use `Vec3`. Adapter-only `WorldPosition` and `SkinPosition` would make required transforms explicit. This is chiefly correctness, not ownership. |
| IDs | Existing `GeometryId`, `PlacementId`, `LivePieceId`, and `ActorHandle` are good patterns. Add distinct output/instance indices where they cross collections; do not wrap every integer. Snapshot/version provenance matters more than renaming raw pointers “handles.” |
| Texture cache identity | Mean caches key raw texture addresses without retaining their sources; material/preview caches already retain theirs. An owning texture key prevents address reuse, while a separate content revision is needed for mutable textures. Bound retention and invalidate deliberately. |
| Scratch targets | `RenderedStack::latest_` relies on ping-pong parity ending on the owned target. A `PingPongPass` or a ready-output owner could state that rule. Current parity already establishes it on successful rendering; do not report it as a proven dangling pointer. |
| Startup, hooks, form conversion | The once-only player hook, checked form lookup, and immediate copy into `EffectShaderRecord` are already narrow adapters. Keep their relocation signatures and runtime checks. A supported-runtime capability is useful only if it actually validates a version/ABI contract; renaming the “CS loaded” boolean does not validate that ABI. |
| Existing owners | Reuse `NiPointer`, `BSTSmartPointer`, `ComPtr`, `TextureSize`, `PbrMaterial`, `RenderPass`, and `SessionQueue`. Preserve the pool's weak return cache and move resource release outside cache locks. Avoid a generic “safe engine pointer” or a replacement smart-pointer library. |

## Coverage and verification

The survey traced `main.cpp`, settings storage and Setup UI; all engine adapter
groups (forms, environment, events/hooks, queue, manager apply/tick/snapshot/event/
inspection, recipe store/editor, mesh reader); render bindings/shells/lights,
compositor preparation/caches, texture pool/previews, initialization, passes and
readback; and the menu snapshot/texture handoffs. Other menu files consume those
records and dispatch intents; their layout/model code is outside this memory and
engine-boundary survey. The pure core and frozen implementation were not audited
again. Vendored headers and `REFERENCE.md` supplied the engine-layout and threading
constraints.

A follow-up completeness check added callback failure/rollback and numeric-write
contracts (sections 13–14). Module coverage is not proof that every ownership or
engine interaction is safe. Remaining external assumptions include the installed
Community Shaders material ABI, the engine's mesh CPU-buffer allocation lengths,
the exact renderer/scene synchronization and teardown rules, and the menu
backend's draw-consumption lifetime. A type can enforce a verified contract; it
cannot establish those facts merely by wrapping the current pointers. The
Community Shaders module-origin check does not validate the material layout of
every version.

Verification for this change is document/link review and `git diff --check`.
No build or tests were rerun for these notes. The validation entries above are
requirements for implementing the proposals, not tests claimed to have run.
