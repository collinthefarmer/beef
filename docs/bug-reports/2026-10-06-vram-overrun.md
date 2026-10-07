# Runtime render targets push VRAM past the DXGI budget and stall the GPU

**Build:** `48938f01e5f7-dirty-99bcac688f1b-Release` (commit 48938f0 "Skip NPCs outside the camera's view" plus uncommitted changes), runtime 1.6.1170.
**Date:** 2026-10-06, session 17:24–17:28 local. The user killed the game because of the slowdown.
**Hardware:** RTX 5070 Ti (16 GB), DLSS at 4K output, Community Shaders frame generation (FSR3 FG on D3D12).

## Symptom

For about 60 s, every frame took 250–1000 ms (2–5 FPS), and the game did not recover. The frame times returned to normal only when the game was killed. The player was in combat outdoors, and the profiler shows paralysis and fire spell effects loading during that time.

## Evidence

All data comes from VramProfiler 0.7.3 (`plugins/VramProfiler`). The files are in `A:\home\documents\My Games\Skyrim Special Edition\SKSE\`, as `VramProfiler.prev.log` and `VramProfiler.prev.csv`. The relaunch at 17:31 rotated them to `.prev`, and it also overwrote `BetterEnchantmentEffects.log`. The trace `BetterEnchantmentEffects-trace-1791321834040182.jsonl` contains only startup and settings events because `DiagnosticLogging=false`.

### BEE allocates large render targets in bursts

At 17:26:54, BEE loaded the presenter textures `slot_162.dds` to `slot_166.dds`. In the same second, the process created 1.59 GiB of `R8G8B8A8_UNORM` render targets at 2048×2048 with full mips, about 21 MiB each:

```
hitch: 522.7 ms | VRAM 5.31 GiB / 14.87 GiB | last 1.0 s: created 1.59 GiB in 344 resources, destroyed 1 MiB
  recent  1.21 GiB  tex2d render-target RGBA8 <=2K video (live 1.96 GiB in 94)
  recent   368 MiB  tex2d render-target RGBA8 <=1K video (live 661 MiB in 124)
  loaded     0 MiB  data\TEXTURES\BetterEnchantmentEffects\slots\slot_162.dds … slot_166.dds
```

These allocations then repeated every 10–20 s. Each burst created about 880 MiB and destroyed about 480 MiB, so every burst added about 400 MiB:

```
hitch: 547.9 ms | VRAM 14.25 GiB / 13.79 GiB (103.4%) | last 1.1 s: created 879 MiB in 101 resources, destroyed 481 MiB
  recent   725 MiB  tex2d render-target RGBA8 <=2K video (live 2.17 GiB in 104)
```

The 30-second census shows how much memory the BEE call stacks held, as the two largest caller stacks below the `CreateTexture2D` hook:

| Census | BEE live render targets |
|---|---|
| 17:26:51 | 498 MiB in 67 textures |
| 17:27:21 | 1.07 GiB in 134 textures |
| 17:27:51 | 1.08 GiB in 134 textures |

The caller stack is `BetterEnchantmentEffects.dll+1F2DC0 < +1F3380 < +20A2E0 < +1E7570`. In the source, the format, size and flags match `RenderTargetPool::CreateTarget` (`src/render/RenderTargetPool.cpp:97`): `R8G8B8A8_UNORM`, `mipLevels = 0`, `GENERATE_MIPS`, one presenter slot per target. I did not map the RVAs to symbols, so check this with the PDB.

The profiler lists the creator as `TextureDownscaler.dll+20FF5` because Texture Downscaler hooks `CreateTexture2D`. The "callers of the top creators" section names BEE as the real caller.

### Total use went over the budget, and the GPU began to page memory

From `VramProfiler.prev.csv`, by seconds since launch:

| t (s) | FPS | local use (MiB) | budget (MiB) | counted allocations (MiB) |
|---|---|---|---|---|
| 154 | 46 | 12866 | 13828 | 13285 |
| 156 | 7 | 11481 | 14129 | 15441 |
| 161 | 4 | 12141 | 14086 | 15117 |
| 198 | 3 | 14345 | 14120 | 15195 |
| 230 | 6 | 14085 | 14121 | 14862 |

From t=156 s on, the counted allocations stayed between 14.6 and 15.4 GiB, which is more than the 14.1 GiB budget. The WDDM memory manager then moved resources between VRAM and system RAM on every frame. During those frames, the main thread sampler found 80–92% of samples in `amd_fidelityfx_framegeneration_dx12.dll+F4DB0` or the NVIDIA driver (`nvwgf2umx.dll`), which means the CPU was waiting on the GPU.

Before 17:26:54, the same scene ran at 18–26 ms per frame with 0.5 GiB of BEE render targets.

### Other memory users stayed the same

DynDOLOD LOD4 meshes held 2.0 GiB, the game's textures 4–5 GiB, and the game and Community Shaders render targets about 2 GiB. These amounts did not change during the slowdown. The DXGI budget fell from 15.2 GiB to 13.8 GiB at about t=118 s, probably because another process used more VRAM. That reduced the free memory, but it was not the cause.

## Settings at the time

```
PlayerOnly=false
EnableShaders=true
UniqueMaterial=true
GeneratedShaders=true
RenderPlanCache=true
CullOutOfView=true
TextureScale=Full
EvictDistance=6000
AnimationFPS=30
```

## Suspected cause (not confirmed)

With `PlayerOnly=false` and `TextureScale=Full`, each NPC with enchanted gear inside 6000 units gets its own set of full-resolution RGBA8 render targets. A search of `RenderTargetPool.cpp` and `RenderTargetPool.h` for a byte limit or a budget check found none. I did not search the rest of the code. The slot numbers above 160 suggest that 30 or more targets were live at once, which fits several enchanted NPCs in combat. I did not check which actors or items received targets, because the BEE log for that session no longer exists.

Two things remain unexplained:
- Why does each burst create about 880 MiB and destroy about 480 MiB, instead of reusing the pool's targets? It looks like an evict-and-rebuild cycle, possibly caused by `CullOutOfView` or `EvictDistance` changing as actors move in and out of view or range.
- Are targets released when the effects of an actor are dropped? After 17:27:00, the process held between 96 and 113 live 2K RGBA8 render targets, which includes BEE's targets and those of other callers. So the targets do not leak without limit, but the total did not go down while the game was over budget.

## Requests

1. Limit the total VRAM that the render-target pool can use. When the limit is reached, lower the resolution or drop effects for actors farther away. This is better than continuing to allocate, because over-budget allocation makes every frame slow, not only the frames with BEE effects.
2. Find out why each burst destroys and creates targets again instead of reusing them.
3. Consider a smaller format. RGBA8 at 2K with mips uses about 21 MiB per target. A BC format is not possible for render targets, but a lower resolution for NPCs is.
4. Add trace events that are always written for pool growth, with bytes, slot index, actor and item. The overwritten log meant that this incident could not be connected to specific actors.

## Reproduction (not yet confirmed)

Use the settings above. Fight a group of NPCs who wear enchanted armor, outdoors, with DynDOLOD LOD loaded and VRAM already at about 85% of budget. Watch `VramProfiler.csv` for `counted_video_mib` going above `local_budget_mib`.
