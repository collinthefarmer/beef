# mesh/

The mesh layer. It turns the raw vertex and index bytes a renderer hands
over into a trusted `MeshData`, then answers questions about it: which texel
a **bake** needs at each vertex, which islands and material clusters the
mesh contains, which **slots** and bones it covers, and how a **shell**'s
rest pose reposes under **paint**. It is engine-free: it compiles natively
and is unit-tested through `ctest --preset native`. It depends on `recipe/`
(for `BakeKind`, `SourceKind`, `DistanceSource`, `MaterialClustersSource`)
and `Core.h`.

## What it owns

Decode is the boundary. The engine reader fetches raw bytes only; this
module turns them into `MeshData` or rejects them, so a malformed buffer
never reaches the rest of the plugin as undefined behaviour.

From a decoded mesh it derives everything a compositor or the studio menu
asks for:

- per-texel bake buffers — `BuildBake`, `BuildDistanceBake`,
  `BuildIslandBake`;
- connected-component and UV-chart segmentation — `AnalyseMesh`;
- material segmentation over a sampled texture — `ClusterMaterial`;
- slot and bone coverage — `FactsOf`;
- the rest-pose transform a shell repose applies — `PosedTransform`.

It also owns `TextureSize`, the clamped bake-resolution type every bake
**target** carries.

## Data

Each group below lives in one header under `src/mesh/`, named in the heading.

### The mesh (`Mesh.h`)

The engine reader (`render/MeshReader.cpp`) hands this module one
`RawPartition` per geometry **partition**, and `DecodePartition` bounds both
counts before it builds a typed `MeshPartition`. A malformed partition
decodes to `std::nullopt`, never to an overread. Every other function in the
module works on the assembled `MeshData`.

| Type | Description |
|---|---|
| `MeshVertex` | One vertex: position, normal, uv, and up to four bone/weight pairs. |
| `MeshPartition` | One biped **slot**'s geometry: the slot number, its bone names, its vertices, and its triangles. |
| `MeshData` | The whole mesh: its partitions, the bounding center and radius, an `origin` label, and a content `hash`. |
| `VertexLayout` | Where each attribute sits in a raw vertex: the stride and one optional byte offset per attribute (position, uv, normal, skinning). |
| `RawPartition` | The undecoded input: vertex and index byte spans, their `VertexLayout`, both counts, the slot, and the bone names. |

### Bakes (`Mesh.h`)

A **bake** carries one mesh-derived value per vertex, and the compositor
rasterizes it into a per-texel texture (`render/CompositorBake.cpp`,
`render/CompositorSource.cpp`). Every `Build*Bake` function returns a
`BakeBuffers`, and a mesh that cannot be baked yields a `problem` string
instead of an exception. `KeyOf` joins a bake's `DefinitionOf` text and
its size into the `BakeKey` the compositor caches under. When a skinned
mesh stores a zero model bound, `MeasureBound` derives one from the
vertices so the `localPosition` frame exists on every mesh.

| Type | Description |
|---|---|
| `BakeVertex` | One baked vertex: the uv it lands at and up to three float channels of value. |
| `BakeBuffers` | A bake's geometry: vertices, indices, a `vector` flag marking a bake whose channels form one vector value, and the `problem` string. |
| `BakeKey` | A bake's cache identity: the definition text and the pixel size, compared field by field. |
| `MeshBound` | A measured bound: box centre and enclosing radius over a mesh's vertices. |

### Bake resolution (`TextureSize.h`)

Every bake **target** carries a `TextureSize`. The constructor clamps its
one argument into `kMin`..`kMax` (64..4096), so every size downstream is in
range by construction.

| Type | Description |
|---|---|
| `TextureSize` | A texture edge length in pixels. `Pixels()` reads the clamped value. |

### Coverage (`MeshFacts.h`)

Coverage answers which **slots** and bones a mesh touches. `FactsOf`
computes both lists in one `MeshFacts`, and `SlotsOf` and `BonesOf` compute
each list alone.

| Type | Description |
|---|---|
| `SlotCoverage` | One biped **slot**: its number, its name, and its triangle count. |
| `BoneCoverage` | One bone: its name and its share of the mesh's skin weight. |
| `MeshFacts` | The pair of vectors: all covered slots and all weighted bones. |

### Island analysis (`Islands.h`)

`AnalyseMesh` segments a mesh twice, into connected components and into UV
charts, and returns both segmentations in one `MeshAnalysis`.
`BuildIslandBake` rasterizes either segmentation's island ids into a
`BakeBuffers`. `kMaxIslands` (255) caps each segmentation.

| Type | Description |
|---|---|
| `IslandSource` | Selects a segmentation: `kComponent` or `kChart`. |
| `IslandSourceSpec` | One row of the `kIslandSources` naming table: a segmentation's wire name, its plain name, and the `BakeKind` that bakes its ids. |
| `MeshIsland` | One island: its `IslandSource`, its id, its triangle count and share, its dominant bone and that bone's share, and its centroid. `twin` names the other segmentation's island when both cover exactly the same vertices. |
| `MeshAnalysis` | The full result: the island list, the per-vertex `componentOf` and `chartOf` id tables, and the component and chart counts. |

### Material analysis (`MaterialClusters.h`)

`ClusterMaterial` groups a sampled texture's texels by material likeness
with k-means++ over four RMAOS channels, diffuse luma and diffuse RGB. It reads at most `kMaxSampleTexels`
(64 x 64) texels and clamps the cluster count to `kMaxMaterialClusters`
(8, `recipe/Recipe.h`). This header does not include `Mesh.h`, because
clustering works on a `MaterialSample` and never on a mesh. The settings
types (`ClusterSettings`, `ChannelWeights`) live in `recipe/Recipe.h`,
because the recipe's `MaterialClustersSource` carries them directly.

| Type | Description |
|---|---|
| `MaterialTexel` | One texel's roughness, metallic, occlusion, reflectance, diffuse luma and RGB. |
| `MaterialSample` | The sampled texture: width, height, and the texels. |
| `MaterialCluster` | One cluster: its id, its centroid texel, its share of the sample, and a text description. |
| `MaterialAnalysis` | The result `ClusterMaterial` returns: the settings it ran under plus the clusters. |

### Shell pose (`ShellPose.h`)

**Paint** poses a **shell** through its skin-to-bone bind transform.
`PosedTransform` applies a `ShellPoseValues` to a `RestSkinToBone` and
returns the reposed transform. `render/Shell.cpp` consumes the result.

| Type | Description |
|---|---|
| `RestSkinToBone` | A bind-pose transform: a 3 x 3 rotation, a translation, and a uniform scale. |
| `ShellPoseValues` | The pose values: inflate, offset, a scale about `scalePoint`, and a spin about `spinAxis`. |

## How a mesh flows

```
RE::BSGeometry (engine buffers)
  │  ReadBuffers, BoneName                    render/MeshReader.cpp
  ▼
RawPartition (sized spans + VertexLayout)
  │  DecodePartition                          Mesh.cpp   (bounds both counts, never overreads)
  ▼
MeshData                                      render/MeshCache.cpp caches by buffer identity
  │
  ├── FactsOf ─▶ MeshFacts (slots, bones)      MeshFacts.cpp
  ├── AnalyseMesh ─▶ MeshAnalysis (islands)    Islands.cpp
  │
  ├── BuildBake / BuildDistanceBake ─▶ BakeBuffers                 Mesh.cpp
  ├── BuildIslandBake(analysis) ─▶ BakeBuffers                     Islands.cpp
  │       consumed by render/CompositorBake.cpp, keyed by BakeKey
  │
  └── (a sampled MaterialSample, not this MeshData) ──ClusterMaterial──▶
        MaterialAnalysis                                            MaterialClusters.cpp
        consumed by render/CompositorSource.cpp

RestSkinToBone + ShellPoseValues ──PosedTransform──▶ RestSkinToBone   ShellPose.cpp
  consumed by render/Shell.cpp to pose a shell's skin-to-bone transform
```

## The files

| File | What it owns |
|---|---|
| `Mesh.h` / `Mesh.cpp` | `MeshData` and its parts, `DecodePartition`, `HashBytes`, the position/distance/uv bake builders, bake key naming. |
| `TextureSize.h` | The clamped bake-resolution type. |
| `MeshFacts.h` / `MeshFacts.cpp` | Slot and bone coverage over a `MeshData`: `SlotsOf`, `BonesOf`, `FactsOf`. |
| `Islands.h` / `Islands.cpp` | Connected-component and UV-chart segmentation: `AnalyseMesh`, `BuildIslandBake`. |
| `MaterialClusters.h` / `MaterialClusters.cpp` | K-means++ material segmentation over a sampled texture: `ClusterMaterial`, `NearestCluster`, `DescribeTexel`. |
| `ShellPose.h` / `ShellPose.cpp` | The shell repose transform: `PosedTransform`. |

## See also

- `REFERENCE.md` → *Meshes, bakes and analysis* — the biped-slot type split,
  the engine's vertex packing, bake-frame ranges, and the welding and
  clustering rules, none of which the types above can state.
- `docs/conventions.md` → *Multi-phase algorithms* — the bounded-recursion,
  capped-row discipline `kMaxIslands`, `kMaxMaterialClusters`, and
  `kMaxClusterIterations` follow.
