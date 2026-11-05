# mesh/

The mesh layer. It turns the vertex and index bytes that the renderer reads
into a trusted `MeshData`. It derives per-vertex **bake** values, **island**
segmentations, **slot** and bone coverage, and the posed transform of a
**shell**. It also groups the texels of a sampled material into **material
groups** and picks the area that one geometry lights. It is engine-free and
compiles natively. Its tests live in `tests/mesh/` and run through
`ctest --preset native`. Its `ALLOWS` row in `tools/gate.py` is
`'mesh': ('Core.h', 'recipe', 'mesh')`. The `planners`, `studio`, `actors`,
`orchestration`, `validator`, `render`, `engine` and `menu` layers may
include it.

## What it owns

`DecodePartition` is the mesh boundary. `render/MeshReader.cpp` reads raw
bytes only. This module turns those bytes into a `MeshPartition` or rejects
them with `std::nullopt`. A malformed buffer therefore never reaches the rest
of the plugin. `RasterOf` is the material boundary. It rejects a raster of
the wrong size and clamps every texel channel into 0..1.

From a decoded `MeshData` and a sampled material, the module computes these
results:

| Result | Function | File |
|---|---|---|
| Per-vertex bake geometry | `BuildBake`, `BuildDistanceBake`, `BuildIslandBake` | `Mesh.cpp`, `Islands.cpp` |
| Connected-component and uv-chart segmentation | `AnalyseMesh` | `Islands.cpp` |
| Slot and bone coverage | `FactsOf`, `SlotsOf`, `BonesOf` | `MeshFacts.cpp` |
| The chart id of each texel of a material raster | `ChartRasterOf` | `MaterialRegions.cpp` |
| Contiguous material regions inside each chart | `SegmentRaster` | `MaterialSegments.cpp` |
| Material groups fitted over the regions | `GroupRegions` | `MaterialGroupFit.cpp` |
| Per-texel group scores, smoothed odds and labels | `ScoringOf`, `SmoothedProbabilities`, `LabelWithOdds` | `MaterialScoring.cpp` |
| The whole grouping of one material | `GroupMaterial` | `MaterialRegions.cpp` |
| The group and charts that one geometry lights | `PickArea` | `AreaPick.cpp` |
| A Gaussian mixture fit with a chosen component count | `ChooseMixture` | `GaussianMixture.cpp` |
| The native filter and noise references for the GPU passes | `FilteredField`, `NoiseFieldsAt` | `FieldFilter.cpp`, `Noise.cpp` |
| The files of a material dump | `EncodeTga`, `MeshDumpJson`, `MetaDumpJson` | `MaterialDump.cpp` |
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
`MeshData::hash` with it. `HashPartitionsAndBound` then folds in each
partition's slot and bone names and the model bound, which the bakes also
read. `AddBoneWeights` adds one vertex's positive, finite bone weights to a
map by bone name. `BonesOf` and the island tally in `Islands.cpp` both use
it.

### Bakes (`Mesh.h`)

A **bake** holds one value per vertex at that vertex's uv.
`ExecuteBuildBakeBuffersStep` in `render/RenderInstance.cpp` builds the
buffers, and `TextureLab::BakeMesh` (`render/TextureLabPass.cpp`) rasterizes
them into a texture. A bake that cannot be built returns an empty
`BakeBuffers` with a `problem` string.

| Type | Description |
|---|---|
| `BakeVertex` | One baked vertex: its `u` and `v`, and three float channels of value. |
| `BakeBuffers` | The bake geometry: vertices, indices, a `vector` flag, an optional `NoiseShading`, and the `problem` string. The `vector` flag marks a bake whose three channels form one vector value. |
| `NoiseShading` | The GPU shading of a noise bake: the octave count, the seed and the `NoiseStyle` (`recipe/Recipe.h`). |
| `NoiseSettings` | A noise bake's full settings: the cell size, the per-axis stretch and the `NoiseShading`. |
| `BakeKey` | A bake identity: the definition text and the pixel size, ordered field by field. `KeyOf` builds it from a definition and a `TextureSize`. |

| Symbol | Description |
|---|---|
| `BuildBake` | Bakes one `BakeKind` from `recipe/Recipe.h`. It returns a `problem` for `PartBake` and `ChartBake`, because those kinds need `BuildIslandBake`. A `PartitionBake` keeps only the partitions in its biped slot. |
| `kPositionFrame` | 128 units. `PositionBake` maps each position axis from -128..128 into 0..1. |
| `LocalPositionBake` | Maps each position axis into 0..1 about `MeshData::center` and `MeshData::radius`. `BuildBake` returns a `problem` when the radius is zero. |
| `NoiseBake` | `BuildBake` writes each vertex position divided by the cell size and the stretch, and copies the `NoiseShading` into `BakeBuffers::noise`. |
| `NoiseSettingsOf` | Clamps raw noise values into the `recipe/Recipe.h` bounds and falls back to the defaults for a value that is not finite. `render/RenderInstance.cpp` calls it with evaluated arguments. |
| `LiteralNoiseSettings`, `NoiseBakeOf` | Convert a `NoiseBake` with literal parameters to `NoiseSettings`, and back. |
| `BuildDistanceBake` | Bakes the distance from one point, divided by `kDistanceFrame`, as a scalar. |
| `kDistanceFrame` | 256 units. The distance that maps to 1. |
| `DefinitionOf` | Names a `BakeKind` or a `DistanceSource` as text, for example `bake boneWeight [a, b]`. `BoneWeightBake` names are sorted first. |
| `DistanceBakeIdentity` | Names a distance bake by its origin, for example `distance 0 0 0`. |

### Noise (`Noise.h`)

The GPU shades a noise bake in `shaders/Noise.hlsl`. These functions are the
native counterpart, and `tests/mesh/noise_tests.cpp` checks them.
`NoiseFieldsAt` evaluates three fields at one cell position, each at its own
offset and seed.

| Symbol | Description |
|---|---|
| `ValueNoise`, `FractalNoise` | Lattice value noise at one cell position, and its sum over octaves. |
| `CellularEdge`, `CellularNoise` | The gap between the nearest and second-nearest cell point, clamped to 0..1, and its sum over at most 4 octaves. |
| `StyledNoise` | Selects the noise for a `NoiseStyle`: fractal for `kBlobs`, folded fractal for `kRidges`, cellular for `kCracks`. |
| `kNoiseFields` | 3. The number of fields that `NoiseFieldsAt` returns. |
| `kNoiseFieldOffsets` | The cell offset of each field. |
| `kNoiseFieldSeedStep`, `kNoiseOctaveSeedStep`, `kCellularSeedStep` | 101, 7919 and 1013. The seed steps between fields, octaves and cellular points. |

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
`render/MeshCache.cpp` stores the result in `MeshEntry::analysis`.
`BuildIslandBake` bakes each vertex's island id, and `ChartRasterOf` reads
the chart ids to label a material raster.

| Symbol | Description |
|---|---|
| `IslandSource` | The segmentation: `kComponent` or `kChart`. |
| `IslandSourceSpec` | One row of `kIslandSources`: the wire name, the plain name, and the `BakeKind` that bakes the ids. `IslandSourceName`, `PlainIslandSourceName` and `IslandBakeOf` read it. |
| `MeshIsland` | One island: the source, the id, the triangle count and share, the dominant bone and its share, and the centroid. `twin` holds the id of the island in the other segmentation that covers exactly the same vertices. |
| `MeshAnalysis` | The result: the island list, the per-vertex `componentOf` and `chartOf` id tables, and the component and chart counts. |
| `kMaxIslands` | 255. The largest number of islands kept per segmentation. The largest islands by triangle count are kept. |
| `kNoIsland` | 0xFFFF. The id of a vertex outside every kept island. |
| `kChartWeldUv` | 1/4096. The uv cell size inside which chart vertices join. |

### Material texels (`MaterialClusters.h`)

`TextureLab::ReadMaterialSample` (`render/TextureLabReadback.cpp`) reads the
RMAOS and diffuse maps back as a `MaterialPixels`. `MaterialSampleOf`
decodes those bytes into a `MaterialSample`. A finished grouping describes
each group as one `MaterialCluster`, and `PickCluster` picks one of them.

| Symbol | Description |
|---|---|
| `MaterialPixels` | The read-back bytes: the edge length and the RGBA bytes of the RMAOS and diffuse maps. |
| `MaterialTexel` | One texel: roughness, metallic, occlusion, reflectance, diffuse luma, diffuse RGB, and `detail`. `MaterialSampleOf` reads RMAOS red, green, blue and alpha as the first four, and diffuse alpha as `detail`. |
| `MaterialSample` | The decoded texture: width, height and texels. |
| `kMaxSampleTexels` | 256 x 256. The largest number of texels that `MaterialSampleOf` decodes. |
| `Chroma` | Two opponent colour axes of a diffuse colour, `orangeBlue` and `greenMagenta`, each in 0..1. `ChromaOf` computes it. |
| `MaterialCluster` | One group as the menu and the pick see it: the id, the mean texel, the share of the labelled texels, and a text description from `DescribeTexel`. |
| `MaterialAnalysis` | The clusters, largest share first. |
| `kClusterPalette` | The display colour of each group id. `menu/ClusterChart.cpp` and `planners/ShaderSource.cpp` read it. |

| Function | Description | Example |
|---|---|---|
| `DescribeTexel` | Names a texel with one word each for roughness, luma and metallic. | `polished bright metal` |
| `PickCluster` | Maps a pick value in 0..1 to a cluster id. Each cluster's weight is its share raised to the area bias, which `kMaxAreaBias` (`recipe/Recipe.h`) bounds. | `PickCluster(analysis, 0.5f, 1.0f)` |
| `PickValueOf` | Returns the pick value at the middle of one cluster's weight span. `menu/Workspace.cpp` uses it. | `PickValueOf(analysis, 2, 1.0f)` |

### Material regions (`MaterialRegions.h`, `RasterGrid.h`)

A **material raster** is a square grid of texels in which each texel also
carries a chart id. `ChartRasterOf` fills the chart ids from the mesh's uv
triangles, and `RasterOf` pairs them with the texels. `SegmentRaster` then
divides the covered texels into **regions**: small contiguous areas of
similar material that never cross a chart edge.

| Symbol | Description |
|---|---|
| `MaterialRaster` | The edge length, the texels and the per-texel chart ids. |
| `kMinRasterSide`, `kMaxRasterSide` | 8 and 1024. The edge lengths that `RasterOf` and `ChartRasterOf` accept. |
| `kNoChart` | 0xFFFF. The chart id of a texel that no triangle covers. |
| `SegmentWeights` | The weight of each texel channel in the region feature: luma, chroma, roughness, metallic, occlusion, reflectance and detail. |
| `SegmentSettings` | The target region count (650), the compactness (10), and the `SegmentWeights`. |
| `Regions` | The edge length, the region id of each texel, and the region count. |
| `kNoRegion` | -1. The region id of an uncovered texel. |
| `Grid` | The edge length, with `At` and `Size` for flat indices. |
| `ForEachNeighbour` | Visits the four edge neighbours of one texel. |
| `IsCovered`, `CoveredCount` | Test one texel, or count the texels, whose chart is not `kNoChart`. |

`SegmentRaster` runs these phases in `MaterialSegments.cpp`:

| Phase | Description |
|---|---|
| `FeaturesOf` | Builds one feature per texel: the CIELAB value of the range-stretched diffuse colour and the scalar channels, each times its weight. |
| `GradientOf` | Computes the Sobel gradient magnitude of the features. |
| `SeedsOf`, `SmoothestNear` | Place one centre per grid step inside each chart, add one centre for a chart that no grid point hits, and move each centre to its lowest-gradient neighbour in the same chart. |
| `Assign`, `MoveCentres` | Run 10 rounds. A texel joins the nearest centre of its own chart by feature and position distance, and each centre moves to the mean of its texels. |
| `FillWithinCharts`, `KeepLargestPieces` | Give unassigned texels the label of a neighbour in the same chart, and keep only the largest connected piece of each region. |
| `AbsorbSmall` | Joins a region smaller than half a grid step squared to the neighbouring region in the same chart that shares the most edges with it. |
| `LabelLeftovers`, `Compacted` | Give each remaining connected piece a new region, and renumber the regions from 0. |

### Gaussian mixture (`GaussianMixture.h`)

A **Gaussian mixture** models a set of points as a weighted sum of normal
distributions. `GroupRegions` fits one over the region descriptors to find
the material groups. `FitMixture` seeds expectation-maximisation with a
k-means labelling. `ChooseMixture` fits every component count in its range
and keeps the fit with the lowest `CompletedCriterion`.

| Symbol | Description |
|---|---|
| `Points` | A dimension and the flat point values. `PointsOf` rejects a dimension outside 1..`kMaxMixtureDimension`, a partial point, too many points, and a value that is not finite. `CountOf` and `PointAt` read it. |
| `Gaussian` | A mean, a full covariance and a weight. `GaussianOf` fits one to a member list. |
| `PreparedGaussian` | A `Gaussian` ready for scoring: the mean, the lower Cholesky factor, and the log scale. `Prepare` fails when the covariance is not positive definite. |
| `Mixture` | The dimension, the components and the log likelihood. |
| `MixtureSearch` | The component count range (2..8), the seed, the restart count per count (5), and the covariance regularisation. |
| `kMaxMixtureDimension` | 12. |
| `kMaxMixtureComponents` | 8. |
| `kMaxMixturePoints` | 2^20. |

| Function | Description |
|---|---|
| `LogDensity`, `Posteriors` | Score one point under one prepared Gaussian, and give its normalised membership in each. |
| `InformationCriterion` | The Bayesian information criterion of a mixture. |
| `CompletedCriterion` | The information criterion plus twice the membership entropy, so overlapping components score worse. |
| `MostLikelyComponents` | The index of the most likely component for each point. |

### Material groups (`MaterialRegions.h`)

`GroupRegions` turns regions into material groups. It describes each region
by its mean texel axes and its luma spread, fits a mixture over those
descriptors, and gives each texel the group of its region. It then merges
the pair of groups that the fitted Gaussians confuse most, while that
confusion exceeds the merge threshold, and fits one Gaussian per remaining
group. `GroupMaterial` runs the whole chain, from a `MaterialSample` and the
chart ids to a `MaterialGrouping`.

| Symbol | Description |
|---|---|
| `TexelAxes` | The 8 axes of one texel: roughness, metallic, occlusion, reflectance, luma, the two `Chroma` axes, and detail. `AxesOfTexel` computes it. |
| `kTexelAxes` | 8. |
| `Standardisation` | The mean and spread of each axis over the covered texels. `Standardised` maps a texel into those units. |
| `MaterialGroup` | One group: the id, its `Gaussian` over standardised axes, and its share. |
| `kNoGroup` | -1. The group id of an unlabelled texel. |
| `kMaxGroups` | `kMaxMixtureComponents`. |
| `kMinCoveredTexels` | 64. `GroupRegions` refuses a raster with fewer covered texels. |
| `GroupMerge` | One merge: the confused share, the kept id and the absorbed id. The larger group keeps its id. |
| `GroupSettings` | The merge threshold (0.3) and the mixture seed. |
| `MaterialGroups` | The `Standardisation`, the fitted component count, the groups, the merges, and the group of each texel. |
| `GroupScoring` | One group in the form the GPU scores: the mean, the packed lower-triangle whitening matrix, the log scale and the log share. `ScoringOf` builds it, `ScoreOf` evaluates it for one texel, and `render/TextureLabPass.cpp` uploads it. |
| `kWhiteningFloats` | 36. The packed size of an 8 x 8 lower triangle. |
| `GroupProbabilities` | The edge length and, per texel, each group's smoothed membership as a byte. `SmoothedProbabilities` computes the posteriors and runs a guided filter with the diffuse colour as the guide. |
| `LabelledTexels` | Every `kLabelledStride`th texel on both axes, with its label. |
| `kLabelledStride` | 4. |
| `ChartCover` | One group on one chart: the group's share of the chart's labelled texels, and the chart's share of all labelled texels. `ChartCoversOf` builds the table. |
| `kMaxPickChart` | 254. The highest chart id that enters the cover table. |
| `MaterialGrouping` | The full result: the `ClusterSettings` (`recipe/Recipe.h`), the labelled texels, the groups without their per-texel table, the scoring, the smoothed odds, the `MaterialAnalysis`, and the chart covers. |

| Function | Description |
|---|---|
| `LabelTexels` | Labels each covered texel with its most likely group. |
| `OddsWeightOf` | 1 when `ClusterSettings::smoothing` is above 0, otherwise 0. |
| `LabelWithOdds` | Labels each covered texel with the group of the highest score, where the score adds the weighted log of the smoothed odds less the group's log share. |
| `AnalysisOf` | Builds the `MaterialAnalysis` from the labels. |

### Area pick (`AreaPick.h`)

An **area pick** decides what one geometry of an armor lights. `PickArea`
first tests the geometry with `IsGeometryPicked`. It then picks one material
group with `PickCluster`, and, when the settings carry a `ChartChoice`, the
charts of that group that light. `AreaDrawOf` in `render/RenderInstance.cpp`
calls it for a `DrawClustersStep` with an area.

| Symbol | Description |
|---|---|
| `AreaPickSettings` | The seed, the lit geometry count, the area bias, and an optional `ChartChoice`. `kEveryGeometryCount` (`recipe/Recipe.h`) lights every geometry. |
| `ChartChoice` | The chart count, the least cover a chart needs (`kDefaultMinCover`), and the least lit share of the geometry. |
| `GeometryPlace` | The geometry's index and the geometry count. |
| `PickedArea` | The picked group, the optional table of lit charts, the lit chart count, and the lit share of the geometry. |
| `LitCharts` | One flag per chart id. |
| `kChartSlots` | 256. The size of `LitCharts`. |
| `CandidateCharts` | The charts where the group covers at least the least cover, or the best chart when none does. |

### Field filter (`FieldFilter.h`)

The GPU filter pass in `shaders/Filter.hlsl` blurs, grows, shrinks or edges
a texture. `render/TextureLabPass.cpp` computes its kernel weights with
`FilterWeight`, and `render/RenderInstance.cpp` computes its tap count with
`FilterTaps`. `FilteredField` is the native counterpart that
`tests/mesh/fieldfilter_tests.cpp` checks.

| Symbol | Description |
|---|---|
| `FilterTaps` | Rounds a radius to a tap count in 0..`kMaxFilterTaps`. |
| `kMaxFilterTaps` | `kMaxFilterRadius` (`recipe/Recipe.h`) as an integer. |
| `FilterWeight` | The Gaussian weight of one tap offset. |
| `FilterPass` | One texel after a pass: the blurred value, the lowest value and the highest value. |
| `FilteredField` | Runs one horizontal and one vertical pass and finishes with the `FilterOperation`. With chart ids, a tap never reads across a chart edge. |

### Material dump (`MaterialDump.h`)

A **material dump** writes one geometry's maps and mesh to a folder for
inspection outside the game. `engine/MaterialDump.cpp` collects the data and
writes the files. This header holds the engine-free formats and names.

| Symbol | Description |
|---|---|
| `DumpMap`, `DumpMapSpec`, `kDumpMaps` | The dumped maps (`kDiffuse`, `kRmaos`, `kNormal`) with their names and TGA file names. `DumpMapName` and `DumpMapFile` read the table. |
| `kDumpMeshFile`, `kDumpMetaFile` | `mesh.json` and `meta.json`. |
| `RgbaImage` | Width, height and RGBA bytes. `EncodeTga` writes it as a 32-bit TGA and rejects an edge above `kMaxTgaSide` (4096). |
| `MeshDump` | The mesh JSON text and its `DumpedMesh` counts. `MeshDumpJson` writes the uvs, triangles, partitions and per-triangle chart and part ids. It refuses a mesh above `kMaxDumpVertices` or `kMaxDumpTriangles`. |
| `DumpedMesh` | The vertex and triangle counts, and the reason the island ids are missing, if they are. |
| `DumpMeta`, `DumpArmor`, `DumpedMap`, `DumpSkip` | The content of `meta.json`: the geometry, the armor, the build, the time, each map's source and size, the mesh counts, and each skipped item with its reason. `MetaDumpJson` writes it. |
| `SanitisedName`, `DumpFolderName` | Reduce text to letters, digits, `-` and `_` within `kMaxDumpNameBytes`, and build the folder name from the armor, the geometry, a stamp and the attempt number. |
| `kDumpMapSide`, `kMaxDumpFolderAttempts` | 512 and 9. No code in `src/` reads them. |

### Shell pose (`ShellPose.h`)

A shell is posed through its skin-to-bone transforms. `engine/ManagerTick.cpp`
resolves the recipe's `ShellSettings` pose through the signals into a
`ShellPoseValues`. `ShellBinding::Pose` (`render/Shell.cpp`) then calls
`PosedTransform` once per bone.

| Type | Description |
|---|---|
| `RestSkinToBone` | A rest transform: a 3 x 3 rotation, a translation, a uniform scale, and the centre that inflation pushes away from. |
| `ShellPoseValues` | The pose: a per-axis inflate, an offset, a scale about `scalePoint`, and a spin in turns about `spinAxis`. |

## How a material sample becomes groups and a pick

```
MaterialPixels                                   render/TextureLabReadback.cpp
  │  TextureLab::CollectMaterialSample           render/TextureLabPass.cpp
  │  RenderInstance::CollectGrouping             render/RenderInstance.cpp
  │    GroupingKeyOf, GroupingTicket
  ▼
MaterialGrouper::Poll ─▶ GroupJob (worker)       planners/MaterialGrouper.cpp
  │  MaterialSampleOf                            MaterialClusters.cpp
  │  ChartsOf ─▶ ChartRasterOf                    MaterialRegions.cpp
  ▼
GroupMaterial                                    MaterialRegions.cpp
  │  RasterOf ─▶ MaterialRaster                   MaterialRegions.cpp
  │  SegmentRaster ─▶ Regions                     MaterialSegments.cpp
  │    FeaturesOf → GradientOf → SeedsOf → SmoothestNear
  │    → (Assign → MoveCentres) x 10 → FillWithinCharts
  │    → KeepLargestPieces → AbsorbSmall → LabelLeftovers → Compacted
  │  GroupRegions ─▶ MaterialGroups               MaterialGroupFit.cpp
  │    StandardisationOf → DescriptorsOf
  │    → ChooseMixture                            GaussianMixture.cpp
  │    → GroupOfTexel → SampleOf → MergeConfused → GroupsOf
  │  ScoringOf, SmoothedProbabilities,           MaterialScoring.cpp
  │  LabelWithOdds
  │  AnalysisOf, ChartCoversOf, LabelledOf        MaterialRegions.cpp
  ▼
MaterialGrouping (shared, cached by MaterialGrouper)
  │  ExecuteDrawClustersStep → AreaDrawOf         render/RenderInstance.cpp
  ▼
PickArea                                         AreaPick.cpp
  │  IsGeometryPicked
  │  PickCluster over GroupsWithCharts           MaterialClusters.cpp
  │  PickCharts(CandidateCharts, InShuffleOrder) AreaPick.cpp
  ▼
PickedArea ─▶ TextureLab::ClusterDraw
  │  TextureLab::RenderClusters                  render/TextureLabPass.cpp
  ▼
cluster texture
```

## The files

| File | What it owns |
|---|---|
| `Mesh.h` / `Mesh.cpp` | `MeshData` and its parts, `DecodeVertex`, `DecodePartition`, `TrianglesWithin`, `HalfToFloat`, `MeasureBound`, `HashBytes`, `HashPartitionsAndBound`, `AddBoneWeights`, `BuildBake`, `BuildDistanceBake`, the noise settings, and the bake names `DefinitionOf`, `DistanceBakeIdentity` and `KeyOf`. |
| `TextureSize.h` | The clamped `TextureSize`. |
| `MeshFacts.h` / `MeshFacts.cpp` | Slot and bone coverage: `SlotsOf`, `BonesOf`, `FactsOf`. |
| `Islands.h` / `Islands.cpp` | Component and chart segmentation and island bakes. `AnalyseMesh` runs the named phases `Flatten`, `Connect`, `Label` and `PairTwins`. `Label` runs `RankedRoots`, `LabelVertices`, `TallyIslands` and `IslandFrom`. `PairTwins` runs `TwinCandidatesOf`, `TwinChartOf` and `LinkChartTwin`. `DisjointSets` holds the union-find state. |
| `Noise.h` / `Noise.cpp` | The native noise functions. |
| `FieldFilter.h` / `FieldFilter.cpp` | The filter taps and weights, and the native `FilteredField`. |
| `MaterialClusters.h` / `MaterialClusters.cpp` | `MaterialTexel`, `MaterialSample`, `MaterialAnalysis`, `MaterialSampleOf`, `ChromaOf`, `DescribeTexel`, `PickCluster` and `PickValueOf`. |
| `MaterialRegions.h` | Every material raster, region and group type, and the functions over them. |
| `RasterGrid.h` | `Grid`, `ForEachNeighbour`, `IsCovered` and `CoveredCount`. |
| `MaterialRegions.cpp` | `RasterOf`, `ChartRasterOf`, `GroupMaterial`, `AnalysisOf` and `ChartCoversOf`. |
| `MaterialSegments.cpp` | `SegmentRaster` and its phases. |
| `MaterialGroupFit.cpp` | `GroupRegions`, `LabelTexels`, `AxesOfTexel`, `Standardised` and `PreparedOf`. |
| `MaterialScoring.cpp` | `ScoringOf`, `ScoreOf`, `SmoothedProbabilities` with its guided filter, `OddsWeightOf` and `LabelWithOdds`. |
| `GaussianMixture.h` / `GaussianMixture.cpp` | Points, Gaussians, mixture fitting and selection. |
| `AreaPick.h` / `AreaPick.cpp` | `PickArea`, `IsGeometryPicked` and `CandidateCharts`. |
| `MaterialDump.h` / `MaterialDump.cpp` | The TGA encoder, the dump JSON writers and the folder names. |
| `ShellPose.h` / `ShellPose.cpp` | `PosedTransform` and its 3 x 3 matrix helpers. |

## See also

- `REFERENCE.md` → *Meshes, bakes and analysis*.
- `REFERENCE.md` → *studio* → *Material regions*, *Area pick* and
  *Material dump*.
- `docs/conventions.md` → *Multi-phase algorithms* → *Named phases* and
  *Bounds*: the rules that `AnalyseMesh`, `SegmentRaster`, `GroupRegions`,
  `kMaxIslands` and `kMaxMixtureComponents` follow.
- `docs/conventions.md` → *Multi-phase algorithms* → *Parse, don't
  validate*: the rule that `DecodePartition` and `RasterOf` follow.
