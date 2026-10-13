Status: active (2026-10-13) on branch `pure-tick`. The user approved the route on 2026-10-13. Step 0 (material dump) is next; nothing else is built.

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
| 4 | **Mask.** A per-material region-id bake; a mask is a lookup from region id to selected. | The per-pixel cluster shader. |

Steps 1 and 2 are built and judged as a standalone prototype against dumps from the user's armor (the bonemold cuirass first) before anything in the render path changes. Step 4 changes the term and source shape; existing `materialClusters` recipes either migrate or keep the k-means path alongside.

## Decisions for the user

None yet. Step 4's migration choice comes after the prototype comparison.
