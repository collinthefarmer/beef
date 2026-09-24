# Resource retention follow-up

The source review found five retained-resource paths that outlived their useful
work: lost geometries kept inputs and stacks, mesh maintenance stopped with the
last actor, material analysis had only load-time clearing, weak shared caches
kept expired serialized keys, and the target pool kept its peak idle allocation.

The changes preserve existing ownership while releasing obsolete references:

- Partial geometry retirement destroys shell/material bindings first, then
  clears input caches, source textures, geometry/root/property references, and
  that geometry's prepared placement stacks. It retains placement indices and
  output diagnostics, marks the outputs failed, and preserves existing problems.
  Other geometries and shared instance lights remain untouched. External target
  consumers keep their leases. Full actor retirement retains its established
  lights/bindings-before-producers order.
- `OnFrame` advances compositor time and runs maintenance independently of
  nonempty actor ticking. The existing five-second mesh sweep therefore continues
  after the last actor retires. The same pass maintains materials and shared keys.
- Material records use the production engine-free `RetainedCache` policy. Active
  non-lost geometry texture pairs are protected; unused records expire at 30
  seconds or are reduced to the newest 64 unused entries under pressure. The
  grace period starts from their last access/protection. Failed unused entries
  expire too. Borrowed records are consumed synchronously and snapshot data is
  copied; eviction does not invalidate live stacks or revoke texture leases.
- `ResourceCache` removes expired weak entries on adoption and maintenance.
  Failed suppliers leave no key. Live sharing and consumers survive pruning.
- `RenderTargetPool` uses the production engine-free `ResourcePool` to retain at
  most 16 idle targets and 64 MiB of mipmapped RGBA allocation. Excess returned
  targets are destroyed; matching acquisition removes their idle accounting.
  Active targets, scratch buffers, previews, and externally retained material
  textures are outside this idle allowance. The policy does not trim live leases.

The limits are provisional retention choices, not measured alpha workload
budgets. Active material pairs are exempt, and unused entries may accumulate
between maintenance passes. Pool byte accounting excludes driver overhead.
Skyrim/D3D ownership and eventual GPU completion still require runtime evidence.

Tests cover age boundaries and clock wrap, active cache protection, pressure
ordering, empty-world expiration, failed records, weak-key churn, exact pool
count/byte limits, oversized accounting, reuse, clear, and independent consumers.
`engine_liveretirement` compiles real `LiveActor.cpp` and its real header against
isolated engine/render doubles. It verifies release order, sibling/index/light
preservation, invalid placement IDs, repeated retirement, and external leases.
It does not emulate actual material takeover or GPU rendering.

Verification: the four focused native suites passed. Eight ASan/UBSan suites
passed: `engine_liveretirement`, `engine_applicator`, `planners_resourcecache`,
`planners_resourcepool`, `planners_retainedcache`, `planners_textureleases`,
`planners_consumptionleases`, and `planners_ownedstate`. Windows Release compiled
and linked, with the existing four menu initializer warnings during the header
rebuild. The compilation database was refreshed. Targeted tidy covered
`LiveActor.cpp`, `ManagerTick.cpp`, `Compositor.cpp`, `CompositorBake.cpp`, and
`RenderTargetPool.cpp`, including the new policy headers. Its new cleanup nesting
finding was resolved by flattening the bounds check; the final retirement test
passed natively and sanitized, Windows rebuilt, and fresh `LiveActor.cpp` analysis
reported zero findings. The other six findings were within the existing baseline;
no baseline changed. Formatting, include layers, and `git diff --check` passed.

The in-game checklist adds empty-world cleanup and partial-takeover recovery
cases. No installation or in-game run was performed.
