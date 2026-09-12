# Rendering state fixes and diagnostic iteration plan

Status: proposed implementation plan, 2026-09-12. No fixes or sequencer have been
implemented by writing this plan.

Basis: [source investigation](render-state-investigation-2026-09-12.md),
[reported reproductions](../regression-feedback-2026-09-12.md), and
[integration procedure](../in-game-regression.md). Preserve the existing working
tree changes. Use `src/_old` to identify inherited behavior, not as a correctness
oracle for the defects under investigation. Keep recipe format 1 unchanged.

## Outcomes and limits

The work must establish these observable contracts:

- An undeformed shell follows its source geometry on affected body armor.
- Apply → retire → apply reproduces the same appearance from the same inputs.
- A retained published texture cannot silently become another output's content.
- Retirement restores fields we still own and preserves external changes.
- Inspecting a page cannot change recipe/application intent; preview rendering
  cannot alter installed scene resources.
- Save loading cancels stale work and releases resources only when their consumers
  are finished.
- Mesh-derived masks and textures align under the actual material UV convention.

Logging a successful application is not visual validation. Report structural
checks, GPU output checks, and user observations separately. Do not assume the
color cycle, stretching, UV appearance, and Quickload crash share a root cause.

## Delivery order

| Stage | Deliverable | Exit checkpoint |
| --- | --- | --- |
| 0 | Correlated diagnostics and reproducible build identity | Capture the existing color and empty-shell failures without changing their behavior. |
| 1 | Small built-in sequencer and comparison fixtures | Replay bounded production commands and produce an interpretable evidence bundle. |
| 2 | Undeformed shell boundary and isolated skinning fix | Empty default shell follows affected body armor; identify the first failing construction stage. |
| 3 | Owned texture publication and explicit retirement | A held snapshot/preview never consumes a repurposed output; resource use remains bounded. |
| 4 | Field ownership and verified material isolation | Partial external changes survive retirement; unaffected fields return to baseline. |
| 5 | Load lifecycle and preview/render boundary corrections | Repeated manual Quickloads and no-click page entry preserve the relevant contracts. |
| 6 | Explicit UV sampling contract, then necessary mapping fixes | Fixed-grid and mesh-derived fixtures align on body and shell. |
| 7 | Full integration and optional narrower application updates | Stable full rebuild first; incremental changes only if justified by evidence. |

Each stage is independently reviewable and gets its own test build. Stage 5's
thread/lifetime contract must be investigated during stage 0 and obeyed by stages
2–4; it is not permission to defer synchronization until after implementation.
If diagnostics identify an active crash path, isolate that correction before
running stress sequences. Stop at each in-game checkpoint for the user to test.

## Stage 0 — Make transitions explainable

Add typed diagnostic events behind one recorder. Suggested locations are a small
`diagnostics/` module, with adapters in `Manager`, `RecipeEditor`, `Binding`,
`Shell`, `RenderTargetPool`, and `TexturePreviews`. Keep event emission out of
domain decision logic where a returned decision can be recorded by its adapter.

Record startup provenance: build ID, source revision plus dirty-source fingerprint,
build configuration, runtime/SKSE/CS versions where available, and a hash of
effective settings and canonical loaded recipe contents. A bare Git revision is
insufficient for this uncommitted rewrite. Record source-to-DLL provenance in the
build/install artifact without requiring source changes just to stamp a run.

Every transition carries a monotonic event sequence, timestamp, thread ID, run ID,
load-session generation, originating command/reason, application attempt, actor,
armor, camera, geometry identity, recipe revision, and binding generation where
applicable. Reuse `ApplicationToken` and `SessionQueue` cancellation semantics;
do not create a competing application queue. Distinguish stable form identity
from session-local geometry identity and pointer reuse.

Record at boundaries, not on every frame:

| Event group | Required evidence |
| --- | --- |
| Intent and application | Actual UI intent or sequencer action; selection/pin/isolation before and after; refresh reason; queued/prepared/rendered/failed/cancelled token. |
| Binding install/write/restore | Geometry/property/original and installed material/color-storage identities; physical field; original, last-written, and current values; restoration decision and reason. |
| Texture lifetime | Target ID and generation, producer identity, presenter/renderer-record/texture/SRV identities, published leases, acquire/recycle/release, preview request source generation. |
| Shell setup | Source/clone parent and local/world transforms; root/bone/world-transform-array identities; per-partition counts and palette validation; skin/matrix-cache sharing; effective pose values; construction stage. |
| Lifecycle | Preload cancellation, retirement begin/end, resource retirement/completion, postload resumption, rejected stale work. |
| Observation | Settled state fingerprint, invariant failures, GPU sample status, and separately entered visual result. |

Use a bounded in-memory ring with configurable detail for the selected actor.
Retain compact reasons and failures in normal logs. Detailed matrix dumps occur
only on demand or at the first mismatch; report suppression and dropped-event
counts. Logging must not retain engine objects, dereference stale pointers, or
add GPU readback to the normal tick path.

Write versioned JSONL events and a small run manifest under the plugin's existing
log location. Use unique run names so a subsequent launch does not truncate prior
evidence. Flush at step boundaries and before load handoff; do not rely on a crash
handler to walk engine objects. Include diagnostic switches in the manifest.

Before changing resource lifetime, document which thread performs each mutation
and which engine renderer lock, queue, or completion mechanism covers each
consumer. Native shared ownership is not a substitute for this access contract.

Checkpoint: capture one Solo cycle, one no-click Recipes visit, and the user's
empty/default shell reproduction. Establish whether the tested DLL matches the
source. Preserve the observed failures as baseline evidence.

## Stage 1 — Built-in test sequencer

Implement a small asynchronous state machine, not a second implementation of
application logic. A pure runner consumes observations and returns actions; its
engine adapter invokes the same production editor/application commands as the UI.
Add a Diagnostics area in Setup with fixture selection, Start, Step, Continue,
Stop, current action, elapsed time, and structural/GPU/visual results.

Suggested states: idle → capture preconditions → execute → await application →
await rendered observation → visual checkpoint → next step → cleanup → finished.
Failure, timeout, manual interference, and load cancellation have explicit exits.
The runner advances from normal updates and never sleeps on the game or UI thread.

Initial bounds: one active run, default 10 cycles, maximum 50 cycles, 10-second
application timeout per step, and five-minute active execution limit. Manual
visual checkpoints pause the active-time budget and always leave Stop available.
Wait for the correct application token and session generation, then a subsequent
render observation. Queue acceptance, a fixed delay, or `prepared` alone cannot
complete a step. GPU readback/completion failure is not a pass.

Capture selection, pin, isolation, freeze/time controls, effective recipe hashes,
and actor/camera identity before starting. Use temporary in-memory fixtures through
the normal recipe/application path, with explicit teardown; never save over user
recipes. Restore temporary state only if it still belongs to the run. Detect user
edits/equipment changes and cancel with a reason instead of silently overriding
them. Loading cancels dependent work; cleanup after a load must not restore pointers
or state captured from the old scene.

Start with these sequences:

| Sequence | Actions and assertions |
| --- | --- |
| Lifecycle | Baseline capture → apply constant red → settle → retire → compare baseline → reapply → compare red; repeat. |
| Solo | Constant red material plus helmet control → selected-piece Solo on/off; verify desired scope and unchanged outputs outside that scope. |
| Preview isolation | Same frozen recipe/state with preview work disabled and enabled; user enters Recipes without clicking controls. Compare commands, resources, and pixels. |
| Shell stages | Same geometry: original-only baseline → attached clone with no skin replacement or pose writes → independent skin copy → zero pose → controlled nonzero pose. Pause for visual movement checks at each stage. |
| Texture retention | Hold a real snapshot lease across retirement/reapplication, request its preview, and churn a bounded number of equal-size outputs. Assert generation/content ownership and bounded resources. |
| External writer | In adapter tests, change one field/flag group between apply and restore. Later provide an opt-in diagnostic engine case using the same ownership boundaries. |
| UV | Frozen grid, identity recipe transform, zero inflation; then known material transforms and mesh-derived mask. |
| Load observation | User performs first and immediate second Quickload; recorder captures cancellation/teardown/resumption. No automated save/load commands in the initial runner. |

Keep UI navigation testing real: calling a preview function alone is not equivalent
to entering the Recipes page. The initial sequencer prompts the user for this step
and records actual UI activity; it must not invent a navigation completion event.

For deterministic fixtures, freeze recipe time and use constant outputs. Compare
structural fingerprints separately from pointer identities, since correct rebuilds
may legitimately allocate new objects. At selected checkpoints, perform bounded,
synchronized GPU readback: exact expected colors for constant RGBA fixtures,
documented tolerances for filtered outputs. Do not compare lit world screenshots
as exact pixels or claim a texture sample proves the right geometry displayed it.
Mark visual results as pending/pass/fail with a user note.

Native tests exercise the production runner: out-of-order completion, stale token,
timeout, cancellation, user interference, cleanup, maximum cycles, and load-session
change. A small host-side report tool summarizes JSONL and pairs runs by fixture
and build, highlighting the first divergent transition.

## Stage 2 — Establish a correct undeformed shell

Split `ShellBinding::Create` into validated attachment, shell material setup, and
optional deformation preparation. Suggested records are `AttachedShell` and
`PrivateSkinPose`; constructors/factories enforce their actual ownership contracts.
Names are provisional. Do not infer exclusive ownership from a description string.

First introduce diagnostic stage switches without changing the production default.
The reported empty/default shell already has zero configured inflation, but still
copies skin data and performs its first pose write. The stage sequence must bypass
those operations separately and log effective values, rather than repeat the same
default-shell test under another name.

Verify source/clone skeletal attachment, palette indices and bone transforms,
including sleeves and pelvis, before deciding whether to share or rebuild skin
state. Validate engine clone semantics from available implementation evidence and
live captures. Never blindly shallow-copy ownership-bearing pointers to make the
clone resemble its source. Clone and material setup failures must unwind without
modifying source geometry.

Implement the smallest correction at the first failing stage. Zero deformation
must not allocate private pose data or write bind transforms unnecessarily. Enable
nonzero inflation only through a validated deformation path; choose bind-transform
updates versus private vertex deformation from measured engine behavior. If moving
to vertex deformation, preserve layout, weights, normals, tangents, partition
mapping and GPU lifetime explicitly.

Checkpoint: Nordic and Daedric body armor at idle and while moving arms/pelvis;
helmet as comparison; repeated attachment/retirement; applicable first-person view.
Confirm the original geometry remains unchanged. A correct undeformed shell is a
prerequisite for further inflation work, not evidence inflation is already fixed.

## Stage 3 — Own published textures and order retirement

Introduce a runtime texture handle holding the target lease, producer identity and
generation. Carry it through compositor outputs, slot writes, snapshots, and preview
sources. Keep a distinct handle for engine-owned static textures. The raw presenter
pointer is borrowed only at the final engine call, with its owner retained.

Distinguish content updates by the same animated producer from reuse by a different
producer. A snapshot may intentionally refer to the former; it must never silently
refer to the latter. Define whether each inspection request means current content
or a frozen capture. Do not use texture pointer identity as content revision.

Create an explicit geometry-effect retirement operation: stop writes and cancel
dependent work → detach shell → restore/relinquish fields → release published
leases after consumers complete. Destroying actor records calls that operation;
member declaration order is no longer the lifecycle policy. If a field remains
installed after a partial takeover, retain its resource until an explicit handoff
or verified safe removal; a lease only in the dying binding is insufficient.

Establish renderer and UI consumption completion before final recycling. Replace
the preview graveyard's fixed tick assumption with the available completion contract
or a documented conservative retention policy backed by measurements. Bound pending
retirement, snapshot history, previews, and pool resources; report pressure and fail
new allocations clearly rather than recycle live content. Manage the finite presenter
inventory across repeated clears; do not solve exhaustion by monotonically allocating
more names.

Tests: production pool/lease logic with a fake backend, old snapshot retained across
rebuild, animated updates versus reuse, pool teardown with outstanding leases,
allocation failure, stale preview request, resource pressure, and late completion.
Checkpoint: retention sequence plus original Solo/Recipes reproductions. Record
remaining visual defects rather than attributing all changes to lifetime repair.

## Stage 4 — Restore owned state and establish isolation

Replace `SlotWriter`'s aggregate ownership gate with a journal keyed by physical
field. Store original state, last written state, and attachment generation. Group
coupled texture/feature/scalar transitions where independent restoration would
create an invalid material; document the policy for partial external takeover.
Track only owned flag bits. Preserve external changes to unrelated bits.

Validate geometry → property → material attachment separately from field ownership.
Track emissive-storage identity as well as value. Value equality cannot identify
an external writer that wrote the same value; document this limitation and prefer
engine ownership/version signals where available. Do not claim perfect writer
provenance from equality checks.

Prove private installation semantics and audit aliasing across all live geometry
bindings. Where mutable property/color storage is shared, install an independently
owned property/storage through a verified engine path, or reject the unsafe binding
with a clear reason. Avoid silently modifying shared originals. Decide explicitly
when reinstating an entire original material would discard external changes to the
installed copy; preserve those changes rather than using whole-material replacement
as an unconditional cleanup shortcut.

Extract production ownership decisions for native tests, including one texture
taken over, scalar-only takeover, changed color-storage pointer, unrelated flag
changes, shared physical slots, detached property, and failed installation. Adapter
tests exercise actual write/restore ordering and resource leases.

Checkpoint: red material lifecycle, Solo, helmet/body and two-wearer isolation,
retirement after partial takeover, and reapplication from verified baseline.

## Stage 5 — Close lifecycle and preview integration gaps

Audit `SessionQueue`, `ApplicationService`, `Manager::Clear`, preview work and
renderer consumers as one load-session lifecycle. Reject old-session actions and
observations at execution, not just submission. Retain resources until their last
consumer completes; never restore into newly loaded geometry using old references.
Verify lock ordering and avoid waiting for GPU/UI completion while holding locks
those consumers need.

Use the stage-0 command trace to distinguish page-entry intent changes from GPU
preview effects. Fix the demonstrated path. Audit capture/restore of D3D state
against engine shadow-state caching and all modified stages/resources; pointer
lifetime fixes alone do not establish pipeline correctness. Keep previews read-only
with respect to recipe intent and scene resource ownership.

Checkpoint: user-driven repeated Quickloads, including the immediate second reload;
load with pending apply/preview/sequence work; menu open/closed; actor unload and
geometry replacement. Capture an external crash report if a crash recurs. The last
completed log event is not a stack trace or fault attribution.

## Stage 6 — Establish sampling coordinates

Capture affected meshes' UVs, material UV offsets/scales, texture clamp modes, and
recipe transforms. Verify the actual CS sampling convention. Define mesh-space
bake/paint and material-space image contracts, then introduce the required mapping
at explicit boundaries. Preserve texture bases that already receive the material
transform; avoid applying it twice. Represent unsupported or degenerate mappings
as errors, not guessed conversions.

Tests cover identity, offset, scale, mirrored/nonuniform transforms and the actual
supported UV sets. Check transformed mesh bakes against analytically known fixtures.
Checkpoint: fixed grid and mask on body/shell with scrolling disabled, followed by
intentional imported scrolling. Re-evaluate apparent misalignment after stretching
is fixed before concluding another rendering defect remains.

## Stage 7 — Integration and completion

Run relevant native production tests in `nix develop`, required repository checks,
and `./build.sh Release -j 4`. Follow the existing installation procedure for each
in-game checkpoint; retain build/run manifests and diagnostic outputs together.
Do not treat compilation or native tests as engine validation.

Replay the fixed sequences and original manual regressions on one build. Check
diagnostics enabled and disabled, bounded resource usage, and ordinary-frame cost.
Limit GPU readback and detailed tracing to diagnostic runs. Update the regression
report with build IDs, measured outcomes and remaining failures.

Only after full retire/reapply is correct, consider reconciling desired and installed
geometry effects incrementally. Reuse existing planners; start with preserving
unaffected actors/geometries during Solo/pin changes. Test matching, recipe revision,
equipment, camera, and load invalidation. Defer this optimization if it adds risk
without resolving a measured issue.

Completion requires separate structural/GPU/visual results for the original
reproductions, no unexplained ownership invariant failures in those runs, and
documented remaining limitations. Remove temporary unsafe stage switches or keep
them explicitly diagnostic; retain the bounded sequencer and useful transition
logging for future regressions.
