Status: exploratory session record, before implementation. This captures the
breadth of the 2026-09-23–24 discussion, including alternatives that were
subsequently narrowed or set aside. It is not a new schema contract, a claim
of runtime verification, or an additional alpha-release checklist.

# Surface rendering, geometry access, and effect-atlas exploration

The immediate direction is a small effect-atlas proof that reuses the existing
texture compositor. The broader geometry platform and draw-time recipe
interpreter remain possible directions, not prerequisites for that proof.

## 1. Session state and decisions

- Baseline commit: `70f7f5b` — `Prepare alpha candidate with lifecycle fixes
  and verified packaging`.
- Experimental branch: `experiment/effect-atlas`.
- Experimental worktree: `/tmp/beef-effect-atlas`.
- The worktree was created from the committed baseline, not from the earlier
  uncommitted working tree. The original checkout was not modified by this
  experiment.
- No implementation, experimental DLL, shader modification, fixture asset,
  or in-game result exists at the time of this record.
- The user requested the full discussion be recorded before implementation.

The user clarified that there are no authored recipes to preserve yet: there
is a schema defining functionality, with existing code, templates, presets,
and test fixtures. Therefore compatibility with hypothetical authored content
must not dictate a dual execution architecture. Semantics can be clarified
now. Existing implementation is still valuable and should ground the work.

The progression was:

1. Assess armor damage, weapon effects, and item-instance assignments.
2. Identify the UV reuse limitation and investigate alternative addressing.
3. Reframe around geometry representations and coordinate spaces.
4. Consider a general CPU-query/GPU-rendering architecture and automated tests.
5. Consider replacing texture composition with direct material-draw evaluation.
6. Reassess that plan against the actual code; much of the proposed machinery
   already exists, and general CPU surface queries are not prerequisites.
7. Narrow further: preserve texture composition and separate effect-storage
   coordinates from original material UVs.
8. Create an isolated worktree for a small proof, then pause implementation
   to capture this record.

## 2. What the plugin already provides

The current pipeline is:

```text
Equipped armor -> recipe matching -> prepared sources/masks/layers
    -> geometry and derived-map bakes in original UV space
    -> GPU expression and layer passes
    -> generated textures bound into TruePBR material slots
    -> Community Shaders draws the geometry
```

Important reusable systems:

| Existing system | Evidence and relevance |
| --- | --- |
| Expression language and bytecode | [Expression.h](../../src/recipe/Expression.h): parsing, type checking, CPU evaluation, bounded operations and references. |
| GPU expression interpreter | [ShaderSource.cpp](../../src/render/ShaderSource.cpp) and [ShaderConstants.h](../../src/render/ShaderConstants.h): the GPU implementation and its data contract already exist. |
| Preparation and dependency binding | [CompositorSource.cpp](../../src/render/CompositorSource.cpp): `MaskBuilder` resolves texture/signal references, mask dependencies, types, and curve lookups. |
| Prepared layers and rendering | [Compositor.cpp](../../src/render/Compositor.cpp): preparation, ordered layer passes, base textures, layer filtering, and static-result reuse. |
| Execution/cache decisions | [Vocabulary.cpp](../../src/recipe/Vocabulary.cpp): `IsAnimated` and `ShareableAcrossActors`. |
| Material composition | Shader blend modes, channel masks, opacity, image transforms, and reoriented normal blending. |
| Material ownership | [Binding.cpp](../../src/render/Binding.cpp): slot publication, restoration, retained texture ownership, scalar and feature handling. |
| Geometry extraction | [MeshReader.cpp](../../src/render/MeshReader.cpp): rigid/skinned buffer paths, CPU copy or GPU readback, partition information, bone names and weights. |
| Geometry analysis | [Mesh.h](../../src/mesh/Mesh.h), [Islands.h](../../src/mesh/Islands.h): triangles, bounds, components, charts, and bake builders. |
| Derived material data | Material sampling/clustering, normal-slope maps, and related caches. |
| Resource management | Render-target pooling, presenter textures, leases, retained caches, and eviction. |
| Actor/application lifecycle | Collection, refresh, retirement, load gating, and application records. |
| Editor | Existing recipe operations, history, selection, isolation, source inspection, and previews. |
| Diagnostics | JSONL traces, timing/resource counters, readback instrumentation, and [trace-report.py](../../tools/trace-report.py). |

The interpreter currently has concrete limits: 256 expression operations,
16 program references, 8 program textures, 4 curve textures, and a 32-entry
GPU stack. These are existing implementation limits, not a proposal to invent
an unbounded general shader language.

The main current coupling is that prepared sources and masks become textures,
and original mesh UVs serve as both material coordinates and effect-storage
coordinates.

## 3. Feature motivations considered

### Armor damage

Surface scratches, scuffs, scorch marks, and dents represented through shading
can reuse diffuse, normal, RMAOS, and height outputs. Actual bent plates or
silhouette-changing dents require vertex deformation, a separate scope.

Missing pieces for accurate hit-driven damage include:

- Reliable contact position, direction/normal, and affected equipment.
- Mapping the contact onto the currently posed visible surface.
- Stable surface placement as the armor animates.
- Accumulated damage records rather than short-lived trigger firings.
- Persistent item identity and save storage if damage must follow an item.
- Coordinated material changes across color, roughness, metalness, and normals.
- Independent surface addressing where original UVs overlap.

The existing received-hit position is the attacker's position, not the contact
point. Actor events fan out to active recipe instances. The ripple path is a
useful spatial-effect precedent, but uses stored-position bakes and a bounded
set of firings, not a damage history. World-to-root conversion does not undo
limb animation.

The height slot binds a displacement texture; this does not establish a vertex
deformation mechanism. Shell posing adjusts skin-to-bone transforms, not a
localized dent in the original mesh. Physical deformation additionally needs
private writable geometry, normals/tangents, bounds, body-clipping decisions,
and restoration. Collision changes are another task.

### Weapon enchantment effects

Basic PBR weapon effects appear substantially closer than persistent damage.
The main collection path explicitly accepts armor, and the matching/editor
identity model contains armor-specific assumptions.

New work would include weapon geometry discovery, hand-specific enchantment
lookup, first-/third-person and draw/sheath lifecycle handling, and editor
targeting. Independent weapon reactions need per-equipment state and event
routing. Current recipe/enchantment instance deduplication can synchronize
two matching weapons, which is insufficient for independent charge or hit
responses. Shell/light attachment also needs weapon validation.

The current material path requires compatible TruePBR geometry. General
non-PBR weapon support would be an additional rendering scope.

### Item-instance assignments

The desired distinction is between two physical copies of the same item form.
Current form keys, actor/form `PieceRef`, and studio pinning do not establish
persistent inventory-copy identity.

Proposed separation:

```text
Recipe definition: what the effect does
Item assignment: which physical item receives it
Live placement: which current geometries display it
```

Assignments could live in save-scoped state and reference reusable recipe IDs.
An assignment and an assignment-only eligibility mode are distinct: attaching
a normally matching recipe to one item does not prevent its ordinary keys
from matching other copies. Composition/replacement policy should remain
explicit.

`ExtraUniqueID` is a candidate engine mechanism, not a verified persistence
solution. It contains a form ID and a 16-bit unique ID; existence of the type
does not prove global uniqueness, transfer stability, or creation semantics.
[CommonLib declaration](https://ryan.commonlib.dev/classRE_1_1ExtraUniqueID.html).

Identity validation would cover stack splitting, equipped copies, transfers,
drop/pickup, save/load, enchanting, duplication, and destruction. Pointers,
names, base forms, and current owners are not sufficient persistent keys.
Unresolved assignments must not fall back to every copy of the base form.

These feature branches motivated the investigation; none is required for the
initial atlas proof.

## 4. Abstractions exposed by those motivations

| Boundary | Why it is useful |
| --- | --- |
| Definition / equipped occurrence / rendered representation | Separates form matching, runtime ownership, and first-/third-person geometry. Persistent inventory identity is a further concern. |
| Matching / signal-state scope | Whether a recipe applies and whether applications share clocks/events are different decisions. Current synchronized armor behavior may remain intentional. |
| Event recipient / event payload | Actor-wide delivery, per-piece delivery, attacker positions, and actual contacts need distinct meanings. |
| Coordinate spaces | World, actor-root, model/skin, bone, tangent, and UV values should not silently share a meaning merely because all are vectors. |
| Durable state / render resources | Damage or assignments must survive disposal of actor geometry and GPU caches. |
| Continuous animation / change-driven updates | Accumulated marks need updates on revision changes, rather than inherently every frame. |
| Surface location / texture address | Multiple surface locations can share a material texel. UV coordinates are an attribute, not a surface identity. |

These are candidate boundaries, not instructions to introduce a general
framework before the first experiment.

## 5. UV mirroring and texel reuse

When two surfaces sample the same texture at the same coordinates, an ordinary
replacement texture cannot give them different values. Higher resolution,
better contact localization, and another mask baked into the same UV domain
cannot restore the lost distinction.

Current geometry bakes rasterize positions, normals, bone weights, and IDs
into original UV space. Overlapping triangles overwrite each other's values.
An ID map in that same domain cannot disambiguate surfaces after the collapse.

The difficult case is overlap within one independently bound geometry. Separate
geometry objects may already receive separate generated textures, provided
their material/resource ownership is independent.

Correction recorded during inspection: current side/facing overlap logs compare
UV bounding rectangles of vertex groups. They are useful heuristics, not
triangle-level overlap measurements. Overlapping rectangles do not prove
overlapping triangles. See [MeshCache.cpp](../../src/render/MeshCache.cpp).

Even an independent per-triangle atlas does not automatically distinguish the
two visible sides of one double-sided triangle; that requires a side-aware
representation if different appearance is needed.

## 6. Alternative rendering/addressing routes

| Route | Benefit | Cost or limitation |
| --- | --- | --- |
| Accept overlap and report it | Smallest change; many glows can be symmetric | Cannot provide arbitrary asymmetric marks. |
| Separate existing geometry bindings | Reuses current per-geometry effects | Does not resolve overlap inside a geometry. |
| Split geometry into independently bound groups | Retains original material UVs | Geometry rebuilding, skinning, extra draws, lifetime/restoration. |
| Texture-array layer selected per triangle/group | Adds surface identity to the sampling address | Per-draw triangle mapping, resource binding, separate bakes, memory. |
| Independent effect atlas | Preserves original material UVs while separating generated results | Atlas layout, draw-time coordinates, filtering, resolution, source remapping. |
| Direct procedural material shading | No intermediate UV collapse for surface-dependent calculations | Shader integration and per-fragment cost; position alone is not unique identity. |
| Localized shader stamps | Sparse oriented marks without original-UV storage | Stable attachment, projection leakage, stamp culling/indexing as count grows. |
| Vertex attributes | Good for coarse regional masks | Detail limited by tessellation; shared vertices may need splitting. |
| Attached decal patches | Independent coordinates and detailed local marks | Geometry generation, skinning, clipping, depth conflicts, draw overhead. |
| Screen-space projection/painting | Distinguishes currently visible pixels | Persistence still needs surface attachment; view dependence and occlusion. |
| Recent stamps plus consolidated textures | Could bound long-term stamp cost | More machinery; only warranted by measured needs. |

Mirrored tangent handedness and front/back-face tests distinguish particular
cases, not arbitrary overlapping UV regions.

Texture reads are GPU instructions, not CPU callbacks. Hooking a resource bind
can select a texture per draw; distinguishing surfaces within that draw needs
additional shader information or different geometry/draw organization.

`SV_PrimitiveID` can provide draw-local primitive identity, subject to shader
stages. It is not a persistent mesh identifier, and it does not by itself
provide the location within a triangle. A per-triangle atlas lookup must also
recover/interpolate that location. See Microsoft's
[semantics](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-semantics)
and [texture sampling](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-sample)
documentation. Geometry partitions, index order, and draw offsets must be
validated rather than assumed to match decoded triangle indices.

## 7. Geometry representations and coordinate spaces

The broader pipeline considered was:

```text
Engine geometry discovery -> identity/lifetime -> buffer decoding
    -> stored mesh snapshot -> topology analysis
                           -> surface addresses and attribute evaluation

Stored mesh + skin binding + pose inputs -> posed surface
    -> spatial queries -> surface addresses

Engine draw data + decoded geometry -> draw correspondence
    -> draw-time surface access
```

Access levels must distinguish readable data, implemented reconstruction,
drawability, and in-game validation.

| Geometry/data | Current access | Missing or unverified |
| --- | --- | --- |
| Worn armor geometry | Collected, including first-/third-person paths | Persistent copy identity. |
| Stored triangles | Positions, normals, UVs, indices, weights, partitions | Stable identity across replacements; explicit attribute presence. |
| Rigid triangle shapes | Non-skinned reader path exists | Weapon discovery/lifecycle and transform validation. |
| Skinning inputs | Bone data and transform pointers accessible | Complete retained numeric mappings and validated pose evaluation. |
| Current skeleton | Nodes and flattened transform storage accessible | Consistent capture timing relative to rendering. |
| Final posed surface | No general CPU representation | Reconstruct selected vertices or access equivalent GPU results. |
| Collision/contact geometry | Not acquired by this plugin | Contact-to-visible-surface mapping. |
| Draw primitives | Renderer buffers accessible | Actual partition/primitive correspondence. |
| Tangent frame | Normal decoded | Full tangent-frame extraction or reconstruction. |

Spaces discussed:

- World: engine actor/node positions; contacts are not supplied by the current
  hit adapter.
- Actor-root local: implemented inverse-root conversion, not inverse skinning.
- Geometry/model and skin/bind spaces: stored positions and bind transforms;
  their relationship needs an explicit contract.
- Bone local: useful for attachments, but weighted points generally involve
  several bones.
- Surface coordinates: triangle plus barycentric coordinates.
- Material UV: fully used today, potentially many-to-one.
- Tangent space: necessary for correctly oriented normal detail.
- View/clip/screen: relevant to picking and diagnostics, not currently a
  general geometry interface.
- Independent effect coordinates: the proposed storage domain.

Candidate surface address:

```text
Geometry identity + topology revision
Partition + triangle
Barycentric coordinates
Optional side information where needed
```

It is valid only for compatible topology; it is not automatically a save-safe
address or a correspondence between different first-/third-person meshes.

Topology, attribute changes, and pose updates should have different invalidation
rules. Animation should not rebuild UV charts; changed topology must invalidate
triangle addresses. Buffer pointers alone do not capture all in-place changes.

One current semantic mismatch is `NodeBindPosition`: skin data yields a
bind-space bone origin, but its fallback yields a current node position in
actor-root space. The shared return type does not communicate that difference.

## 8. CPU queries and GPU rendering need not duplicate every mesh

The preferred broad model was a common surface/pose contract with different
evaluation strategies:

- Reuse the engine's deformation for drawing.
- Pose three vertices to evaluate a known triangle location on the CPU.
- Transform rays into local space for rigid-mesh queries.
- For skinned queries, use conservative candidate bounds and pose candidates,
  or pose the whole mesh and refit a spatial hierarchy when query density
  justifies it.
- Use a simple complete evaluator as a correctness reference before optimizing.

Bind-pose bounds or nearest-bone heuristics must not be presented as exact
animated-surface queries. Bounds must conservatively include possible queried
geometry. GPU compute may help large batches, especially when results remain
on the GPU; CPU readback introduces latency and synchronization concerns.

This full query subsystem is explicitly deferred from the atlas proof.

## 9. Skyrim and Community Shaders findings

Source inspection supports a scoped rigid/skinned pipeline, but no experiment
in this session established rendered correctness.

The inspected Community Shaders revision was
`dd2677fc4020db1da91b39cebef3dbfbe8913983`, the repository's material-layout
reference. Its shader files were fetched and inspected separately from the
installed game. A layout-compatible installed release does not imply identical
shader source or safe whole-file replacement.

Concrete findings:

1. Lighting passes incoming `ModelPosition` and transformed `WorldPosition`
   to pixel shading. Skinned raindrop effects already use model position.
   Camera-position adjustments matter when interpreting shader world values.
   This is a precedent for stable UV-independent effects, not proof of unique
   surface identity. [Lighting.hlsl](https://github.com/community-shaders/skyrim-community-shaders/blob/dd2677fc4020db1da91b39cebef3dbfbe8913983/package/Shaders/Lighting.hlsl).
2. Skinning blends four influences, with three rows per transform, pivot
   correction, and current/previous palettes in separate constant buffers.
   The shader is an explicit reference for comparison.
   [Skinned.hlsli](https://github.com/community-shaders/skyrim-community-shaders/blob/dd2677fc4020db1da91b39cebef3dbfbe8913983/package/Shaders/Common/Skinned.hlsli).
3. Lighting `SetupGeometry` hooks receive a render pass identifying geometry;
   Community Shaders also updates resources around graphics-state processing.
   These are internal integration points. No public arbitrary-effect API was
   established by this investigation.
   [Hooks.cpp](https://github.com/community-shaders/skyrim-community-shaders/blob/dd2677fc4020db1da91b39cebef3dbfbe8913983/src/Hooks.cpp).
4. Our [SkinPalette.cpp](../../src/render/SkinPalette.cpp) handles valid world
   transform entries with null bone-node pointers in flattened bone trees.
   A node-only pose evaluator would miss those cases.
5. The generic reader does not explicitly acquire `BSDynamicTriShape` dynamic
   data. Reading a GPU vertex buffer does not prove it is the final posed or
   otherwise deformed surface.
6. `TrianglesWithin` filters invalid triangles, which can change numbering.
   A draw-correspondence implementation must preserve original identity or
   reject such input explicitly.
7. The reader retains partition bone names, but the proposed evaluator needs
   the numeric partition-to-skin mapping as well.
8. The player-update hook does not prove that captured pose data matches a
   particular render pass. Frame/pass timing needs explicit evidence.

## 10. Memory and compute discussion

These were illustrative estimates, not measurements of an implemented pipeline.
Let V be vertices including partition duplicates, T triangles, and K influences
per vertex (up to four in the current decoded representation).

| Data | Approximate storage |
| --- | --- |
| Current decoded vertex | 56 bytes per vertex from the declared fields; verify actual ABI if budgeting precisely. |
| Triangle indices | 12 bytes per triangle. |
| Posed positions | 12 bytes per vertex, per independently posed instance. |
| Posed normals | Another 12 bytes per vertex if retained. |
| Acceleration structure | Layout-dependent, typically tens of bytes per triangle. |
| Surface address | Tens of bytes per tracked point. |

A 20,000-vertex / 30,000-triangle example is about 1.4 MiB of decoded geometry,
0.23 MiB for posed positions, or 0.46 MiB for posed positions plus normals,
before containers, strings, temporary data, and analysis. A spatial hierarchy
was provisionally estimated at 1–3 MiB for that example, not benchmarked.
Immutable geometry may be shared only when the actual data agrees; equal item
forms do not establish identical mesh data.

Compute considerations:

- Decode: O(V + T), on acquisition/change.
- Topology analysis: typically linear or sorting-based, depending on method.
- Spatial hierarchy construction: commonly O(T log T); refit commonly O(T).
- Full pose: O(VK).
- Known surface point: three vertex poses and interpolation.
- Hierarchical ray queries: often sublinear, worst case linear.
- Precise UV overlap: candidate-dependent, quadratic worst case.

At 60 Hz, 20,000 vertices imply 1.2 million vertex evaluations per second per
instance. Fifty such instances imply 60 million. Ten tracked triangle points
on each of fifty instances imply at most 90,000 vertex evaluations per second
before reusing shared vertices. These counts are not frame-time predictions.

One 1024-square RGBA8 texture is 4 MiB, about 5.3 MiB with a full mip chain.
Channels, atlas area, array layers, scratch targets, and per-instance copies
multiply that cost. Separating shared surfaces costs storage if detail density
is retained. GPU readback stalls can matter more than transfer size.

Direct material evaluation shifts cost toward visible fragments and render
passes. Atlas composition retains the current update-then-sample cost model.
Neither is inherently faster for every recipe.

## 11. The broader direct-evaluation proposal

One considered contract was: a recipe describes values evaluated on a surface,
and the renderer chooses which intermediate results to cache.

```text
Recipe validation and dependency analysis
    -> prepared resources and executable program
    -> per-application updates
    -> material-draw evaluation
```

Image/material sources retain UV semantics; geometry sources access the surface
directly; signals are uniform inputs; masks and layers combine them. Clustering,
mesh analysis, and reductions remain preparation tasks. One semantic model
does not require all work to execute per fragment every frame.

Under that design, `Compositor` becomes more of a program preparer; `TextureLab`
becomes a supporting service for analysis, lookups, previews, and captures;
material binding gains program/resource association at draw time. Shell/light
lifecycle machinery remains useful.

Open issues included evaluation scopes, original versus already-modified
material sampling, preview context, output integration, shader permutations,
program/resource limits, and interpreter versus compiled shader variants.

The subsequent source review corrected the apparent size of this work:
compiler, interpreter, dependency preparation, caching decisions, curves,
blend mathematics, and diagnostics already exist. The main extension would
be new surface-reference kinds and material-draw integration. Nested masks
currently materialized as textures would need another execution representation.

This route remains an alternative. It was not selected as a prerequisite for
the smaller addressing experiment.

## 12. Current focus: independent effect coordinates

The targeted route keeps texture composition and separates two domains:

```text
Original material UVs -> read original images and material maps
Effect coordinates   -> store bakes, masks, previous layers, final effects
```

At an effect texel, the compositor can recover original UVs when needed, read
geometry-derived values from independent bakes, run its existing expressions
and blends, and publish a composited atlas. The armor draw must then sample
that result using matching effect coordinates.

### Layout and baking

Introduce a layout with coordinates per triangle corner and a layout identity.
Per-corner coordinates accommodate atlas seams at otherwise shared vertices.
Geometry bakes rasterize into this layout rather than copying `vertex.uv` into
their destination coordinates. Original UVs remain an attribute.

A tiny proof can assign selected triangles explicit tiles. Production packing
should preserve coherent regions where possible; one tile per triangle wastes
area and creates many filtering boundaries. Existing component/chart analysis
is useful input, not proof that each chart is internally overlap-free.

### Compositor source domains

| Input | Address domain |
| --- | --- |
| Original material maps | Original material UV. |
| Authored image | Original UV followed by the image's declared transform. |
| Geometry bake | Effect coordinates. |
| Computed mask | Effect coordinates. |
| Previous composited layer | Effect coordinates. |
| Initial material base | Correctly sampled/resampled from original UVs before later atlas layers. |

Prepared bindings should state the address domain explicitly. The existing
`meshSpace` flag is not a substitute for this distinction.

Two preparation options were discussed:

- A floating-point original-UV lookup texture lets existing fullscreen passes
  recover material coordinates at each effect texel.
- Rasterizing compositor passes over layout geometry can interpolate original
  UVs directly, avoiding that lookup at the cost of changing pass submission.

The current target pool creates RGBA8 UNORM textures. It cannot directly hold
general precise UV coordinates, especially outside [0,1]. A coordinate texture
requires suitable format support and matching resource accounting.

### Final draw mapping

Candidates are an additional coordinate stream/private geometry representation,
or draw/triangle metadata combined with a within-triangle mapping. The draw
must use exactly the layout used during baking. Merely supplying a triangle
ID is insufficient.

Animation can use the engine's existing deformation when the effect address is
attached to the original surface. This does not require general CPU skinning,
but the actual shader input and interpolation route must be validated.

### Filtering and material limitations

- Coordinate maps must not average unrelated UVs across atlas boundaries.
- Atlas gutters and mip generation need ownership-aware treatment. The current
  two-pixel bake dilation is not a general atlas mip solution.
- Fine detail requires adequate atlas density after formerly shared regions
  become independent.
- Layout identity and resolution must enter bake/result cache keys.
- Resampled normal values must retain the original tangent-frame meaning;
  atlas orientation must not redefine that frame.
- Height/parallax introduces coordinate-displacement questions and follows,
  rather than precedes, the emissive proof.
- A sampled composited base may incur quality loss; retaining original base
  sampling and combining an effect contribution at draw time is another design
  choice, but arbitrary blend modes must remain mathematically correct.

## 13. Minimal proof and current worktree boundary

The first checkpoint is different atlas colors on surfaces that share material
UVs. The second is connecting one existing mask/layer stack to that addressing.

Proposed first slice:

1. One controlled mesh/layout or explicitly supported single-partition mesh.
2. Distinct effect regions for surfaces with overlapping original UVs.
3. One emissive output, without changing recipe/schema/editor vocabulary.
4. A small separately staged shader modification to sample the atlas.
5. Clear diagnostic output and a short in-game procedure.

Pass evidence: original UV usage remains understandable, effects differ on
surfaces with shared UVs, animation preserves attachment, another instance
is unaffected when not targeted, and disabling the experiment restores normal
rendering.

The most recent implementation sketch proposed a self-describing atlas that
carries triangle-to-atlas mapping data, to avoid a new render hook in the first
probe. This is only a candidate experimental transport. No encoding, marker,
precision, resource format, or primitive-correspondence mechanism has been
implemented or validated. It must not become a production contract by accident.
A supplied secondary coordinate layout on a controlled fixture remains another
way to isolate the same question.

Explicitly outside this proof:

- Automatic general unwrapping/packing.
- All material outputs and all mesh types.
- General CPU posed meshes, ray queries, or spatial hierarchies.
- New studio workflows or a new recipe compiler.
- Item identity, persistence, damage history, or weapon support.
- A complete scene automation framework.

No full shader file should be assumed compatible merely from a matching PBR
material layout. Any patch must identify and validate the source it modifies.
Experimental assets should be staged separately and reversible.

## 14. Testing and automation ideas

The full test strategy considered controlled fixtures first, then representative
game assets:

| Fixture | Question isolated |
| --- | --- |
| Rigid mesh | Transform and projection conventions. |
| Two-bone joint | Palette mapping, blended deformation, update timing. |
| Deliberately overlapping UVs | Independent effect addressing. |
| Two actors with the same mesh | Per-instance isolation. |
| Layered/double-sided surfaces | Ambiguous locations and projection leakage. |
| First-/third-person representations | Separate geometry and lifecycle coverage. |

Diagnostic modes considered model/posed position, triangle/partition IDs, bone
weights, triangle-point markers, a mask-plus-image example, instance colors,
and current/candidate renderer comparison.

Validation gates:

1. Target a draw and restore it without leaking state.
2. Establish coordinate access under camera/actor/joint motion.
3. Compare CPU and GPU positions using the same draw/pose inputs.
4. Check recipe operations against explicit references.
5. Exercise equip, view switches, load/unload, and geometry rebuilds.
6. Measure preparation/update/draw costs and resource baselines.

Native tests should cover deterministic mapping, interpolation, invalid/stale
addresses, coordinate contracts, and limits. GPU conformance captures can use
small known outputs and numerical tolerances. Screenshots are supporting
evidence, not a replacement for numerical assertions.

GPU readbacks should carry the generation/draw inputs that produced them;
comparing an old GPU result with the current CPU pose is not a valid test.
Verbose logging/readback must be disabled or accounted for during performance
measurement. Warm caches and vary instance count, mesh size, pixel coverage,
program complexity, and query count independently.

### Full automation option

Three components were proposed:

- External runner: launch a test profile through SKSE/MO2, submit a manifest,
  monitor progress, detect crashes/timeouts, archive artifacts.
- In-game driver: load/reset fixtures, configure actors/equipment/camera,
  wait for readiness, execute probes.
- Capture/assertion layer: numerical buffers, diagnostic images, timings,
  tolerances, and outcomes.

A file-based protocol with atomic manifests and run IDs is sufficient initially;
named pipes are optional. Game mutation and render capture belong at their
appropriate execution boundaries. A dedicated cell/baseline save could make
longer-term testing repeatable, resolving forms by plugin/local ID.

```text
Load baseline -> wait for cell/actor 3D -> configure probe
    -> wait for geometry and actual draw -> warm up
    -> capture matching observations -> assert -> reset
```

Readiness is observed, not inferred from a fixed sleep. Outcomes distinguish
pass, assertion failure, missing prerequisite, timeout, and crash. Archive a
manifest, results JSON, event JSONL, captures, and loader/plugin logs. Test
results must not disappear when rotating diagnostic logs expire.

This is a rendered Windows integration test, not a headless unit test. The
runner/startup/load sequence and fixture assets do not currently exist.

### Trimmed automation for the first proof

Use an existing known save/armor and a small sequencer. Reuse application states,
trace recording, resource counters, and readback instrumentation; add actual
draw acknowledgement and minimal captures. Defer a new fixture plugin, a scene
constructor, general IPC, and a standalone reporting framework unless the
first experiment demonstrates a need.

## 15. Dependency boundaries and future capabilities

| Capability | Prerequisites beyond current rendering |
| --- | --- |
| Basic weapon effects | Weapon discovery, enchantment lookup, lifecycle. |
| Independent equipment reactions | Explicit equipment state/event scope. |
| Persistent item assignments | Reliable item identity and save storage. |
| UV-independent procedural effects | Surface coordinates and draw integration. |
| Stable arbitrary attachment | Surface address and validated pose evaluation. |
| Click/paint on visible geometry | Surface queries; independent storage for arbitrary persistent paint. |
| Localized hit damage | Contact acquisition plus surface mapping and mark representation. |
| Persistent hit damage | Above plus item identity and save storage. |
| Physical deformation | Private geometry, displacement, frame/bounds updates, restoration. |

Potential future features include shape-following glows and waves, asymmetric
material changes, surface painting/selection, regional wear/frost/scorch marks,
oriented scratches, and surface-attached lights/particles/meshes. Connectivity
could support surface-distance brushes, distinct from a spatial radius that
also reaches nearby disconnected surfaces.

Cutting/tearing requires topology work. Collision-aware changes need physics
integration. Placement across different first-/third-person meshes needs an
explicit correspondence. None follows automatically from an atlas.

Identity/persistence, CPU surface queries, and draw integration are independent
feasibility branches. Synthetic rays can test queries before real contacts;
solid colors can test draw mapping before recipes; assignments can test item
identity before damage storage. These branches should not all gate one proof.

## 16. Sizing discussion and corrections

All estimates in the conversation were provisional effort judgments, not
measured delivery commitments.

| Scope discussed | Estimate given | Current interpretation |
| --- | --- | --- |
| Broad replacement, geometry foundation, full harness, studio integration, hardening | 10–20 engineer-weeks; 2–4 week feasibility phase | Withdrawn for the narrowed scope: counted existing systems as new and bundled optional subsystems. |
| Source-grounded direct-evaluation replacement | About 3–6 weeks; 2–5 day initial probe | Conditional on practical draw integration; not an estimate for a universal geometry platform. |
| Small atlas worktree proof | A few focused hours for a buildable candidate, then one or two in-game iterations | A working visual outcome was not promised; draw mapping remains uncertain. |

The strongest correction is scope, not the calendar: reuse the current compiler,
interpreter, preparation, caches, binding lifecycle, and diagnostics. Do not
build raycasting, persistence, scene automation, or a second authoring system
to prove independent effect addressing.

## 17. Open questions before implementation resumes

1. Which minimal fixture and mapping mechanism best isolate the draw-address
   question: supplied coordinates or self-describing triangle metadata?
2. Can the selected shader path identify the same triangles as the CPU reader,
   including partitions and index offsets? How will mismatch be detected?
3. How is within-triangle position recovered without unstable numerical or
   unsupported shader assumptions?
4. Which installed shader source/version is being patched, and how is the patch
   staged and removed independently of the normal plugin?
5. How is only the intended geometry/application opted into the experiment?
6. What numerical/log evidence accompanies the visual color test?
7. After the draw test, should source remapping use a float coordinate map or
   mesh-rasterized composition passes?
8. What filtering and texel-density constraints are acceptable for the proof,
   and which must be solved before generalizing it?
9. Which existing schema semantics need clarification if a geometry bake no
   longer shares the original UV domain?

The next implementation should answer the first draw-address question with
the smallest reviewable change. This record preserves the larger possibilities
without turning them into mandatory work.
