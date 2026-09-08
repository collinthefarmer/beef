# Assumptions that need the game to verify

Each item names what the code relies on, where the belief comes from, and
what happens if it is wrong.

1. **Vanilla `SetupGeometry` uploads `*property->emissiveColor * emissiveMult`
   as the shader's `EmitColor`, and only when `kOwnEmit` is set.**
   Basis: the brief; `reference/community-shaders/Lighting-emissive-excerpt.hlsl`
   (`Color::EmitColor(EmitColor)` multiplied by the glow sample);
   `RE/B/BSLightingShaderProperty.h` members `emissiveColor` (0xF0, a
   `NiColor*`) and `emissiveMult` (0xF8). The `kOwnEmit` gating is inferred
   from the flag's name (`RE/B/BSShaderProperty.h` bit 22); the plugin sets it
   while applied (`src/EffectManager.cpp` ApplyGeometry) and restores it.
   If wrong: the glow is constant colour or invisible; the fix is confined to
   `WriteFrame`/`ApplyGeometry`.

2. **CS sets `HasEmissive` and binds slot 6 from `material->emissiveTexture`
   on every draw whenever it is non-null and not `defaultTextureBlack`.**
   Basis: `reference/community-shaders/TruePBR-SetupMaterial-excerpts.cpp`
   (`hasEmissive` block). The plugin never binds black, so the flag is always
   on for driven geometry. Texture address mode comes from the armor material's
   `textureClampMode` (same excerpt); the flipbook assumes wrap.

3. **The CS PBR material layout matches `src/cs/BSLightingShaderMaterialPBR.h`.**
   Basis: the pinned commit `dd2677fc` (2026-08-25) and the installed v1.8.3
   tag (`2f2919a7`) have byte-identical member blocks (diffed on 2026-09-03);
   `GlintParameters` from `src/TruePBR.h` at the same commit. `src/PBRMaterial.h`
   static_asserts `emissiveTexture` at 0xE0 and `sizeof == 0x148`. Runtime
   check: all five PBR texture slots are non-null `NiSourceTexture`s, which
   `ReceiveValuesFromRootMaterial` guarantees
   (`reference/community-shaders/BSLightingShaderMaterialPBR.cpp`). If the check
   fails the emissive path is disabled for the session and an error is logged.

4. **A CS PBR material is recognised by `kVertexLighting` on the property plus
   feature `kDefault`/`kMultiTexLandLODBlend`, and its vtable differs from
   vanilla `BSLightingShaderMaterial`'s.**
   Basis: `reference/community-shaders/TruePBR-GetRenderPasses-excerpt.cpp`;
   `BSLightingShaderMaterialPBR::GetFeature` returns `kDefault`
   (`BSLightingShaderMaterialPBR.cpp`), so the feature alone cannot separate
   the two, hence the vtable compare against `RE::VTABLE_BSLightingShaderMaterial[0]`
   in `src/PBRMaterial.h`. A vanilla mesh with `kVertexLighting` set is thus
   skipped instead of read past its 0xA0 bytes.

5. **Materials are pooled, so a worn clone can share its material with other
   clones; `BSShaderProperty::SetMaterial(material, true)` installs a private
   copy made with `Create()` + `CopyMembers()`.**
   Basis: the `MaterialExtensions::lastOwnerRefFormID` comment in the CS header
   ("pooled material instance would be overwritten by a different ref,
   triggering a clone"); `RE/B/BSShaderProperty.cpp:11` (RELOCATION_ID
   98897/105544) is only a thunk, so the `unique` semantics are from memory
   of NiOverride's use. The code checks the pointer changed and, if not, makes
   the copy itself and calls `SetMaterial` again; if it still did not change it
   logs `could not give ... a private material` and proceeds shared. Restore
   uses `SetMaterial(original, true)`, giving the property a fresh copy of the
   original (never the pooled instance itself). Checklist item 2 is the test.

6. **`GetModuleHandleW(L"CommunityShaders.dll")` is the right presence test.**
   Basis: the installed file name under
   `mods/Community Shaders 1.8/SKSE/Plugins/CommunityShaders.dll`. A renamed
   DLL would idle the plugin with a clear log line.

7. **`BSShaderManager::GetTexture` accepts both the raw EFSH `ICON` path and a
   `textures\...` prefixed path.**
   Basis: the original passes `fillTexture.textureName` unchanged
   (`decompiled/WornEnchantmentFX/plugin.c` 4332-4380). Flipbook frames are
   requested as `textures\WornEnchantmentPBR\<name>\frame_<i>.dds`. If the
   prefix form fails, `flipbook: ... exists on disk but failed to load` appears
   and the fallback texture is used.

8. **Frame discovery via `std::filesystem::exists` on
   `Data/Textures/WornEnchantmentPBR/<name>/frame_<i>.dds` works under MO2's
   usvfs and the working directory is the game folder.**
   Basis: usvfs hooks the Win32 file APIs the STL uses; every SKSE plugin that
   reads `Data/SKSE/Plugins/*.ini` by relative path relies on the same. If
   wrong, no frames are found and the fallback texture is used (logged).

9. **`RE::GetDurationOfApplicationRunTime()` is an acceptable clock in place
   of `BSTimer::smoothedRunTimeMS`.**
   Basis: the decompile reads that member (`plugin.c` 416) but the pinned
   CommonLibSSE-NG `RE/B/BSTimer.h` does not expose it. Both are millisecond
   counters; only the elapsed difference is used, so drift between them does
   not matter.

10. **`TESEquipEvent`, `TESObjectLoadedEvent` and SKSE's `NiNodeUpdateEvent`
    are dispatched on the game thread.**
    Basis: the original mod handles them the same way (`plugin.c` 1383-1508).
    The queue structures are still mutex-guarded; all scene mutation goes
    through `SKSE::GetTaskInterface()->AddTask`.

11. **The biped `partClone`'s geometries carry their lighting property in
    `properties[States::kEffect]`.**
    Basis: `RE/B/BSGeometry.h` (`properties[kTotal]`, `kEffect` = 1) and the
    decompile reading the property at a runtime-dependent offset
    (`all.c` 16843-16846). Iterating all 42 `BIPOBJECT` slots
    (`RE/B/BipedObjects.h` `kTotal`) instead of the original's 32 only adds
    the extra editor slots, which have no `partClone`.

12. **`PlayerCharacter::Update` (vfunc 0xAD, `RE/A/Actor.h:371`) runs once
    per frame on the game thread while the game is unpaused, and a vtable
    write on `VTABLE_PlayerCharacter[0]` is a safe place to hook it.**
    Basis: the vfunc index is CommonLibSSE-NG's for SE/AE (VR is not built);
    the vtable comes from the Address Library. This replaced the first
    design, where the tick re-posted itself through `SKSE::TaskInterface`:
    SKSE's `BSTaskPool::ProcessTasks` (skse64 `Hooks_Threads.cpp`) loops
    `while (!IsTaskQueueEmpty())`, so a self-reposting task spins forever
    inside one frame, which froze the game on 2026-09-03 right after
    `PBR material layout check passed`. Equip finalizes are now due times
    checked from the same per-frame hook. If the hook index were wrong the
    game would crash at the first player update, not freeze.

13. **The emissive glow sample is multiplied by vertex colour (AO) and, without
    linear lighting, converted sRGB<->linear.**
    Basis: `Lighting-emissive-excerpt.hlsl`. A mesh with dark vertex colours
    will glow dimmer than the membrane shader would; `EmissiveScale` is the
    only knob for that in the alpha.

14. **`emissiveColor` is never null on a lighting property that renders.**
    Basis: the field is a pointer (`BSLightingShaderProperty.h` 0xF0) that
    `LoadBinary` allocates. Geometry with a null pointer is skipped and logged
    under verbose rather than dereferenced.

15. **Address Library IDs used indirectly (`SetMaterial`, `GetTexture`,
    `GetDurationOfApplicationRunTime`, `ProcessLists`, `PlayerCharacter`)
    resolve on 1.6.1170 through the pinned CommonLibSSE-NG.**
    Basis: `versionlib-1-6-1170-0.bin` is installed and the library release
    predating 1.6.1170 only needs the database, not new IDs, for these
    functions. A missing ID would show as a crash at first use, with the PDB
    shipped next to the DLL for the crash logger.

16. **A linear EFSH-alpha to emissive mapping is too weak for vanilla records,
    so `EmissiveScale` defaults to 20.**
    Basis: `EnchArmorMagickaFXS` (00092DED), `EnchArmorStaminaFXS` (00092DEE)
    and `EnchArmorHealthFXS` (00092DEC) read from Skyrim.esm on 2026-09-03:
    source blend 5 (src alpha), dest blend 2 (one), fill alpha ratios
    0.05/0.10/0.05, fill colours grey 102/0/45, colour scale 1.5, texture
    scale 3/2/2, V scroll only (0.1/0.05/0.05), flags 0x3000C (greyscale to
    alpha via the NAM8 palette). The membrane adds colour x texture x alpha,
    about 0.03, which after CS's sRGB-to-linear conversion
    (`Lighting-emissive-excerpt.hlsl`) is nothing. The first in-game run with
    `EmissiveScale=1` applied to six geometries and showed nothing, consistent
    with this. `DebugSolidGlow` exists to separate "channel broken" from
    "values too small".

17. **The greyscale-to-alpha palette (EFSH flag bit 2, NAM8 gradient) is not
    applied; the fill texture's RGB is used as the glow shape directly.**
    Basis: `RE/T/TESEffectShader.h` `kGreyscaleToAlpha`; the vanilla membrane
    remaps the fill texture's grey through the palette's alpha. The three
    vanilla textures are grey swirl/cloud/vapour maps whose RGB already carries
    the shape, so the visual difference is contrast, not layout. Baking the
    palette into the flipbook frames is a `make_flipbook.py` change if needed.

18. **Setting `pbrFlags` Fuzz, `fuzzColor`/`fuzzWeight`, `glintParameters` and
    `specularColorScale` on a live PBR material takes effect on the next draw,
    and Fuzz/Glint on a material without TwoLayer or HairMarschner is a valid
    combination.**
    Basis: CS `src/TruePBR.cpp` (v1.8.3, lines 894-960) reads all of them in
    `SetupMaterial` every draw and picks coat > hair > (subsurface + fuzz |
    glint); `PBR.hlsli` 168-172 and 244-247 show fuzz as a specular lerp plus
    an ambient add. Glint additionally flips the technique in
    `GetRenderPasses` (`TruePBR-GetRenderPasses-excerpt.cpp`), so its first
    use compiles a shader variant. Unverified: whether the armor's own
    roughness map interacts badly with fuzz on metals (it may look like
    velvet at high weight; `SheenScale` is the knob).

19. **Greyscale-to-palette-colour samples the palette along U by texel grey,
    middle row.**
    Basis: the flag name and the palette textures (256x64 gradients along U).
    The exact V coordinate the vanilla effect shader uses was not checked;
    the gradients vary little across rows, so the baked tint is close either
    way. Only records with flag bit 1 (fire among the vanilla armor set) need
    it; others get their hue from the edge colour at runtime.

20. **SKSE Menu Framework render callbacks run on the game thread, so the
    menu may edit the settings struct and read the applied-effect map without
    locking.**
    Basis: the framework draws from the D3D11 present hook, which Skyrim
    calls from its main thread, and the framework's own examples mutate game
    state from render callbacks. Anything structural still goes through
    `ReapplyAll`, which only posts tasks. If this is wrong the symptom is a
    torn read in the Effects page, not a crash in the render path.

21. **Vendoring `SKSEMenuFramework.h` (GPL-3.0) is acceptable for this
    private build.** Publishing the plugin would make it GPL-3.0 or require
    re-deriving the small API surface (`SetSection`, `AddSectionItem`, the
    handful of ImGui wrappers used) independently.

22. **An `NiSourceTexture` loaded from a placeholder DDS keeps working after
    its `rendererTexture` is pointed at a `RendererData` we allocated
    (`new` on the engine heap via `TES_HEAP_REDEFINE_NEW`) whose `texture`
    and `resourceView` are our own D3D11 objects.**
    Basis: `RE/N/NiTexture.h` RendererData layout (texture at 0, SRV at 0x10,
    width/height at 0x18) and CS binding only `rendererTexture` when it sets
    PS textures (`TruePBR.cpp` 890, 988, 997, 1006). The other RendererData
    fields keep the constructor defaults copied from the engine. The original
    pointer is restored in `Target::~Target` before the shell reference drops.
    Risk: the texture manager touching the renderer data of a cached texture
    (streaming, unload). We hold a reference for the target's lifetime.

23. **Using the immediate context from `PlayerCharacter::Update` is safe when
    every state we set is restored.** Basis: Skyrim renders from the main
    thread, and ENB/CS-style plugins draw from hooks on the same context. The
    pass saves render targets, viewport, shaders, SRV 0, sampler 0, constant
    buffer 0, input layout, topology, blend, depth-stencil and rasterizer
    state (`RuntimeTextures.cpp` SavedState). If the engine relies on a state
    we do not capture, the symptom is corrupted rendering the frame after an
    apply, and the fix is adding that state to SavedState.

24. **CS treats `featuresTexture1` as a fuzz map when it is non-null and not
    the default white, and `displacementTexture` as a heightmap when non-null
    and not the default black; a displacement scale (`rimLightPower`) of
    0.1 is a mild parallax.** Basis: `TruePBR.cpp` 987-1009 and
    `Lighting.hlsl` 1121-1147, 1804-1812. The scale semantics of
    `GetParallaxCoords` were not read; `ShimmerScale` is the knob.

Items 22-24 were confirmed in game on 2026-09-03: shells render, the frame
after an apply is intact, sheen map and shimmer show.

25. **Sampling the fill texture's 1x1 mip after `GenerateMips` gives its mean
    luminance.** Basis: box-filtered mip chains average their inputs. The
    readback is one staging copy per source texture, cached.

26. **RMAOS blue (ambient occlusion) is a usable relief proxy when the armor
    has no displacement map, and the engine's default black texture is
    `BSGraphics::State` offset 0x58.** WRONG on the second half, found
    2026-09-04: offset 0x58 held 0x1010000 on 1.6.1170 and dereferencing it
    crashed the game (`Outputs.cpp` shell setup). No code reads it now; a
    placeholder map is recognised by its 1x1 size alone. Basis: CS's RMAOS layout comment in
    `cs/BSLightingShaderMaterialPBR.h` (AO in b) and `RE/S/State.h` ("black?"
    at 0x58; `defaultTextureWhite` at 0x60 is confirmed by CS's own use).
    If 0x58 is not black, an armor with the default displacement texture is
    treated as having a real one and its (black) map yields a flat relief;
    the verbose line `shimmer on '<geometry>': depth from ...` says which
    input was chosen. Armor textures are sampled with the raw mesh UV, the
    same coordinates CS uses for every layer.

27. **Replacing `rmaosTexture` with an R8G8B8A8 re-render of the armor's RMAOS
    is visually equivalent apart from resolution and compression.** Basis:
    CS reads RMAOS as `float4` r/g/b/a (`Lighting.hlsl` 1326, 1715-1738) and
    binds slot 5 from the material pointer each draw (`TruePBR.cpp` 890);
    the pass copies g, b and a unchanged. The target is square at the
    armor's larger dimension capped by `GlossMapSize`, so a non-square RMAOS
    is resampled; sampling is by mesh UV, so only detail, not placement, is
    affected. Cost: one pass of that size per geometry per tick.

28. **The framework's `ImTextureID` is a D3D11 shader resource view pointer.**
    Basis: the SDK types it as `void*` and the framework's backend is
    `imgui_impl_dx11`, whose texture id is `ID3D11ShaderResourceView*`. The
    Inspect page draws our targets and the armor's own textures through it.
    If wrong, thumbnails render blank or the framework logs an error; nothing
    else depends on it. Channel thumbnails are 128 px targets from the shell
    pool (up to five per expanded slot), cleared when the selection changes.

Items 29-33 cover the light and shell output spikes (`src/Outputs.cpp`),
added 2026-09-04 and not yet run in game.

29. **The Address Library IDs for `NiPointLight`'s constructor (69583/70967),
    `NiPointLight::SetLightAttenuation` (17224/17626),
    `ShadowSceneNode::AddLight(NiLight*, LIGHT_CREATE_PARAMS&)` (99692/106326)
    and `ShadowSceneNode::RemoveLight(const NiPointer<BSLight>&)`
    (99698/106332) are correct, and `LIGHT_CREATE_PARAMS` is the 0x30-byte
    struct with powerof3's field names.**
    Basis: powerof3's CommonLibSSE fork (`dev` branch, fetched 2026-09-04:
    `src/RE/S/ShadowSceneNode.cpp`, `include/RE/N/NiPointLight.h`,
    `include/RE/S/ShadowSceneNode.h`); the pinned CommonLibSSE-NG has the same
    struct layout under placeholder names. If an ID is wrong the game crashes
    at the first light creation or removal, with `Outputs.cpp` in the crash
    log. The light is created on the third-person effect only, hung from
    `NPC Spine2 [Spn2]` by default, with `neverFades`, `affectLand` and
    `affectWater` set and no shadow.

30. **Writing `NiLight::diffuse` and `fade` each tick is enough to animate a
    registered light.** Basis: Light Placer animates its lights the same way.
    The radius is re-applied through `SetLightAttenuation` only when the
    setting changes.

31. **`NiAVObject::Clone()` on a skinned `BSTriShape` yields a geometry that
    shares the original's GPU buffers and skin instance (or a skin clone bound
    to the same skeleton), with its own `BSLightingShaderProperty` and its own
    `emissiveColor` storage.** Basis: overlay mods (NiOverride) clone body
    parts this way. The apply line's `shell (skin ..., buffers ..., emissive
    storage ...)` reports which of these held. If the clone shares its property
    with the original the shell is dropped and logged. If the property clone
    shares `emissiveColor`, the shell allocates its own (`shared emissive`
    reads `own`); that allocation is left to the property's destructor.

32. **An `NiAlphaProperty` built by hand (engine heap, vtable from
    `VTABLE_NiAlphaProperty`, zeroed `NiObjectNET`, flags via the library's
    setters) is accepted by the renderer and destroyed cleanly when its
    refcount drops.** Basis: the class has no members beyond `alphaFlags` and
    `alphaThreshold`, and a null `BSFixedString` name is what an unnamed
    property holds anyway. Risk: the engine destructor touching something the
    constructor would have set; the symptom is a crash on retire.

33. **A cloned lighting property with `kVertexLighting` cleared, a vanilla
    `BSLightingShaderMaterial` installed through `SetMaterial(..., true)`,
    `kRimLighting`, `kOwnEmit`, `kDecal` set and `kZBufferWrite` cleared, then
    `SetupGeometry` + `FinishSetupGeometry` + `DoClearRenderPasses`, renders as
    a vanilla-lit alpha-blended shell over the PBR original without z-fighting.**
    Basis: CS decides PBR by `kVertexLighting` plus the material vtable
    (item 4), so the shell takes the vanilla path; `kDecal` selects the
    depth-biased rasterizer state the engine uses for decals. Unverified:
    whether the vanilla rim term shows at all under CS's lighting (it is lit by
    scene lights, so it is dark in darkness), and whether CS's own
    `GetRenderPasses` hook needs anything more from the shell's flags.
    CS's `Lighting.hlsl` (fetched 2026-09-04, `diffuseColor += emitColor`
    after `diffuseColor += lightsDiffuseColor`, then multiplied by the base
    colour) adds rim and emissive into the lit diffuse and multiplies by the
    albedo, so the shell's diffuse defaults to the engine's white texture
    (`defaultTextureWhite`, 0x60). A consequence: the vanilla lighting shader
    cannot isolate the rim; the shell always adds ordinary diffuse lighting
    too, scaled by its alpha. A view-dependent-only term needs the EFSH
    membrane's edge effect on the shell instead (spike 3, not built).

34. **`NiSkinData::BoneData::verts` counts the vertices a bone influences,
    `bound.center` is that set's centre in the bone's space, and
    `NiSkinInstance::bones[i]` is the skeleton node for bone i.** Basis: the
    pinned headers (`NiSkinData.h`, `NiSkinInstance.h`) and Gamebryo's skin
    layout. The light targets are the bones with the most skinned vertices
    across the effect's geometries; if `bound.center` is in another space the
    lights sit off the armor and `LightUseBound=false` puts them on the bone
    origins.

35. **Skyrim bones point along their local X axis, the renderer rebuilds the
    bone matrices from `skinToBone` every frame, and the cloned skin instance
    of a shell either owns its `NiSkinData` or accepts a hand-copied one
    (engine heap, vtable from `VTABLE_NiSkinData`, per-bone vertex lists
    nulled so the destructor frees only the bone array).** Basis: the
    skeleton convention of the vanilla rigs; `NiSkinInstance::boneMatrices`
    with `frameID` in the header. The shell scales rows Y and Z of each
    bone's `skinToBone` rotation and translation, which pushes vertices away
    from the bone axis. If bones used another axis the shell would stretch
    along the limb instead. The apply line's `inflation ...` says which skin
    data is edited; `none (skin instance shared)` means the clone shares the
    original's instance and inflation is off to protect the armor.

36. **CS TruePBR draws an alpha-blended geometry whose material is a private
    `Create()` + `CopyMembers()` copy of a PBR material, with the layers'
    emissive, fuzz, displacement and RMAOS writes taking effect on it as they
    do on the armor.** Basis: the copy has the PBR vtable and layout (item 3)
    and `kVertexLighting` stays set, so CS's test (item 4) passes; the alpha
    property only changes the blend state. Unverified: whether CS's PBR pass
    handles the decal flag (`ShellDepthBias`) the same way as the vanilla
    path, and whether the additive blend keeps the emissive readable. If the
    shell renders opaque or not at all in this mode, the first things to try
    are `ShellDepthBias=false` and `ShellBlend=1`.
    Confirmed in game 2026-09-04 with the defaults (additive, depth bias on).

37. **The armor's RMAOS green channel is metallic under CS TruePBR, and the
    per-geometry masked glow target (scrolled fill x mask, up to the RMAOS
    resolution) can replace the shared effect target as the glow layer's
    frame without any other change.** Basis: CS's RMAOS layout comment
    (`cs/BSLightingShaderMaterialPBR.h`: roughness, metallic, AO, reflectance)
    and the gloss map already binding a per-geometry target in a material
    slot. The mask source is the RMAOS captured at apply time, before the
    gloss map layer swaps the slot. Cost: one pass per masked geometry per
    tick at the larger of the runtime size and the RMAOS size.

Items 38-40 cover the recipe store added by phase 1 of the compositor
rewrite (`src/RecipeStore.cpp`, 2026-09-04), not yet run in game.

38. **Files written by relative path under `Data/WornEnchantmentPBR/imported/`
    from the game folder land somewhere the next read by the same path finds
    them.** Basis: item 8 for reads; MO2's usvfs redirects new files under
    `Data` to its overwrite folder and serves them back on later reads. The
    store creates the folder, writes each imported recipe, reads it back and
    compares. If wrong: `could not write` lines, or a `READ-BACK MISMATCH`,
    or a second run importing everything again (`N imported` twice).

39. **`TESDataHandler::GetFormArray<EnchantmentItem>()` holds every loaded
    enchantment at `kDataLoaded`, and `GetCastingType() == kConstantEffect`
    selects the worn (armor) ones; `ShaderFor` then yields the EFSH the
    proof of concept binds at equip time.** Basis: the form arrays are
    filled during load and `kDataLoaded` fires after; the vanilla armor
    enchantments are constant-effect, weapon ones fire-and-forget. If wrong:
    no `imported recipe` lines, or weapon shaders (`EnchWeapon*FXS`) imported
    too, which only costs a file each.

40. **The values `tools/efsh_dump.py` reads from the ESM equal what the
    runtime `EffectShaderData` holds: the file layout stores the addon-model
    and ambient-sound form ids in 4 bytes each where the runtime holds
    8-byte pointers, so every field after 0xF4 sits 16 bytes earlier in the
    file (colour keys 2 and 3 at 0x138, scales 0x140, times 0x14C, colour
    scale 0x158, flags 0x180, texture scale 0x184/0x188).** Basis: the dump
    of `EnchArmorMagickaFXS` matches item 16 (fill 102 grey, alpha 0.05,
    scale 3, V scroll 0.1, colour scale 1.5, flags 0x3000C). If wrong, the
    recipe the game writes for `skyrim~092ded` differs from
    `tests/fixtures/recipes/skyrim~092ded.json` (checklist item 8).

41. **po3's Tweaks exports `GetFormEditorID(std::uint32_t formID)` from
    `po3_Tweaks.dll`, answering with the editor ID of any form, and the
    engine's own `TESForm::GetFormEditorID` and `LookupByEditorID` cover only
    the types that keep editor IDs (keywords, magic effects, a few more).**
    Basis: clib_util's `editorID::get_editorID`, which SPID and KID use,
    takes exactly this route; the first in-game run (2026-09-04) with Tweaks
    installed still named every imported recipe by form key, showing the
    engine call alone returns nothing for effect shaders. The store resolves
    the export at load, asks it for every effect shader, enchantment, magic
    effect, keyword, armor, addon and light, and builds a reverse map for
    the editor IDs recipes name; the importer names records through the
    same call. If wrong: the `recipes: N editor IDs indexed` line stays
    small with Tweaks loaded and imported files keep form-key names; the
    keys still match, because the importer fills the form key behind the
    reference it wrote.

42. **Community Shaders' Light Limit Fix keeps per-light flags in the
    `NiLight`'s `ambient.red` bits (Initialised 1<<8, InverseSquare 1<<10),
    the ISL cutoff in `ambient.green` and the ISL size in `radius.z`, and
    ISL derives the engine radius as `sqrt(3920 (8 fade - cutoff size^2) /
    (2 cutoff))`.** Basis: CS `LightLimitFix.cpp` and
    `InverseSquareLighting.cpp` at the commit in `cs/SOURCE.txt`. The light
    binding writes these on every light it creates and updates `radius.x`
    from the formula per tick, so a recipe's `size` and `cutoff` mean what
    ISL means by them. If wrong: lights render with vanilla falloff or not
    at all; the Material page's ISL values differ from CS's light debug view.

43. **The engine's `NiTexture::RendererData` reports `width` and `height`
    as 0 for streamed textures that are resident and have a shader
    resource view.** Basis: the first phase 2 run (2026-09-04) rejected every
    armor RMAOS map as a 1x1 placeholder while the texture had a valid
    `resourceView` and its path; the proof of concept never sized the glow
    mask, which is why it worked. `TextureLab::ExtentOf` reads the size
    from the D3D texture description instead and the compositor's real-map
    test uses it. If wrong: `mask '@metal': '<path>' is 0x0` warnings return.

44. **`Actor::AddAnimationGraphEventSink` delivers `BSAnimationGraphEvent`
    (tag, holder, payload string) for every graph event of the actor, on
    the thread that runs the behaviour graph, and the sink list belongs to
    the graph, so a reloaded 3D starts with no sinks.** Basis: CommonLibSSE
    `Actor.h` and `BSAnimationGraphEvent.h`; the manager re-watches the
    actor on every apply (remove, then add, so a surviving graph keeps one
    sink) and posts each event to the game thread as `anim.<tag>` with the
    payload as `arg`. If wrong: `step` triggers never fire (no `anim.*`
    firings on the Signals page), or fire twice per footfall.

45. **The interpreter pass runs the recipe language per texel as a stack
    machine in one fixed pixel shader: the postfix program (at most 256
    ops) in a constant buffer, every stack value a float3 with scalars
    broadcast, eight image slots sampled by their own placement, sixteen
    names, four curve lookups, a 32-deep stack, and pops from an empty
    stack reading 0.** Basis: `Expression.h`'s op set and evaluation
    order, mirrored case by case (`RuntimeTextures.cpp` PSProgram; the
    static_asserts pin the op numbers); dxc compiles the shader offline.
    Runtime compilation goes through D3DCompile at lab init, and a failure
    there leaves the layer passes working with expression masks white and
    a log line. If wrong: masks render as constant 0 or white, or the log
    says `PSProgram compile failed` (then the HLSL needs fxc-specific
    changes, most likely around dynamic indexing of the stack array).
    Confirmed in game 2026-09-04 (README 18, checks 1 to 4): fxc compiled
    the program shader, an animated mask pulsed on its own, `if` kept only
    the raised metal, and a bad expression fell back to white.

46. **PBR armor sets can ship a displacement map that is a real texture
    and entirely black.** Basis: the 2_nordwar iron armor on 2026-09-04:
    the `relief` source read the map (it passed the size test) and every
    texel was 0, so `if(@relief > 0.5, ...)` switched the whole mask off
    and the Sources page showed a black thumbnail. `relief` now takes the
    displacement map only when its mean lies inside 0.02 to 0.98 and
    otherwise reads the RMAOS occlusion channel, as the proof of concept's
    shimmer did when it warned about a flat depth source. If wrong: armors
    with a genuine but very dark height map lose it to occlusion; the
    thresholds are the knob.

47. **A skinned `BSTriShape` keeps its vertex and index buffers per skin
    partition (`NiSkinPartition::Partition::buffData`), laid out by the
    partition's `VertexDesc`: position as four floats, uv as two halfs,
    normal as four biased bytes, skinning as four half weights then four
    byte bone indices into the partition's bone list; a CPU copy sits in
    `rawVertexData`/`rawIndexData` when the engine kept one, else the GPU
    buffers are the only copy.** Basis: CommonLibSSE `VertexDesc::GetSize`
    and NifSkope's BSVertexData; the mesh reader decodes by the descriptor's
    offsets and falls back to a staging-buffer read of the D3D buffers, and
    logs which path it took per geometry. Confirmed 2026-09-04 on the iron
    armor: every geometry took the CPU copy (`cpu copy` in the `mesh` log
    lines, 324 to 3123 vertices) and the position bake drew the mesh
    correctly in UV space. The bind-pose positions are what
    the position bake wants; the normal is bind pose too, so `worldUp`
    ignores the wearer's pose (a limitation, recorded in the source's
    description). If wrong: the position thumbnail is noise or empty, the
    `mesh` log line reports an absurd vertex count, or every bake reports
    `vertex N does not fit the layout`; the layout then needs a per-flag
    check against the actual stride.

48. **CS TruePBR's feature maps and where the coat and subsurface scalars
    live: `featuresTexture0` is the subsurface map (colour rgb, thickness a)
    or the coat map (colour rgb, strength a, with ColoredCoat); `featuresTexture1`
    is the fuzz map (colour rgb, weight a) or the coat normal map;
    subsurface colour and coat colour sit in the base material's
    `specularColor`, their opacity and strength in `subSurfaceLightRolloff`;
    `coatRoughness`, `coatSpecularLevel` and `glintParameters` are their own
    fields, and glint has no map.** Basis: the member comments and getters in
    `cs/BSLightingShaderMaterialPBR.h` (commit in `cs/SOURCE.txt`). The slot
    writer binds coat and subsurface to features0 (they exclude each other),
    fuzz to features1, glint to the parameter block (fuzz and glint exclude
    each other, as CS evaluates one or the other), and feature stacks start
    transparent black so an unwritten texel has zero weight, strength or
    thickness. If wrong: a coat or subsurface output changes the specular
    colour of the base material visibly, or the coat shows where the stack
    laid nothing down.

49. **A shape the engine builds from an armor addon has no authored name;
    its `BSGeometry::name` is ` (<ARMA id>)[<index>]/ (<ARMO id>)
    [<weight>%]`, eight hex digits per id, and only meshes that ship
    named shapes (`Armor003`) keep them.** Basis: the plugin's apply log
    for the Northern Iron boots, a ring and the Rugged Iron cuirass on
    2026-09-05 (` (FE034935)[0]/ (2500097A) [100%]`, ` (0008E840)[0]/
    (00100E29) [100%]`), against `Armor003`, `Armor004`, `Armor004_1` on
    the iron cuirass. The menu turns the pattern into "<armor> geometry
    <index> (addon <id>)" (`Studio::GeometryLabel`) and keeps the raw
    string as the key. If wrong: a shape shows the raw string in the
    Geometry combo, or two geometries of one piece share a label.

50. **CS parallax offsets a texel by `(height - 0.5) * HeightScale`, where
    `HeightScale` is the material's displacement scale (the `scale` the
    height output writes into `rimLightPower`), so 0.5 is the neutral
    height and the scale is material-wide.** Basis: the installed 1.8
    shaders, `ExtendedMaterials/ExtendedMaterials.hlsli`
    (`AdjustDisplacement`, `AdjustDisplacementNormalized`) and the PBR
    parallax branch of `Lighting.hlsl` (`displacementParams.HeightScale
    *= PBRParams1.y`). Consequence: a height stack whose base is an
    all-black displacement map (NOTES 46) shifts every texel outside its
    masks by `-0.5 * scale`, and an animated scale shimmers the whole
    piece; the compositor therefore starts such a stack from a neutral
    0.5 target. A mask confines a layer's contribution to the map, never
    the scale: to confine animation to a mask, animate the layer's
    opacity, not `scale`. If wrong: a masked height layer on a
    flat-displacement armor still shifts the texels outside the mask, or
    the neutral base shows as a uniform offset.

51. **In ImGui, `SetNextItemWidth(-1)` leaves one pixel free, and a
    stretch-sized table whose cells hold such items shrinks by a pixel a
    frame; `-FLT_MIN` is the width that fills exactly.** Basis: the
    studio's context row (a `SizingStretchProp` table of combos given
    width -1) visibly narrowed after the menu opened on 2026-09-05; the
    ImGui documentation for `SetNextItemWidth` states that a negative
    width is measured from the right edge and that `-FLT_MIN` means the
    full width. `Widgets::kFillWidth` is `-FLT_MIN` and every fill goes
    through it. If wrong: a filled input still ends a pixel short, or a
    table with filled cells still creeps.

52. **A start with `PlayerOnly=false` in a crowded cell can freeze during
    the burst of NPC applies; the same place runs with `PlayerOnly=true`.**
    Basis: three starts in Solitude on 2026-09-05 froze within a second of
    `TextureLab: ready`, each at a different actor in the applies, with
    the plugin log ending on a routine line and nothing in the Community
    Shaders, framework or SKSE logs; a fourth start with `PlayerOnly=true`
    ran. One of the freezing builds drew the studio's badges with the
    framework's FontAwesome face and a run without it happened to load,
    which looked like the cause for an hour; the icon-free build froze
    too, so the face is cleared (the badges stayed as text glyphs
    anyway). Suspect: `Actor::AddAnimationGraphEventSink` and its remove,
    called per apply and per retire of every NPC, taking the graph
    manager's spin lock while a busy or loading NPC graph holds it on
    the behaviour thread; unproven, since a freeze leaves no stack. If
    wrong: a build that watches the graph once per loaded actor still
    freezes in the same crowd.

53. **Rendering with the engine's immediate D3D11 context from the menu's
    render thread while the game thread ticks corrupts the driver: the
    crash lands inside `nvwgf2umx.dll` on a driver worker thread with
    nothing of ours on the stack.** Basis: a CTD on save load on
    2026-09-05 (`crash-2026-09-05-15-27-22.log`, access violation in
    `nvwgf2umx.dll+019699B`, no plugin frame) after a session where the
    studio was open in a crowd (`PlayerOnly` had been reset to false by
    the installer) and every preview thumbnail re-rendered on the render
    thread after each apply; D3D11's immediate context is not thread
    safe. The lab now renders previews on the game thread only: the
    render thread records what it asked for under a lock and reads the
    last finished target; `RenderPreviews` runs once per tick; a retired
    target lingers eight ticks so a draw list already built can present
    it. If wrong: the same crash recurs with the menu open in a crowd
    and no preview ever rendered off the game thread.


54. **Two ImGui items with one label in one ID scope share an ID, and a
    click on the second is credited to the first.** Two "Add" buttons on
    the studio's pane rules, drawn in the recipe's scope, both answered to
    the signals rule's button, so Add on the Curves rule added a signal.
    Each rule's items now sit in their own `PushID` scope. If wrong: a
    click on a second same-labelled item in one scope acts on itself.

55. **A geometry's mesh is read once and kept beside its bakes for as
    long as it is bound; the read is logged with a hash of the bytes.**
    The mesh used to live in the per-apply record, so every edit (a
    retire and re-apply) read it again per recipe on the geometry, and
    the snapshot's `meshRead` flipped false until the next read; the
    snapshot also fell through to a read, a bake and a D3D pass on the
    render thread for any mask or source no stack had rendered, the
    NOTES 53 rule broken. The compositor now owns one `MeshCache` keyed
    by the geometry: `Get` reads on the first call and compares a
    `MeshIdentity` (the skin partition, the buffer pointers, the vertex
    count) on every later one, re-reading when it changed; the entry
    carries the facts the snapshot shows and the bakes keyed by
    definition and size, so a rename cannot serve an old bake; the tick
    sweeps entries unbound for 30 s. Every read logs `mesh '<geometry>':
    cpu copy, <p> partitions, <v> vertices, <t> triangles, hash <16 hex>`
    (FNV-1a over the vertex and index bytes), and under verbose logging
    the same read compares the CPU copy against a GPU readback: `mesh
    '<geometry>': cpu copy vs gpu readback: <n> of <m> bytes differ`.
    Basis for the diagnostic: the stale-copy hypothesis of the mesh
    pipeline review (2026-09-06) could not be settled from the headers.
    If wrong: the hash changes between two reads of one geometry with no
    `buffers changed` line, or the compare line reports differing bytes
    on a mesh whose position bake looks right. Confirmed 2026-09-07 in game: each geometry was read once per session and served from the cache through every Paint round after (one `mesh` line per geometry, `cached` after), and the CPU copy matched the GPU readback byte for byte (`0 of 32360 bytes differ`, `0 of 20080 bytes differ` on two pieces), so the moving region of 2026-09-06 was the unrendered preview, not the read. If wrong: a second `mesh` line for the same geometry with a different hash, or a compare line with a nonzero count.

56. **The material analysis is one readback per pair of maps, on request
    (Paint's read of a geometry, or a `materialClusters` source at prepare),
    never at apply: both maps are copied
    at the mip that fits 64 px into one 64 px target and read back through
    a staging texture, and other cluster settings re-run on that stored
    sample; the classify pass writes the analysis' cluster id per texel and
    must agree with `NearestCluster`.** Basis: the flat-displacement measure
    already proved the staging path on the game thread (NOTES 53); a
    4096-texel sample is the cap `Analysis.h` sets, and a mip average reads
    whole texels where a sparse pick would alias. The shader repeats the
    CPU's weighted squared distance in analysis order with strict less-than,
    so a texel lands in the same cluster on both. Unconfirmed in game as of
    2026-09-07; a freeze the same day inside an apply while the sample
    still ran there is why it moved (ARCHITECTURE, Known debts). If wrong: a cluster's share in the snapshot differs visibly
    from the area its id covers in the rendered map, or the map shows ids
    the analysis does not list.

57. **Every use of the engine's immediate D3D11 context from the game
    thread must hold the engine's renderer lock
    (`BSGraphics::Renderer::Lock`/`Unlock`, the critical section at
    `RendererData+0x2780` that the render thread holds around its own
    use); running our passes and readbacks on the game thread is not
    enough on its own.** Basis: a CTD on 2026-09-07
    (`crash-2026-09-07-17-46-03.log`, execute-address access violation,
    the whole stack inside `nvwgf2umx.dll` on a driver thread, nothing of
    ours) after a burst of Paint re-applies with the studio open, the
    same signature as NOTES 53 with every pass already on the game
    thread, which is NOTES 53's "if wrong". The lab now takes the lock in
    `Render`, `RenderProgram`, `RenderClusters`, `BakeMesh`,
    `RenderRipple`, `ReadBuffer`, `ReadBackPixels` and `ReadBackMean`; a
    readback holds it across its `Map`, so the render thread waits while
    the GPU finishes our copy (a hitch, not a hang). Confirmed 2026-09-07:
    the same steps (Paint open, a mask edited through several re-applies,
    a menu opened) ran without a crash on the locked build. If wrong: the same
    crash again with the lock held, or a hang with the render thread
    waiting on the lock while our `Map` waits on the GPU.

58. **The `normal` blend is reoriented normal mapping in the layer
    shader (mode 6), and a preview reads the same channel vocabulary as
    the layer pass.** Unconfirmed 2026-09-08. The blend rotates the
    layer's tangent-space normal so its up follows the normal below
    (Barré-Brisebois and Hill's RNM: `t = below * 2 - (1, 1, 0)`,
    `u = value * (-2, -2, 2) + (1, 1, -1)`, `r = t * dot(t, u) / t.z - u`),
    with `t.z` floored so a black texel below cannot divide by zero; the
    stack starts from the material's normal map, so the first layer
    reorients over the real surface. Before this the mode fell through to
    replace. The lab's channel pass used to number luminance 6 and the
    normal slope 5 while the layer pass numbered luminance 5, so a `luma`
    image source's thumbnail drew the image's slope; both now take one
    `ShaderChannel`, and the slope is a flag on the pass. If wrong: a
    normal-stack layer on `normal` blend shows seams or flattening where
    the base normal tilts, or a `luma` source's thumbnail still looks
    like an embossed relief rather than a grey image.
