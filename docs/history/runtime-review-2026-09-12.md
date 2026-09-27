Status: history. Its seam findings are settled in `REFERENCE.md` and the
cleanup checkpoints. Names and paths here predate the critique remediation of
2026-09-14 (Plan C's file moves and Plan D's renames); the root `README.md` lists
the current set.

# Engine integration and ownership review

This pass follows the mixed-layout DLL diagnosis in `crash-2026-09-11.md`.
It preserves the ongoing refactor and reviews the current engine/render seam.

## Corrected paths

- Startup previously only loaded settings and registered the menu. It now
  loads recipes and installs the engine event sinks and player update hook
  once, with Community Shaders as the runtime prerequisite. The emissive path
  defaults disabled until that check succeeds.
- Hook installation is idempotent. Calling it twice cannot save our own thunk
  as the previous update function and recurse indefinitely.
- Pre-load pauses ticks and task submission, invalidates queued work, detaches
  animation sinks, clears actor effects/caches, and replaces the published
  snapshot. Post-load resumes actor discovery; new game clears and resumes.
  All queued manager work now checks the session generation, including editor
  commands and animation events.
- Live actors use handles. A tick holds the resolved actor reference and
  retires deleted/unloaded actors; actors with no remaining effects retire
  through the same cleanup path. Refresh retains the looked-up actor while
  replacing its bindings. Piece enchantments are stored as form IDs.
- Animation registration avoids CommonLib's unchecked `graphs.front()` and
  unlocked sink-array scan. It checks each graph and uses the event source's
  locked registration/removal methods. Empty graph arrays are safe.
- Runtime snapshots retain the texture objects referenced by their rows.
  Deferred preview entries and extracted work batches also retain their
  source textures. An old shared snapshot remains a valid owner during actor
  retirement or publication of a newer snapshot.
- Render-target deleters hold a weak return-cache reference instead of a raw
  pool address. A late release safely destroys the target if the cache is gone.
  Return-cache access is locked; target destruction and shared-pointer control
  block allocation happen outside that lock.
- Light bindings retain the shadow scene that registered their lights and
  remove lights from that scene, rather than whichever scene is current later.
  Entry storage is reserved before lights are attached to avoid vector growth
  throwing after registration.

## Verification scope

The native sanitizer suite checks the engine-free recipe, mesh, studio, and
planner modules. It does not execute the SKSE callbacks or engine reference
counting. Those paths require the Windows build/static checks and a game retry.

Completed: Release DLL build, sanitized native suite, schema validation, and
focused clang-tidy on the ten changed implementation files. The per-file tidy
baseline gate reports no new findings. The final animation-source registration
change was rebuilt successfully. The DLL/PDB pair is staged, not installed.

Texture references preserve object lifetime, not immutable rendered pixels.
Manual shell skin-data construction, raw CPU mesh allocation extents, and the
Community Shaders material ABI remain engine integration surfaces. This pass
does not establish that all runtime memory failures have been eliminated.

## Follow-up pass: materials and shell allocations

- Slot writers retained raw material/property pointers. Keeping the property
  alive did not keep a material alive after replacement. Writers now hold
  owning references and reject writes after material replacement. Binding
  liveness checks the geometry's current property too; shells retain their
  vanilla material and check property and parent attachment before posing.
- Temporary materials from `Create`/`CreateMaterial` and scene clones now
  acquire an owning reference immediately. Early exits release them, and a
  failed private-material installation drops the binding.
- Skin copying retained the source bone count while its new bone pointer was
  null, exposing partial construction to invalid destructor traversal on an
  allocation failure. The count now stays zero until the copied array is
  ready. Per-bone weights receive independent engine allocations; the owning
  skin reference cleans up partial copies on failure.
- Presenter cleanup only restores metadata if the presenter still points to
  its owned replacement. The renderer availability flag is atomic across
  game-thread initialization and render-thread preview requests.

The follow-up Release build and focused clang-tidy baseline gate passed.
Analyzer-enabled checks on `Binding.cpp`, `Shell.cpp`, and
`RuntimeTexturesLab.cpp` reported no analyzer diagnostics, only the existing
ten tidy findings. `git diff --check` passed. These engine-facing paths still
need an in-game retry; the native suite from the preceding pass does not
exercise engine allocation or reference counting.

That pass produced the following artifacts before the further engine pass
below. SHA-256:

- DLL: `a54b21a7c13ded577a504f852c2186e0b672429f68f5353d77f97d26640819e5`
- PDB: `528d5e92c4c75f780e7180b78094d112d7b7bb7566ea9272c1c9779d1cce8214`

## Further engine pass: renderer state and load boundaries

- `IsPBRProperty` rejected only the base vanilla material vtable. The vanilla
  landscape material also passes its feature check and has a different
  layout, so the adapter could interpret unrelated fields as PBR texture
  pointers. The check now requires a vtable belonging to the loaded Community
  Shaders module before accessing extended fields. This narrows the accepted
  implementations; it does not validate arbitrary CS versions against the
  pinned layout.
- Render passes saved one render target and one viewport. Binding our target
  clears other render-target slots and can clear conflicting texture bindings
  in other stages. The scoped state owner now preserves all render-target
  slots, viewports, shader-resource bindings and graphics shader class
  instances. Geometry/hull/domain shaders and predication are disabled during
  the pass and restored afterward. Output-merger UAV slot zero is restored
  without resetting its counter if our single render target displaced it.
  See the D3D references under lab mechanics in `REFERENCE.md`.
- Readbacks could skip unmapping after a successful map with unusable data or
  a vector-allocation exception. Staging resources now have COM owners and
  successful maps have scoped unmaps. Mean readback checks the pointer, pitch,
  format and mip count. Readback copies disable and restore predication so
  an engine visibility predicate cannot leave the staging copy unwritten.
  Extent queries use the renderer lock too.
- Mesh-bake uploads now reject byte counts exceeding the D3D11 descriptor's
  32-bit range before narrowing them.
- SKSE dispatches post-load even when loading failed. The callback now checks
  its encoded success value and leaves engine effects paused on failure.
- Refresh and equip-finalization submission recheck loading and generation
  inside the queue lock. Previously an event could pass the outer loading
  check, wait while `Clear` reset the queue, then insert pending work whose
  task was dropped. That left a pending actor with no servicing task, or
  carried an old equip timer into the next session.

These changes add D3D state capture work per render pass. In-game validation
must cover both visual correctness and tick time; the engine-free sanitizer
suite cannot establish D3D behavior, hook scheduling or loaded CS ABI agreement.

The Release build and focused clang-tidy baseline gate passed for the five
changed implementation files. Analyzer-enabled checks reported no analyzer
diagnostics; the remaining findings in these files are existing render-pass
size/complexity warnings. `git diff --check` passed.
That pass produced the following DLL/PDB pair, superseded by the type-boundary
build below. SHA-256:

- DLL: `01af59c03b4f28004ff0e87fef238a08206d106fc4c92696ed3da6cdca8ed87f`
- PDB: `8bbc945a40aa262b0c1de17cb900a6999d57c749465a78b086414ca98f13e661`

## Type-enforced boundaries

- `PbrMaterial::Bind` validates the property and retains its material and
  property together. Its constructor and layout storage are private. Slot
  writers and material-input snapshots require this record; the adapter's
  remaining PBR downcast is confined to its checked constructor. Compile-time
  assertions reject default construction, unchecked pointer construction, and
  copying writers. The raw layout getters on material and shell bindings were
  removed. All binding writes still check live attachment identity.
- `TextureLab::RenderPass` owns the renderer lock and saved context state in
  one noncopyable, nonmovable scope. All five drawing paths issue context
  commands through it. Restoration and captured COM-reference release finish
  before unlocking, including early returns. The constructor requires nonnull
  renderer and context references.
- `SessionQueue` owns generation, loading, refresh coalescing, equip deadlines,
  and synchronization. Failed task submission releases pending state, stale
  callbacks cannot consume a new session's work, and follow-up refreshes retain
  their original generation. Callbacks hold weak queue-state references, so
  destroying the queue invalidates outstanding callbacks. Manager tasks retain
  the existing game-thread execution contract; the queue releases its mutex
  before calling engine code.

The sanitized native suite passed, including 21 new session-queue checks for
load transitions, stale and duplicate work, failed submission, timer wraparound,
and queue destruction. The Release build passed without compiler warnings.
The focused clang-tidy gate passed for all nine changed implementation files;
analyzer-enabled checks reported no analyzer diagnostics. Formatting passed for
all sixteen changed C++ files, and `git diff --check` passed.
The staged DLL and PDB match the build outputs and have not been installed.
SHA-256:

- DLL: `29fcf0012e73ada960c613061e64b24d9ad8e6b97ae18b2dc5cd4d19470fe53c`
- PDB: `b850620bfedc30f66e57a8f9072b8b0e3259308f3a9483f8189fdb807b5df078`

These types enforce construction and cleanup within the adapter. They do not
prove Community Shaders ABI compatibility, keep a retained material attached,
or synchronize engine calls made from arbitrary threads. Those remain the
runtime checks and game-thread integration contract above.

The remaining opportunities are recorded in
[the engine-facing type survey](engine-types-survey-2026-09-12.md), with source
locations, priorities, the rule each proposed type would enforce, and validation
requirements. That survey changes documentation only.

## In-game checkpoint

After installing the matching DLL/PDB pair with the game closed:

1. Load a save. Look for `event sinks registered`,
   `hooked PlayerCharacter::Update (vfunc 0xad)`, and `kPostLoadGame`.
   The hook-registration line should appear only once per process.
2. Equip and remove enchanted armor, inspect textures in the studio, and
   allow NPCs wearing affected armor to unload. Exercise shell and light
   recipes as well as material-only effects. Check shell inflation on skinned
   armor and refresh effects after another system replaces its material.
3. Load another save without exiting. Look for `kPreLoadGame`,
   `cleared ... actor states`, then `kPostLoadGame`; confirm effects rebuild
   and old preview/actor rows disappear. Repeat with a new game.
4. Repeat the armor-inspection operation from the supplied crash log. If it
   fails, collect a new crash log with the matching PDB and plugin log.
5. Exercise animated textures, previews and mesh bakes in scenes using CS
   effects. Check for missing render outputs, stale previews and increased
   tick time. After a failed save load, the log should say
   `save load failed; engine effects remain paused`; loading a valid save or
   starting a new game should resume effects.
