Status: active (2026-10-13) on branch `pure-tick`. Hybrid + merge is ported: P1 (c37ba33) and P2–P5 with D1–D3 as recommended (the user said continue) are built and wait for an in-game run. Open: the Temple seven-group gap, a studio chart that shows default settings only.

# Material regions: segment first, then group

## Context

A **material cluster** source (`MaterialClustersSource`) splits a geometry's material into groups that a mask can pick. Today it works in four steps:

1. The texture lab copies the material's RMAOS and diffuse maps, and a normal-detail measure, into a 64×64 **sample** (`TextureLab::SubmitMaterialSample`).
2. `ClusterMaterial` (`mesh/MaterialClusters.cpp`) runs k-means on the 4,096 sample texels in eight axes: roughness, metallic, occlusion, reflectance, luma, two chroma axes and detail. Each axis has a weight from the recipe.
3. `GuessClusterSettings` picks the weights from a per-axis two-group split (Otsu), and the cluster count from 2 to 8 by a centre-based silhouette score.
4. The cluster shader (`PSClusters`) assigns every pixel of the target to its nearest centre with the same weighted distance.

## Findings

| # | Finding | Effect |
|---|---|---|
| F1 | The sample covers the whole texture, including UV padding and atlas areas the mesh never shows. | Clusters form on texels nobody sees. |
| F2 | Texels are clustered as an unordered set. Their position and the mesh's charts are not used. | Masks speckle; a strap and a plate of similar colour merge. |
| F3 | k-means needs hand-set axis weights, and the guess needs six tuned constants (single-group line, discount, gap boost and floor, flat cutoff, tie-break). | Each new armor case adds a constant. |
| F4 | A per-axis two-group split under-credits an axis with three groups and cannot see materials that differ only in a combination of axes. | Weights are wrong for armors with three or more materials. |
| F5 | No score knows which materials the effect is about. | The guess can be right in general and wrong for the user's intent. |

## Route

| Step | What | Replaces |
|---|---|---|
| 0 | **Material dump.** A studio action writes a geometry's diffuse, RMAOS and normal maps at 512 px, its mesh (UVs, triangles, chart and part ids) and a metadata file to a dump folder. | Nothing; it gives the prototype real data. |
| 1 | **Regions.** An engine-free module splits the UV texture, masked by mesh coverage, into small contiguous regions that follow colour edges and chart borders (SLIC superpixels or Felzenszwalb graph segmentation). Each region gets a descriptor: per-axis mean and internal variation. | Raw-texel clustering, padding noise (F1, F2), the detail scale constants. |
| 2 | **Grouping.** A Gaussian mixture with per-cluster covariance over region descriptors, with the count chosen by BIC. | Weights, Otsu, boosts, silhouette tie-break (F3, F4). |
| 3 | **Intent.** Two clicks mark regions as "different" or "same"; the grouping splits or merges to honour them (Fisher discriminant or a constrained mixture). | Best guess as the only control (F5). |
| 2b | **Per-texel labels.** Each group gets the mean and covariance of the texels in its regions. Every texel is scored against the groups by its own values, and the scores are smoothed by a guided filter with the diffuse map as guide (radius 2–4 texels). | Region-mean labels, which lose features thinner than a superpixel. |
| 4 | **Mask.** The cluster shader scores each pixel against the group Gaussians, and a guided-filter pass smooths the scores; a mask selects groups. | The weighted nearest-centre distance in the cluster shader. |

Steps 1 and 2 are built and judged as a standalone prototype against dumps from the user's armor (the bonemold cuirass first) before anything in the render path changes. Step 4 changes the term and source shape; existing `materialClusters` recipes either migrate or keep the k-means path alongside.

## Prototype results

The prototype (`proto.py`, `hybrid.py`) compares three methods on nine dumps: today's k-means, regions (300 SLIC superpixels, Gaussian mixture by BIC) and the hybrid (step 2b).

| Armor | Finding |
|---|---|
| Bonemold cuirass | Regions and hybrid find bone against leather (k=2). Today splits by brightness into stripes. |
| Chitin cuirass | Regions lose the straps and rings. The hybrid at radius 2–4 keeps them without today's speckle. Radius 16 breaks thin straps. |
| Deathbrand | The dump holds the same maps and mesh as the Stalhrim dump (`stahlrim_armor_light_male_body`); both armors resolve to armor addon 0403B3DB. |
| Stalhrim | The hybrid finds leather, ice, metal frame and fur. A fifth group of dark edge texels speckles the fur. The merge folds it into leather (Fisher 0.89); some dark fur texels then go to leather. Today separates only ice. |
| Temple Priest robes | The hybrid finds cloth, sash, gold trim lines and the fur ring. Regions scatter blocks. Today separates only the trim. |
| Nordic Carved greaves | The hybrid draws seams and hem cleanly. Regions blotch them. |
| Chitin Heavy greaves | Regions and hybrid give the same clean split. Today speckles. |
| Blackguard | Mixed. The hybrid finds the buckles but merges raised straps into the vest leather, which today splits by brightness. The merge folds a blue group into the leather (Fisher 0.91), and the small leather pieces then blotch more. |
| Ahzidal | 2% coverage; too little area to judge. |

- About 930 superpixels instead of 300 make BIC pick k=4 on Bonemold instead of 2. The superpixel count therefore changes the grouping, not only the region size.
- **Merge step** (`hybrid.py`, `merge_groups`). Each group gets the mean and covariance of its texels. The Fisher distance of a pair is the distance between the means, measured in the pair's pooled covariance. The pair with the smallest distance merges while that distance is under 1.1, and the larger group keeps its id. On the eight distinct dumps the merged pairs measure 0.89 and 0.91; the closest pairs judged real (Temple trim, Chitin straps) measure 1.34 to 1.55. The threshold rests on these eight dumps only.
- The merge threshold is a setting; the bench offers 1.0 to 2.0. On Stalhrim the merged edge group also carried stitch lines that are worth keeping.
- **Jump flood, rejected.** Seeds are texels with a group probability over a confidence setting and at least 5 of 8 agreeing neighbours; occlusion grooves can also be seeds. Every other texel takes the nearest seed's label within its UV chart. Confident speckle (Stalhrim fur) passes the seed rules and stays, and thin straps (Chitin) fragment unless grooves are kept.
- **Guided filter, then jump flood, rejected.** Smoothing at radius 4 first, then flooding texels under 0.8 confidence, with grooves keeping their unsmoothed label, is worse than the guided filter alone on Stalhrim, Chitin, Blackguard and Temple: straps and trim lines break into dots, and fur and leather get blotches.
- **Known methods.** Step 2b is cost-volume filtering (Rhemann et al. 2011): per-group probabilities filtered by a guided filter, then the largest wins. That is one pass of a fully connected CRF (Krähenbühl & Koltun 2011) without the unary term added back. The CRF solved by mean field (5 passes, each a guided filter of the probabilities plus the texel's own log-probability, weighted by a strength) is the prototype's `crf.py`; its guide is colour or colour plus normal map and occlusion.
- **Merge criteria compared** on the eight distinct dumps:

| Criterion | Junk pairs (Stalhrim edge group, Blackguard blue) | Closest real pair | Verdict |
|---|---|---|---|
| Fisher distance (mean term only) | 0.89, 0.91 | 1.34 (Temple trim) | Best margin; blind to spread differences. |
| Bhattacharyya overlap | 0.41, 0.28 | 0.34 (Blackguard cloth/leather), 0.35 (Stalhrim ice/frame) | Ranks a real pair above a junk pair; rejected. |
| Directly estimated misclassification (Hennig 2010) | 23%, 48% | 21% (Chitin straps/plates) | Readable as a percentage; thin margin on Stalhrim. |

- Refitting the groups on texels with expectation maximisation, initialised from the region groups, makes every pair separable (minimum Fisher 1.9), so it removes the merge signal. It cleaned Stalhrim and Temple but added blotches on Blackguard and crack lines on Chitin. The prototype does not use it.
- Parts that differ only by brightness inside one material (Blackguard straps) fall into one group. That split is an intent question for step 3.

- **Bands (one material split into parallel stripes along a gradient), three fixes rejected** on Temple, Stalhrim, Chitin, Blackguard, Bonemold, Nordic and Chitin Heavy at 256 px:
  - Ridgeline ratio between group Gaussians (Ray & Lindsay 2005): 0.76–1.0 for nearly every pair, including Chitin straps against plates (1.0) and Bonemold bone against leather (0.76).
  - Valley depth of the texels projected on the pair's discriminant (after Tantrum et al. 2003): Temple's robe, sash and ring groups all show no valley, because one group is five times larger than the other.
  - Lower brightness weight in the group fit (0.5, 0.2): the Temple ring bands stay; Chitin loses its straps, Stalhrim its fur and Blackguard collapses to one or two groups.
  - A band and a trim line look alike to every test tried; the "same" click of step 3 is the tool for bands.
- **Chart-aware superpixels.** The tuning tool (`tune/` in the prototype) runs its own SLIC: grid seeds per UV chart (one at the centre of a chart too small for the grid), assignment only within a texel's chart, seeds moved to the lowest-gradient texel in 3×3. No patch crosses a chart border, and every chart has a patch of its own, on Blackguard, Temple, Ahzidal and Stalhrim at 256 and 512 px.

## Port of hybrid + merge

| Stage | Where | What |
|---|---|---|
| P1 | `mesh/MaterialRegions` (engine-free) | SLIC inside the coverage mask, region descriptors, Gaussian mixture with ICL (k 2–8; BIC was planned), group Gaussians from texels, misclassification merge. Native tests use synthetic rasters; the dumps cannot be fixtures, so a scratch harness compared the port with the prototype. |
| P2 | `TextureLab` | The material sample grows from 64 px to 256 px and carries the coverage mask rasterised from the mesh UVs. The fit runs once per geometry on the CPU, off the render thread. |
| P3 | Cluster shader | `PSClusters` scores each texel against the group Gaussians (mean and inverse covariance per group in a constant buffer, 8 groups × 8 axes) instead of the weighted nearest centre. |
| P4 | CPU + shader | Built differently from this row: the guided filter runs on the CPU over the 256-texel group probabilities, and the shader adds their log to each texel's own density (one CRF-style pass). The shader scores the padded material maps, so no padding fill is needed. |
| P5 | Studio | The cluster chart shows group means; weights sliders and Best guess go; merge share and smoothing radius take their place. |

## Decisions for the user

| # | Decision | Recommendation |
|---|---|---|
| D1 | Recipe fields of `materialClusters`. | Replace `clusters`, `weights`, `seed`, `iterations` and `guess` with `merge` (share of confused texels, default 0.3) and `smoothing` (texels, default 8). Built with 0.3 and 8 after the tuning on 2026-10-13. Keep `pick`, `areaBias` and `palette`. No migration: old fields become a validation error, and the debug recipe is rewritten. |
| D2 | Where the fit runs. | CPU at 256 px with the coverage mask; labelling on the GPU at full resolution. |
| D3 | Group count control. | BIC picks k; the merge share is the only count control. A fixed `clusters` override is left out until a recipe needs it. |


## Sampling a material an earlier recipe changed

A recipe can apply after another recipe on the same armor. When the earlier recipe has rewritten a material's maps, those maps are the plugin's render targets, not files.

| Fact | Consequence |
|---|---|
| A render target's `Generation` changes only when the pool leases it, not when it is drawn again. | `TextureIdentity` stays the same while the content changes. A grouping cached by texture identity alone goes stale. |
| A stack result carries `contentVersion`, and a view carries `drawStamp`. | The content of a produced map has a version that a cache key can use. |

The readback cache therefore keys by the sampled content: a file texture by `TextureIdentity`, and a produced texture by its producing step's content key and version. Produced textures are not cached yet; they take the content-hash path, which is never stale.

Decided (2026-10-20): a sample of a material that an animated earlier recipe produces is refused. A static earlier recipe stays sampleable.

Open:

1. Merged chains. Two recipes on one slot of one geometry draw as one pass (`MergeSlotChains`), so the version after the earlier recipe never exists as a texture. Sampling it needs that link unmerged and its result held.
2. Which version is sampled today. `MaterialInputs::From` reads the material's current layout, so the answer depends on when sampling runs relative to publishing. Not confirmed; needs a test in game.
