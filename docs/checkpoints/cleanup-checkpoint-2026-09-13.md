# Cleanup checkpoint — 2026-09-13

## Baseline and scope

Started from HEAD `a983787` plus the existing uncommitted rendering fixes and
2026-09-12 evidence. A byte-for-byte working-source baseline was retained at
`/tmp/beef-cleanup-baseline-20260913` for review during this session. No deployment
or in-game result is implied by a successful build.

Keep full actor rebuilds. A recipe argument scopes application reporting, not
actor selection: changing a selector can affect actors that did not match before.
Incremental application and unrelated menu redesign are outside this cleanup.

## Implemented ownership contracts

- `OwnedState` records the original and last-written state of a coupled material
  group. Material binding captures physical texture identity, relevant scalar
  values, owned feature bits, and emissive-storage identity. Retirement restores
  a group only if it still matches. Other groups can restore independently.
  Equality cannot detect a different writer that writes the same value.
- A texture, its scalar controls, and its feature bits retire together. Coat and
  subsurface remain mutually exclusive because they share `featuresTexture0`.
  Flags outside a group's mask are never restored. Replaced emissive storage is
  never written through its old pointer.
- Private materials remain attached after restoration. Swapping the entire
  original material back would discard external changes to untracked fields.
- When retirement leaves a generated texture installed, a retained record keeps
  both its material and target alive. Records are allocated before writing and
  transferred without allocation during retirement. Engine-thread sweeping releases records
  when the field changes or only the retained records still own the material.
  Refcount sampling uses the engine's atomic intrusive-refcount API. It assumes
  material consumers follow the engine's reference ownership rules.
- `RetireActorEffects` explicitly cancels application tokens, removes lights,
  detaches shells, retires material journals, and then releases producers and
  geometry inputs. Destruction order of `LiveActor` members is not the policy.
- `ResourceSlots` gives each live target a presenter-slot lease. Target teardown
  restores its renderer data before releasing the slot. Clears can reuse freed
  names, while old snapshots and pending draws keep their own targets reserved.
  Generations are runtime acquisition state, independent of diagnostic enablement.
- Generated `TextureRef` values can be constructed directly from their producer.
  Compositor outputs, slot chains and snapshot capture carry these owning values;
  raw pointers are borrowed for engine rendering and the engine-free UI projection.
- `SkinPaletteLease` owns its hidden repair state and releases its own links on
  destruction. Flattened-tree layout access is centralized. Skin-data allocation
  is isolated in `SkinData`; shell setup uses explicit state, never log wording.

## Preview submission contract

Each thumbnail records an ImGui draw callback after its image command and retains
its target through a `ConsumptionLeases` ticket. The callback acknowledges CPU
submission of the preceding image; collection then permits target recycling.
The local SKSEMenuFramework interface exposes `ImDrawListManager::AddCallback`.
The [upstream DX11 backend](https://github.com/ocornut/imgui/blob/v1.90.9/backends/imgui_impl_dx11.cpp#L237-L265)
processes image commands and user callbacks in list order.

This is a draw-submission contract, **not a GPU fence**. It relies on the installed
framework using the same command ordering and on subsequent rendering using the
same ordered D3D11 context. Verify this integration in game. A draw list discarded
without executing its callback remains conservatively retained; a maximum of
2048 pending tickets bounds that retention and refuses additional thumbnails.
Pressure is logged once per exhaustion episode. The old eight-tick graveyard
heuristic has been removed. Load cleanup does not discard unacknowledged draws.

## Other cleanup

- `BindingPlan` contains surfaces and shell owner; unused incremental restoration
  vectors and their inputs have been removed.
- `SourceSampling` owns image cache keys, material map selection, placeholder-size
  policy, and animated sampling. Preparation and inspection share metadata setup.
- `ChangeAndRebuildActors` exposes mutation ordering and reporting scope in one
  operation; loaded actor enumeration is shared. Undo/redo preflight occurs before
  retirement. Existing edit preflight still rejects errors and unchanged edits.
- Noise seeds reuse bounded integer parsing; large values retain precision and
  fractional/out-of-range inputs are refused. Trigger counts use bounded integers.
- Diagnostic capture skips argument construction when disabled. Rebuild capture
  is a helper, so serializing all recipes does not obscure lifecycle orchestration.

## Validation

Release build `a98378789c00-439b13e4782e34eb-Release` passed and its embedded source
fingerprint matches the final working source. The ASan/UBSan native suite passed
(50 suites, 1966 checks), as did JSON schema validation and both Python tests.
Targeted clang-tidy runs completed; final logs for Binding, RenderTargetPool,
SkinPalette, SkinData, SourceSampling, TextureRef, TexturePreviews, MenuWidgets,
ManagerShared and BindingPlan contain no project diagnostics. The tidy script's
summary also includes unrelated cached results; it is not a full-tree clean bill.
`git diff --check` passed. No deployment or in-game acceptance was performed.
New native tests cover coupled ownership decisions, repeated presenter reuse,
retained consumers, submission acknowledgement and pressure, exact seed parsing,
and disabled diagnostic capture. These exercise shared production helpers; they
are not substitutes for executing the engine adapters in game.

## Required in-game acceptance

1. Original red baseline / Recipe Solo / page-navigation reproductions: stable
   output and no presenter aliases, renderer mismatches or rejected live leases.
2. Repeated load and reapply cycles whose cumulative target allocations exceed
   512: no exhaustion while concurrent retained usage remains below capacity.
3. External takeover of one material texture, scalar, flag group and emissive
   storage: preserve that group, restore unrelated groups, retain still-installed
   generated content. Verify no writes reach replacement emissive storage.
4. Keep previews open during edits/reloads; close or discard windows during a
   pending draw. Verify callback acknowledgements, bounded retention, and no
   recycled preview displaying another producer's content.
5. Nordic and Daedric shells at rest and moving arms/pelvis, helmet comparison,
   first-person geometry, inflation, repeated attach/retire. Verify source skin
   and original geometry remain unchanged.

In-game acceptance remains pending until these cases are run against the final
built DLL. Do not treat earlier 2026-09-12 evidence as validation of this cleanup.
