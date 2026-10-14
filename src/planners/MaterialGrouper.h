// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/Islands.h"
#include "mesh/MaterialClusters.h"
#include "mesh/MaterialRegions.h"
#include "mesh/Mesh.h"
#include "planners/JobQueue.h"
#include "recipe/Recipe.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <expected>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kGroupingJobCapacity = 8;
inline constexpr std::size_t kGroupingCacheSize = 32;

struct GroupingJob {
  std::uint64_t key = 0;
  MaterialSample sample;
  std::shared_ptr<const MeshData> mesh;
  MeshAnalysis analysis;
  ClusterSettings settings;
};

using GroupingResult =
    std::expected<std::shared_ptr<const MaterialGrouping>, std::string>;

struct GroupingTicket {
  GroupingJob job;
};

[[nodiscard]] std::uint64_t
GroupingKeyOf(const MaterialSample &a_sample, std::uint64_t a_meshHash,
              const ClusterSettings &a_settings) noexcept;
[[nodiscard]] GroupingResult GroupJob(const GroupingJob &a_job);

class MaterialGrouper {
public:
  using Work = GroupingResult (*)(const GroupingJob &);

  MaterialGrouper(Work a_work, std::size_t a_capacity, std::size_t a_cacheSize);
  [[nodiscard]] static MaterialGrouper &GetSingleton();
  [[nodiscard]] std::optional<GroupingResult>
  Poll(const GroupingTicket &a_ticket);

private:
  void CollectLocked();
  void RememberLocked(std::uint64_t a_key, GroupingResult a_result);
  [[nodiscard]] bool IsPendingLocked(std::uint64_t a_key) const;

  std::mutex lock_;
  JobQueue<GroupingJob, GroupingResult> queue_;
  std::size_t cacheSize_;
  std::map<JobId, std::uint64_t> pending_;
  std::map<std::uint64_t, GroupingResult> done_;
  std::deque<std::uint64_t> order_;
};
}
