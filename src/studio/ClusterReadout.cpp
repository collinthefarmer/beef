// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/ClusterReadout.h"

#include <algorithm>

namespace BetterEnchantmentEffects::Studio {
const MaterialAnalysis *
ClustersOf(ClusterMemo &a_memo,
           const std::shared_ptr<const MaterialSample> &a_sample,
           const ClusterSettings &a_settings) {
  if (!a_sample) {
    return nullptr;
  }
  if (a_memo.sample != a_sample || a_memo.settings != a_settings) {
    a_memo = {a_sample, a_settings, ClusterMaterial(*a_sample, a_settings)};
  }
  return &a_memo.analysis;
}

std::vector<const MaterialCluster *>
LargestFirst(const MaterialAnalysis &a_analysis) {
  std::vector<const MaterialCluster *> rows;
  rows.reserve(a_analysis.clusters.size());
  for (const MaterialCluster &cluster : a_analysis.clusters) {
    rows.push_back(&cluster);
  }
  std::ranges::stable_sort(
      rows, [](const MaterialCluster *a_left, const MaterialCluster *a_right) {
        return a_left->share > a_right->share;
      });
  return rows;
}
}
