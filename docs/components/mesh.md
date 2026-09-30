# mesh/

The mesh layer. It turns the vertex and index bytes that the renderer reads
into a trusted `MeshData`. It then derives per-vertex **bake** values,
**island** segmentations, **slot** and bone coverage, material clusters, and
the posed transform of a **shell**. It is engine-free and compiles natively.
Its tests live in `tests/mesh/` and run through `ctest --preset native`. Its
`ALLOWS` row in `tools/gate.py` is `'mesh': ('Core.h', 'recipe', 'mesh')`.
The `planners`, `studio`, `render`, `engine` and `menu` layers may include it.

## What it owns

`DecodePartition` is the boundary. `render/MeshReader.cpp` reads raw bytes
only. This module turns those bytes into a `MeshPartition` or rejects them
with `std::nullopt`. A malformed buffer therefore never reaches the rest of
the plugin.

From a decoded `MeshData`, the module computes these results:

| Result | Function | File |
|---|---|---|
| Per-vertex bake geometry | `BuildBake`, `BuildDistanceBake`, `BuildIslandBake` | `Mesh.cpp`, `Islands.cpp` |
| Connected-component and UV-chart segmentation | `AnalyseMesh` | `Islands.cpp` |
| Slot and bone coverage | `FactsOf`, `SlotsOf`, `BonesOf` | `MeshFacts.cpp` |
| Material clusters over a sampled texture | `ClusterMaterial`, `NearestCluster`, `DescribeTexel` | `MaterialClusters.cpp` |
| The posed skin-to-bone transform of a shell | `PosedTransform` | `ShellPose.cpp` |

The module also owns `TextureSize`, the clamped texture edge length that
every `TextureRequirements` record carries.

## Data

Each group below lives in the header that its heading names.

### The mesh (`Mesh.h`)

`render/MeshReader.cpp` builds one `RawPartition` per geometry
**partition** and passes it to `DecodePartition`. `DecodePartition` decodes
each vertex through `DecodeVertex` and rejects the partition when the index
bytes are shorter than the triangle count needs. `TrianglesWithin` then
drops every triangle whose corner index is outside the vertex list. Every
other function in the module reads the assembled `MeshData`.

| Type | Description |
|---|---|
| `MeshVertex` | One vertex: position, normal, uv, and four bone index and weight pairs. |
| `MeshPartition` | One partition's geometry: the raw slot number, the bone names, the vertices and the triangles. `MeshPartition::kNoSlot` (0xFFFF) marks a partition with no slot. |
| `MeshData` | The whole mesh: the partitions, the bound center and radius, an `origin` label, and a content `hash`. |
| `VertexLayout` | The byte layout of one raw vertex: the stride and an optional byte offset for position, uv, normal and skinning. |
| `RawPartition` | The undecoded input: vertex and index byte spans, the `VertexLayout`, the vertex and triangle counts, the slot, and the bone names. |
| `MeshBound` | A measured bound: the box center and the enclosing radius. `MeasureBound` computes it from the vertices. `render/MeshReader.cpp` calls it when the engine bound is zero. |

`HashBytes` computes an FNV-1a hash from `kHashBasis`.
`render/MeshReader.cpp` folds the vertex and index bytes into
`MeshData::hash` with it. `AddBoneWeights` adds one vertex's positive, finite
bone weights to a map by bone name. `BonesOf` and the island tally in
`Islands.cpp` both use it.

### Bakes (`Mesh.h`)

A **bake** holds one value per vertex at that vertex's uv.
`ExecuteBuildBakeBuffersStep` in `render/RenderInstance.cpp` builds the
buffers, and `TextureLab::BakeMesh` (`render/TextureLabPass.cpp`) rasterizes
them into a texture. A bake that cannot be built returns an empty
`BakeBuffers` with a `problem` string.

| Type | Description |
|---|---|
| `BakeVertex` | One baked vertex: its `u` and `v`, and three float channels of value. |
| `BakeBuffers` | The bake geometry: vertices, indices, a `vector` flag, and the `problem` string. The `vector` flag marks a bake whose three channels form one vector value. |
| `BakeKey` | A bake identity: the definition text and the pixel size, ordered field by field. `KeyOf` builds it from a definition and a `TextureSize`. |

| Symbol | Description |
|---|---|
| `BuildBake` | Bakes one `BakeKind` from `recipe/Recipe.h`. It returns a `problem` for `ComponentIdBake` and `ChartIdBake`, because those kinds need `BuildIslandBake`. A `PartitionBake` keeps only the partitions in its biped slot. |
| `kPositionFrame` | 128 units. `PositionBake` maps each position axis from -128..128 into 0..1. |
| `LocalPositionBake` | Maps each position axis into 0..1 about `MeshData::center` and `MeshData::radius`. `BuildBake` returns a `problem` when the radius is zero. |
| `BuildDistanceBake` | Bakes the distance from one point, divided by `kDistanceFrame`, as a scalar. |
| `kDistanceFrame` | 256 units. The distance that maps to 1. |
| `DefinitionOf` | Names a `BakeKind` or a `DistanceSource` as text, for example `bake boneWeight [a, b]`. `BoneWeightBake` names are sorted first. |
| `DistanceBakeIdentity` | Names a distance bake by its origin, for example `distance 0 0 0`. |

In `src/`, only `mesh/` refers to `BakeKey`, `KeyOf` and `DefinitionOf`.
The tests in `tests/mesh/mesh_tests.cpp` cover them.

### Texture size (`TextureSize.h`)

`TextureRequirements` (`planners/TextureDemand.h`) carries a `TextureSize`,
and `TextureLab::Acquire` takes one. The constructor clamps its argument
into `kMin`..`kMax`, so every size downstream is in range.

| Symbol | Description |
|---|---|
| `TextureSize` | A texture edge length in pixels. `Pixels()` reads the clamped value. |
| `TextureSize::kMin` | 64 pixels. |
| `TextureSize::kMax` | 4096 pixels. |

### Coverage (`MeshFacts.h`)

Coverage tells which slots and bones a mesh touches. `render/MeshCache.cpp`
calls `FactsOf` once per mesh read and stores the result in
`MeshEntry::facts`.

| Type | Description |
|---|---|
| `SlotCoverage` | One slot: the raw number, a name, and the triangle count. |
| `BoneCoverage` | One bone: its name and its share of the mesh's total skin weight. |
| `MeshFacts` | Both lists: every covered slot and every weighted bone. `FactsOf` fills it from `SlotsOf` and `BonesOf`. |

### Islands (`Islands.h`)

An **island** is one group of connected triangles. `AnalyseMesh` segments a
mesh twice: into **components**, which join vertices at the same position,
and into **charts**, which join vertices at the same uv.
`render/MeshCache.cpp` stores the result in `MeshEntry::analysis`, and
`BuildIslandBake` bakes each vertex's island id.

| Symbol | Description |
|---|---|
| `IslandSource` | The segmentation: `kComponent` or `kChart`. |
| `IslandSourceSpec` | One row of `kIslandSources`: the wire name, the plain name, and the `BakeKind` that bakes the ids. `IslandSourceName`, `PlainIslandSourceName` and `IslandBakeOf` read it. |
| `MeshIsland` | One island: the source, the id, the triangle count and share, the dominant bone and its share, and the centroid. `twin` holds the id of the island in the other segmentation that covers exactly the same vertices. |
| `MeshAnalysis` | The result: the island list, the per-vertex `componentOf` and `chartOf` id tables, and the component and chart counts. |
| `kMaxIslands` | 255. The largest number of islands kept per segmentation. The largest islands by triangle count are kept. |
| `kNoIsland` | 0xFFFF. The id of a vertex outside every kept island. |
| `kChartWeldUv` | 1/4096. The uv cell size inside which chart vertices join. |

### Material clusters (`MaterialClusters.h`)

`ClusterMaterial` groups the texels of a sampled texture by material
likeness with k-means++. It reads at most `kMaxSampleTexels` texels and
clamps the cluster count to `kMaxMaterialClusters` (`recipe/Recipe.h`).
`ClusterTexels` runs at most `kMaxClusterIterations` passes (`recipe/Recipe.h`). This
header does not include `Mesh.h`, because clustering reads a
`MaterialSample` and never a mesh. `ClusterSettings` and `ChannelWeights`
live in `recipe/Recipe.h`, because `MaterialClustersSource` carries them.

| Symbol | Description |
|---|---|
| `MaterialTexel` | One texel: roughness, metallic, occlusion, reflectance, diffuse luma, and diffuse RGB. |
| `MaterialSample` | The sampled texture: width, height and texels. `TextureLab::ReadMaterialSample` (`render/TextureLabReadback.cpp`) produces it. |
| `MaterialCluster` | One cluster: the id, the centroid texel, the share of the sample, and a text description from `DescribeTexel`. |
| `MaterialAnalysis` | The result: the `ClusterSettings` it ran with and the clusters, largest first. |
| `kMaxSampleTexels` | 64 x 64. The largest number of texels that `ClusterMaterial` reads. |

`NearestCluster` returns the id of the cluster nearest to one texel under the
analysis weights. `DescribeTexel` names a texel with one word per band, for
example `polished bright metal`.

### Shell pose (`ShellPose.h`)

A shell is posed through its skin-to-bone transforms. `engine/ManagerTick.cpp`
resolves the recipe's `ShellSettings` pose through the signals into a
`ShellPoseValues`. `ShellBinding::Pose` (`render/Shell.cpp`) then calls
`PosedTransform` once per bone.

| Type | Description |
|---|---|
| `RestSkinToBone` | A rest transform: a 3 x 3 rotation, a translation and a uniform scale. |
| `ShellPoseValues` | The pose: a per-axis inflate, an offset, a scale about `scalePoint`, and a spin in turns about `spinAxis`. |

## How a mesh flows

```
RE::BSGeometry (engine buffers)
  │  ReadBuffers, BoneName, RawPartitionOf       render/MeshReader.cpp
  ▼
RawPartition (byte spans + VertexLayout)
  │  DecodePartition                             Mesh.cpp
  │    DecodeVertex per vertex, index bytes checked,
  │    TrianglesWithin drops out-of-range triangles
  ▼
MeshData (MeasureBound, HashBytes)               render/MeshReader.cpp
  │  MeshCache::Get stores one MeshEntry          render/MeshCache.cpp
  ├── FactsOf ─▶ MeshEntry::facts                MeshFacts.cpp
  └── AnalyseMesh ─▶ MeshEntry::analysis          Islands.cpp
        Flatten → Connect(PositionCell) → Label   (components)
                → Connect(UvCell)       → Label   (charts)
                → PairTwins
  │
  ▼
BuildBakeBuffersStep                             planners/RenderPlan.h
  │  ExecuteBuildBakeBuffersStep                 render/RenderInstance.cpp
  │    BakeBuffersFor ─▶ BuildIslandBake          Islands.cpp
  │                   └▶ BuildBake                Mesh.cpp
  │    or BuildDistanceBake                       Mesh.cpp
  ▼
BakeBuffers
  │  ExecuteBakeMeshStep → TextureLab::BakeMesh   render/RenderInstance.cpp,
  ▼                                               render/TextureLabPass.cpp
baked texture

MaterialSample                                   render/TextureLabReadback.cpp
  │  ClusterMaterial                             MaterialClusters.cpp
  │    TexelAxesOf → ClusterTexels → RankedClusters
  ▼
MaterialAnalysis    ExecuteClusterMaterialStep   render/RenderInstance.cpp
                    Compositor::MaterialRecord   render/CompositorBake.cpp

ShellPoseValues + RestSkinToBone
  │  PosedTransform                              ShellPose.cpp
  ▼
posed skin-to-bone  ShellBinding::Pose           render/Shell.cpp
```

## The files

| File | What it owns |
|---|---|
| `Mesh.h` / `Mesh.cpp` | `MeshData` and its parts, `DecodeVertex`, `DecodePartition`, `TrianglesWithin`, `HalfToFloat`, `MeasureBound`, `HashBytes`, `AddBoneWeights`, `BuildBake`, `BuildDistanceBake`, and the bake names `DefinitionOf`, `DistanceBakeIdentity` and `KeyOf`. |
| `TextureSize.h` | The clamped `TextureSize`. |
| `MeshFacts.h` / `MeshFacts.cpp` | Slot and bone coverage: `SlotsOf`, `BonesOf`, `FactsOf`. |
| `Islands.h` / `Islands.cpp` | Component and chart segmentation and island bakes. `AnalyseMesh` runs the named phases `Flatten`, `Connect`, `Label` and `PairTwins`. `Label` runs `RankedRoots`, `LabelVertices`, `TallyIslands` and `IslandFrom`. `PairTwins` runs `TwinCandidatesOf`, `TwinChartOf` and `LinkChartTwin`. `DisjointSets` holds the union-find state. |
| `MaterialClusters.h` / `MaterialClusters.cpp` | K-means++ material clustering. `ClusterMaterial` runs `TexelAxesOf`, `ClusterTexels` and `RankedClusters`. `ClusterTexels` runs `SeedCentroids`, then alternates `Recentre` and `Assign`. `NearestCluster` and `DescribeTexel` read a finished analysis. |
| `ShellPose.h` / `ShellPose.cpp` | `PosedTransform` and its 3 x 3 matrix helpers. |

## See also

- `REFERENCE.md` → *Meshes, bakes and analysis*: the two biped-slot
  representations, the renderer byte-count limits, the bake frames, and the
  weld and clustering rules.
- `docs/conventions.md` → *Multi-phase algorithms* → *Named phases* and
  *Bounds*: the rules that `AnalyseMesh`, `ClusterMaterial`, `kMaxIslands`
  and `kMaxMaterialClusters` follow.
- `docs/conventions.md` → *Multi-phase algorithms* → *Parse, don't
  validate*: the rule that `DecodePartition` follows.
