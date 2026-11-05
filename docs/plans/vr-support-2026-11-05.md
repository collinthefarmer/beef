Status: deferred (2026-11-05) by the user. Nothing is implemented. Start it only when a Skyrim VR install is available for in-game runs. It is step 5 and open question Q1 of `docs/plans/version-agnostic-2026-11-05.md`. Facts were read at e99ea2b on branch `commonlib-fork`, with CommonLib at `alandtse/CommonLibVR` `cd21580`.

# Skyrim VR support

The goal is that the same DLL also loads and works on Skyrim VR 1.4.15, or
refuses it with a message that names it.

## How it works today

| Part | Rule today | Source |
|---|---|---|
| Compiled runtimes | Both profiles compile SE and AE. `compiled.vr` is false, so CommonLib builds with `ENABLE_SKYRIM_VR` off. | `cmake/compatibility/*.json`, `cmake/Windows.cmake` |
| Startup guard | Accepts SE 1.5.x and AE 1.6.317 to `SKSE::RUNTIME_SSE_LATEST_AE`. It refuses VR with the runtime message. | `cmake/Compatibility.cmake`, `src/main.cpp` |
| Minimum SKSE | One value per profile, 2.0.17 for `address-library`. | profile `minimum_skse` |
| Hook | `PlayerCharacter::Update` at vtable slot `0xAD`, a constant. | `src/engine/Hooks.cpp` |
| Own relocations | Four two-ID `REL::RelocationID` values: the `NiPointLight` constructor and `SetAttenuation`, and `ShadowSceneNode` add and remove light. | `src/render/Light.cpp` |
| Flattened bone tree | `REL::RelocateMember` with the SE/AE and VR offsets (0x130/0x158, 0x128/0x150). | `src/render/SkinPalette.cpp` |
| Community Shaders layout | One copied header, the 1.8.3 SE/AE baseline. The layout check reads texture slot types only. | `src/cs/BSLightingShaderMaterialPBR.h` |
| Editor | Registers only when every SKSE Menu Framework function it calls is exported; otherwise effects run without it. | `engine/MenuDependency.cpp` |
| Source archive | `git archive` of each dependency. It omits submodules; CommonLib's one submodule, `extern/openvr`, is unused while VR is off. | `tools/source-archive.py` |

## Findings

| # | Finding | Evidence |
|---|---|---|
| R1 | On VR, a two-ID `RelocationID{se, ae}` resolves through the SE ID in the VR Address Library. | fork `include/REL/ID.h`, `_vrID = a_seID` |
| R2 | The four SE IDs in `Light.cpp` (69583, 17224, 99692, 99698) exist in the VR 1.4.15 database. That they name the same functions on VR is the database's design, not checked per ID. | fork `tests/REL/version-1-4-15-0.csv` |
| R3 | The fork gives VR addresses for `VTABLE_PlayerCharacter`, `VTABLE_NiSkinData`, `VTABLE_NiAlphaProperty` and `NiRTTI_BSFlattenedBoneTree`. | fork `include/RE/Offsets_VTABLE.h`, `Offsets_NiRTTI.h` |
| R4 | `Actor::Update` is slot `0xAD` on SE and AE and `0xAF` on VR. | fork `src/RE/A/Actor.cpp`, `RelocateVirtual(0x0AD, 0x0AF, ...)` |
| R5 | With VR enabled beside SE and AE, CommonLib makes per-runtime virtuals non-virtual and reaches moved members only through runtime accessors. Direct reads of a moved member then fail to compile. | fork `include/REL/Common.h`, `SKYRIM_REL_VR_VIRTUAL` |
| R6 | SKSE VR is numbered 2.0.x below 2.0.17, so the single `minimum_skse` refuses every SKSE VR build. | profile `minimum_skse`; guard in `src/main.cpp` |
| R7 | VR on links `openvr_api.lib` from `extern/openvr`, and the archive then needs the submodule's contents. openvr is BSD-3-Clause. | fork `CMakeLists.txt` |
| R8 | Community Shaders' VR material layout, SKSE Menu Framework's VR availability, and the plugin's frame cost at VR's two eyes and frame rate are not examined. | |

## Steps

| # | Work | Checked by |
|---|---|---|
| 1 | Probe: configure a separate build directory with `compiled.vr` true and build once. Count and classify the compile errors (R5). About 20 minutes of CommonLib compile; one heavy job. | Windows build |
| 2 | Fix the plugin's errors from step 1 through CommonLib's runtime accessors. | Windows build; `tools/tidy.py --check` |
| 3 | Hook slot per runtime: `0xAD` on SE and AE, `0xAF` on VR (R4). | Windows build; hook log line in game |
| 4 | Profile shape: `minimum_skse` per runtime family (R6), `compiled.vr` true, and a VR 1.4.15 check and name in the generated guard. | `tests/tools/compatibility_tests.py` |
| 5 | Source archive takes `extern/openvr`; openvr's license joins `THIRD_PARTY_NOTICES.md` and `licenses/` (R7). | `tests/tools/source_archive_tests.py` |
| 6 | Community Shaders VR: read CS's VR material layout against the copied header; extend the layout check if it differs (with version-agnostic Q2). | review; CS source |
| 7 | In game on VR: load, hook, one effect per output kind, shells on a skinned piece, lights, the editor if SKSE Menu Framework loads, and frame cost against a nothing-equipped baseline. | in-game run |
| 8 | Docs: release notes, tester README, `docs/build.md`, `docs/source-archive.md`, and `REFERENCE.md` for each fact found. | review |

## Open questions

| # | Question | Notes |
|---|---|---|
| Q1 | Is a VR install available? | Without one, steps 3 to 7 rest on R1 to R5 only. This is the start condition. |
| Q2 | Does SKSE Menu Framework have a VR build? | Without one, VR runs effects with no editor; the menu dependency check already allows that. |
| Q3 | Do the fps budget defaults suit VR? | The budgets and stress figures are from flat Skyrim. |
