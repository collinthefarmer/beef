Status: active (2026-11-05) on branch `commonlib-fork`, so `main` and its candidate stay releasable. Steps 1 and 2 are done (9aac44f), step 3 (a0aa8f5) and step 4 (e99ea2b). Step 5 is decided: VR is deferred to `docs/plans/vr-support-2026-11-05.md`. Step 6 is closed with no code change: a header review at each Community Shaders release replaces a run-time scalar check. Nothing is seen in game yet.

# A version-agnostic plugin

The goal is one DLL that loads and works on every Skyrim runtime and peer
version it can support safely, refuses the rest with a message that names
them, and needs no rebuild for a new peer version.

## How it works today

| Part | Rule today | Source |
|---|---|---|
| Skyrim runtime | The `address-library` profile declares `SKSE::VersionIndependence::AddressLibrary`. The startup guard accepts SE 1.5.x and AE 1.6.317 and later 1.6 releases. It refuses 1.7 and VR. | `cmake/Compatibility.cmake`, `cmake/compatibility/address-library.json`, `src/main.cpp` |
| SKSE | Minimum 2.0.17, the oldest SE build. Each SKSE build is made for one runtime. | the profile |
| Community Shaders | No version check. A run-time layout check turns effects off when a PBR material's texture slots are not all `NiSourceTexture`. | `Manager::NoteMaterialLayout`, `engine/ManagerApply.cpp` |
| SKSE Menu Framework | No version check. The editor registers only when every function it calls is exported. | `engine/MenuDependency.cpp` |
| Messages | One place for the player-facing text of each refusal. | `engine/PeerMessages.cpp` |
| CommonLib | `CharmedBaryon/CommonLibSSE-NG` at `b93280e` (2024-09-03), which is that repository's HEAD. It names AE runtimes up to 1.6.1330 and none in 1.7. | `cmake/Windows.cmake`, profile `dependencies.commonlib` |

## Findings

| # | Finding | Evidence |
|---|---|---|
| V1 | Every Address Library ID the plugin itself uses exists on SE 1.5.97, AE 1.6.1170, 1.7.99 and 1.7.104: the `NiPointLight` constructor (SE 69583, AE 70967), the `PlayerCharacter`, `NiAlphaProperty` and `NiSkinData` vtables, and the `BSFlattenedBoneTree` RTTI. | `tools/address-library-ids.py` over Address Library All in One v13 |
| V2 | Some functions change ID in 1.7. The maintained fork handles them per runtime, for example `AE_CHECK(SKSE::RUNTIME_SSE_1_7_99, 77235, 524254)`. In the databases, 77235 exists up to 1.6.1170 and 524254 only from 1.7.99. | fork commit `6d61bd5`; the tool |
| V3 | 1.7.99 moves members of `PlayerCharacter` by 8 bytes (for example `0x2D8` to `0x2E0`). The pinned CommonLib reads the old offsets on 1.7. | fork `include/RE/P/PlayerCharacter.h`, `RelocateMemberIfNewer` |
| V4 | `PlayerCharacter::Update`, the plugin's one hook, stays at vtable slot `0x0AD` on SE and AE in the fork, which marks other per-runtime slots. | fork `include/RE/A/Actor.h:401` |
| V5 | The maintained NG fork is `alandtse/CommonLibVR`, branch `ng`, release 15.7.0 at `cd21580` (2026-11-05). It names `RUNTIME_SSE_1_7_99` and `RUNTIME_SSE_1_7_104` as the latest AE runtimes. | `include/SKSE/Version.h` |
| V6 | The fork's CMake calls `find_package(spdlog CONFIG REQUIRED)` and `find_package(directxtk CONFIG REQUIRED)`. This build fetches spdlog with FetchContent and has no DirectXTK. The fork uses DirectXTK only for `SimpleMath.h` (`RE/S/State.h`); `DirectXMath` comes with the Windows SDK. | fork `CMakeLists.txt:37-39` |
| V7 | The flattened-bone-tree offsets the shells use (0x128/0x130 on SE and AE) are derived from the NiNode size, which only VR changes; NiNode is not among the fork's 1.7 layout changes. | `REFERENCE.md`, the shell skinning passage |
| V8 | The tested install runs Community Shaders 1.9.1 while the copied header is from the 1.8.3 baseline, and the layout check passes. The check reads texture slot types only; it does not see a moved scalar field. | 2026-11-05 runs; `src/cs/BSLightingShaderMaterialPBR.h` |

## Steps

| # | Work | Checked by |
|---|---|---|
| 1 | Fetch `alandtse/CommonLibVR` `ng` at a pinned commit: the repository joins the profile's `dependencies`, beside the revision. Declare spdlog with `OVERRIDE_FIND_PACKAGE`. Fetch DirectXTK at a pinned tag and define a small `Microsoft::DirectXTK` target from its `Inc/` headers and `Src/SimpleMath.cpp` before CommonLib's `add_subdirectory`. | Windows configure |
| 2 | Fix the plugin's compile errors from the two-year CommonLib jump. | Windows build; `tools/tidy.py --check` |
| 3 | Add the fork's and DirectXTK's licenses and provenance: `THIRD_PARTY_NOTICES.md`, `licenses/`, `tools/source-archive.py`, the license tests. | `tests/tools/licenses_tests.py`, `source_archive_tests.py` |
| 4 | Widen the guard to the newest AE runtime the pinned CommonLib names, read from `SKSE::RUNTIME_SSE_LATEST_AE` in `main.cpp`, so a CommonLib bump widens it and nothing else can. The supported-runtime text then names the range up to that runtime. | `tests/tools/compatibility_tests.py`; a native message test |
| 5 | VR: deferred by the user to `docs/plans/vr-support-2026-11-05.md`. The guard keeps refusing VR. | |
| 6 | Closed with no code change (Q2). The layout check stays a texture-slot check. Each new Community Shaders release's header is diffed against `src/cs/` before the release notes name it as tested; `docs/build.md` holds the step and `REFERENCE.md` (Bindings) what the check misses. | review |
| 7 | Regression: native and sanitized suites, the release gate, and the full in-game suite on 1.6.1170 with `--timeout 3600`, against the 2026-11-05 run of the c8ddb85 candidate. 1.7 is "loads, not tested in game" unless a 1.7 install is available. | `tools/regression-run.py`, census scan |
| 8 | Docs: the release notes' "Loads with" column, the tester README, `docs/build.md`'s profile section, and `REFERENCE.md` for each fact found. | review |

## Open questions

| # | Question | Notes |
|---|---|---|
| Q1 | Build VR as well? Deferred: see `docs/plans/vr-support-2026-11-05.md`. | The fork supports VR. The plugin then needs VR's hook slot (it differs from 0xAD), VR's flattened-tree offsets (0x150/0x158 per REFERENCE.md), and VR IDs for its own relocation; Community Shaders' VR layout is unknown. |
| Q2 | Should the layout check verify the scalar fields the plugin writes, not only the texture slot types? | Closed 2026-11-05: no. A plausibility check misses the one change that can crash (a pointer replacing two floats), and a version gate needs a rebuild per Community Shaders release. The header is reviewed per release instead; it is unchanged from 1.8.3 to 1.9.1. |
| Q3 | Is a 1.7 install available for an in-game run? | Without one, 1.7 support rests on V1 to V5. |
| Q4 | Does the GOG runtime need anything? | Not examined. Address Library publishes per-executable databases; the guard reads the runtime version only. |

## State when this plan was written

- `main` is at ca83b46: c8ddb85 plus this plan and `tools/address-library-ids.py`. The release gate passed on c8ddb85: 190 sanitized suites, the Windows build, tidy at the 84-finding baseline.
- The candidate archive `BetterEnchantmentEffects-0.1.0-address-library-c8ddb8547bf4-Release.zip` (SHA-256 `307610362c5558e711ca5a994c5e77f3e60e6f4e9cad1a67fce9bae74b9107ce`) is installed in the MO2 mod `BetterEnchantmentEffects 0.1.0 candidate`, used by the profile `beef candidate`.
- Not yet seen in game: the new peer messages of 8ac7ead. Two launches of `beef candidate`, one with Community Shaders off and one with SKSE Menu Framework off, show them; restore `modlist.txt` after each (it has CRLF line endings).
- Still open for the alpha, outside this plan: the crowd-fight environment fault (fighters never draw weapons, with any build since 2026-11-05 04:00), the owner's visual pass, the supported-workload draft, and the squashed public history.
