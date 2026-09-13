# Texture consumer lifetime checkpoint

This is the first structural integration from the updated
[rendering-state plan](render-state-fix-plan-2026-09-12.md). Presenter uniqueness
and shell palette repairs remain included.

## Change

`TextureRef` retains an engine texture and, for generated textures, its target
and acquisition generation. The weak target registry remembers generated
identities after expiration; expired generated handles are rejected rather than
accepted as static engine textures. A live presenter cannot be re-registered for
another target or generation. Presenter identities remain reserved by the
existing pool until its shutdown.

Prepared source/mask/material inputs, compositor base and analysis records,
material journals, Studio snapshots, preview cache entries and queued preview
work now carry this reference. Material writes accept a typed reference and
reject invalid references before touching the material. Empty references retain
the existing restore/default meaning. Raw Studio row pointers are backed by
references in their containing snapshot. Engine material fields still use the
engine ABI; the corresponding journal retains their generated targets while
bound.

A retired producer can release its references without recycling a target held
by a material, snapshot or preview. The final lease release invokes the existing
pool deleter. This enforces the reuse dependency without changing the current
application rebuild policy. Leases do not freeze animated pixels, fence GPU
work, replace the per-field restoration policy, or make application replacement
transactional. Those remain later checkpoints.

## Verification

Native lifetime tests model a producer, material, snapshot and queued preview:
the target expires only after the last consumer releases it. Tests also cover
expired-generated versus static classification, live reassignment rejection and
new acquisition generations. The trace report adds `Texture lease rejections`;
material first-write events and preview requests include their source generation.
The existing acquire/recycle events continue to expose resource lifetime.

After restarting, repeat Solo and Recipes-page viewing, painting, shell removal
and recreation, then equipment and save/load checks. Expected: prior visual fixes
remain stable; zero presenter aliases, renderer mismatches and lease rejections.
Retained snapshots/previews may legitimately delay target recycling.

## Installed validation status

Build `a98378789c00-a46daab12af211c2-Release` installed; DLL, manifest and all 512 presenter assets
match staged bytes. Release build and sanitized native suite passed, including
nine lease-lifetime checks. Two Python packaging/report regressions pass.
Formatting and whitespace checks pass. Focused static analysis finds no issues
in TextureRef or RenderTargetPool; existing preview complexity and glint size
findings remain. In-game validation is pending.
