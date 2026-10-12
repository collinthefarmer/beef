// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/MaterialClusters.h"
#include "recipe/Recipe.h"

#include <memory>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct ClusterMemo {
  std::shared_ptr<const MaterialSample> sample;
  ClusterSettings settings;
  MaterialAnalysis analysis;
};

[[nodiscard]] const MaterialAnalysis *
ClustersOf(ClusterMemo &a_memo,
           const std::shared_ptr<const MaterialSample> &a_sample,
           const ClusterSettings &a_settings);
[[nodiscard]] std::vector<const MaterialCluster *>
LargestFirst(const MaterialAnalysis &a_analysis);
}
