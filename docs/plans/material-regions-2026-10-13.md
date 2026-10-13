Status: active (2026-10-13) on branch `pure-tick`. The user approved the route on 2026-10-13. Step 0 (material dump) is done. Steps 1 and 2 are prototyped in Python over nine dumps; the user judged the hybrid labelling good on 2026-10-13. Next: the merge step, then the engine-free port.

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
- Refitting the groups on texels with expectation maximisation, initialised from the region groups, makes every pair separable (minimum Fisher 1.9), so it removes the merge signal. It cleaned Stalhrim and Temple but added blotches on Blackguard and crack lines on Chitin. The prototype does not use it.
- Parts that differ only by brightness inside one material (Blackguard straps) fall into one group. That split is an intent question for step 3.

## Decisions for the user

None yet. Step 4's migration choice comes after the merge step.
