# Rendering state investigation — 2026-09-12

## Scope and evidence

Initial source investigation of the color cycling, Recipes-page appearance
change, shell stretching, and apparent texture misalignment. No runtime changes
or deployment were made. The working tree contains extensive existing rewrite
changes over `cc2e135`; correspondence between this source and the tested DLL is
not established. `src/_old` is a comparison for inherited mechanisms, not proof
that a mechanism is correct.

The live SKSE log was byte-identical to
`docs/regression-evidence/2026-09-12/pinned-vanilla-solo-color-cycle.log` when
inspected. User observations remain essential: application messages do not
record pixels, installed material identities, renderer metadata, or skin matrices.

In `recipes-page-repro-from-reload.log`, lines 695–711, the post-load application
and the application after retirement report the same body/helmet material modes
and recipe counts. F14 records different appearances. No `restore skipped` or
`dropping ... replaced` messages were found in the saved regression logs. The
restoration defect below is therefore not yet demonstrated as the trigger for
this run.

## Findings

### 1. Restoration violates the per-field ownership requirement

Confirmed implementation defect; inherited from `src/_old/Binding.cpp`.

[`SlotWriter::StillOwned` and `Restore`](../../src/render/Binding.cpp#L256)
combine all written texture identities into one boolean. If another system
replaces one slot, restoration returns before restoring any unaffected texture,
emissive value, or scalar. The material-binding destructor applies the same gate
before restoring its original material.

Conversely, ownership checks do not compare the last written scalar values,
emissive-color storage identity, or flag bits. If only another system's scalar or
flag changes, restoration overwrites those changes. Restoring the entire saved
`pbrFlags` word also discards unrelated flag changes.

Concrete failure case: write emissive and height; a second writer replaces height;
retire. The emissive texture and multiplier remain modified despite still being
ours. Reapplying can capture modified state as the new baseline. This is a source
counterexample to REQUIREMENTS item 4, not an observed causal chain in the logs.

Required correction: track original and last-written state per physical field,
including individual owned flag bits, and restore unaffected fields independently.
Separate attachment identity from field ownership. The policy for restoring a
whole private material must account for external changes to that material.

### 2. A retained texture does not retain its rendered content

Confirmed lifetime-contract gap; the same split exists in the proof of concept.

[`RenderTargetPool::Acquire`/`Recycle`](../../src/render/RenderTargetPool.cpp#L90)
reuses a target when its shared target owners disappear. Its presenter stays
attached to that target during recycling. Meanwhile,
[`RetainTexture`](../../src/engine/ManagerSnapshot.cpp#L25), saved binding slots,
and preview sources retain only `NiSourceTexture` references. Those references
do not participate in the target's shared ownership.

Concrete sequence: hold a snapshot containing stack A's presenter; retire A;
acquire a target of the same size for B; render B. The old snapshot can now read
B's pixels through A's unchanged texture identity. Preview generation invalidation
can cause a reread, but cannot restore A's identity. This establishes stale
inspection content, not by itself corruption of a correctly restored armor slot.

The gap becomes relevant to scene rendering if a slot, cloned material, or engine
cache outlives its target owner. Finding 1 supplies one possible retained-slot
path, but that path has not been observed in the supplied run. On load, clearing
unused targets restores presenter metadata while old texture references may still
exist; this also is not a content-lifetime guarantee.

Required correction: a published runtime texture needs a lease on its target or
an explicit generation that prevents stale use. A raw or intrusive texture
reference alone is insufficient. Include engine consumers and UI draw completion
in the reuse policy.

### 3. “Private material” proves less than the name suggests

Confirmed validation gap; cross-item aliasing remains a hypothesis.

[`MaterialBinding::Install`](../../src/render/Binding.cpp#L331) changes the
material on the existing lighting property. It verifies that the installed
material differs from the original. It does not establish that the property is
exclusive to this geometry, or that the property's `emissiveColor` storage is
exclusive. `WriteEmissive` writes through that property's pointer. Shell creation
does check its color pointer against its immediate source; body installation has
no equivalent isolation step.

Do not conclude from this that the helmet and body actually share either pointer.
Capture geometry, property, original/installed material, color-storage, texture,
renderer-record, and SRV identities across all live bindings to establish that.
The locally available CommonLib `SetMaterial` implementation is a relocation
wrapper with an unnamed boolean, so it does not independently prove the engine's
copy/pooling semantics. The local CS material copy copies texture references.

### 4. The shell's skinning contract is unverified

Confirmed missing validation; the cause of stretching remains unresolved.

[`ShellBinding::Create`](../../src/render/Shell.cpp#L98) clones a geometry and
checks whether its skin-instance and renderer-data pointers equal the source's.
It deep-copies shared `NiSkinData` before inflation. That protects the copied bone
data, but does not validate the cloned root, bone pointers, bone-world-transform
array, matrix caches, or skin-partition palette correspondence. “Skin cloned” in
the log means pointer inequality, not verified skeletal attachment.

[`Pose`](../../src/render/Shell.cpp#L410) scales rows of each bind transform's
rotation and translation in bone space. This is inherited from the proof of
concept. It is not vertex displacement along surface normals. Whether Skyrim's
skinning path handles these modified transforms and caches as intended needs an
engine check; the math alone does not prove the reported world-fixed stretching.

The discriminating experiment is the same affected body armor with an otherwise
identical shell and inflation held at zero. If it still stretches, investigate
clone attachment, palettes and matrix state first. If it does not, isolate the
pose update and matrix-cache behavior. Capture per-partition bone indices and
their source/clone world transforms, not only counts or “shared/cloned” labels.

Follow-up: the user now reports stretching with a cleared/default shell, diffuse
selected, and no layers. Current `ResetShell` and `ClearOutputs` restore zero
inflation. This supplies the reported default-shell comparison and moves the
investigation toward the clone/setup path. The new snapshot
`empty-default-shell-stretch.log` logs untouched body materials with shells at
`15:49:50.296`; it does not log effective inflation values. Even at zero inflation,
`Create` still copies shared skin data, and the first `Pose` still writes rest
transforms because `lastInflate_` starts at `(-1,-1,-1)`. Zero inflation therefore
does not bypass those operations. Next isolate an attached clone before skin-data
replacement and pose writes, then add those operations individually.

### 5. Mesh UV space and material sampling space are not represented together

Confirmed omission; relevance to the observed armor is unverified.

[`MaterialInputs::From`](../../src/render/CompositorSource.cpp#L252) captures
texture references and displacement classification, but no material UV offsets
or scales. The base material has `texCoordOffset[2]` and `texCoordScale[2]`.
Mesh-derived sources use mesh UVs; image sources add their recipe scroll and tile.
No active plugin source explicitly reconciles these with material UV transforms.

This matters for a mesh-space bake or paint mask sampled later through a
nonidentity material transform. It does not establish misalignment for identity
transforms, and simply adding the material transform to every compositor sample
could double-transform maps. Record the affected material's transform and verify
the actual shader sampling convention first.

Imported recipes also intentionally scroll/tile effect imagery. The user's
apparent misalignment must be tested separately from both intentional scrolling
and distorted geometry. Use a fixed UV grid, zero scroll, identity tile, and zero
inflation on the same armor; compare body and shell.

## UI trigger and alternatives checked

- Solo/isolation, pin, and paint changes reach `RecipeEditor` and rebuild actors.
  `RebuildActorsAfterChange` retires first and queues refreshes afterward. These
  interactions repeatedly exercise the lifetime contracts above, including other
  loaded actors when rebuilding all. The broad rebuild is not itself proof of
  incorrect matching.
- `RenderRecipes` watches/publishes selection and draws the board. The inspected
  path has no unconditional recipe edit or rebuild on page entry. `Watch` only
  updates snapshot-request state. Board thumbnails request GPU previews, so the
  renderer/preview boundary remains a candidate for the no-click reproduction.
  The logs do not identify which UI command caused each retirement.
- The compositor chooses its first ping-pong destination from visible-layer
  parity. Its last successful pass lands on the stack's own target, for both odd
  and even counts. The simple theory that it publishes global scratch after every
  second layer is contradicted by this code and by the old implementation.
- The render-pass wrapper saves/restores shader stages and disables GS/HS/DS and
  predication during its passes. This audit does not prove complete integration
  with the engine's cached render state.
- `abc` owns the `Skyrim-92DEC` matching key by load precedence. That explains the
  startup warning, not a red/white/purple-cyan cycle with unchanged inputs.
- The repeated-Quickload log ends after the second clear completes. There is no
  crash stack establishing which surviving engine resource is involved. Do not
  identify the last log message as the faulting operation.

## Next diagnostic implementation

1. Stamp the build and session. Give each apply/retire a reason and generation,
   including the originating UI intent, selected piece, pin, and isolation.
2. Log the identities in finding 3 at install, first successful write, and retire.
   Include per-field restoration decisions and original/last/current values.
3. Assign target generations and log acquire/recycle/publish relationships.
   Detect a published presenter being reused while a snapshot or binding still
   holds its prior generation. Check target presenter metadata before binding.
4. Repeat the no-click Recipes reproduction with preview rendering disabled while
   preserving page navigation and snapshot generation, then enabled. This separates
   preview GPU work from selection/application changes without changing recipes.
5. Run the zero-inflation and fixed-grid shell comparisons above. Do not combine a
   material-lifetime fix and a skinning rewrite in the same diagnostic comparison.

Validation for this initial pass was source tracing, comparison with `src/_old`,
and inspection of the supplied logs and local engine/CS headers. No game run,
D3D capture, or engine-facing automated test was performed. Native planner tests
cannot establish these resource identities or skeletal behavior.
