// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/MaterialGrouper.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::uint64_t kWordPrime = 0x100000001b3ull;

[[nodiscard]] std::uint64_t MixWord(std::uint64_t a_hash,
                                    std::uint64_t a_word) noexcept {
  return (a_hash ^ a_word) * kWordPrime;
}

[[nodiscard]] std::vector<std::uint16_t> ChartsOf(const GroupingJob &a_job) {
  const std::uint32_t side = a_job.sample.width;
  std::vector<std::uint16_t> charts;
  if (a_job.mesh) {
    charts = ChartRasterOf(*a_job.mesh, a_job.analysis, side);
  }
  const auto covered = static_cast<std::size_t>(std::ranges::count_if(
      charts, [](std::uint16_t a_chart) { return a_chart != kNoChart; }));
  if (covered < kMinCoveredTexels) {
    charts.assign(static_cast<std::size_t>(side) * side, 0);
  }
  return charts;
}
}

std::uint64_t GroupingKeyOf(const MaterialSample &a_sample,
                            std::uint64_t a_meshHash,
                            const ClusterSettings &a_settings) noexcept {
  std::uint64_t hash = kHashBasis;
  for (const MaterialTexel &texel : a_sample.texels) {
    const float values[]{texel.roughness,   texel.metallic,  texel.occlusion,
                         texel.reflectance, texel.luma,      texel.diffuse.x,
                         texel.diffuse.y,   texel.diffuse.z, texel.detail};
    for (const float value : values) {
      hash = MixWord(hash, std::bit_cast<std::uint32_t>(value));
    }
  }
  hash = MixWord(hash, a_sample.width);
  hash = MixWord(hash, a_meshHash);
  hash = MixWord(hash, std::bit_cast<std::uint32_t>(a_settings.merge));
  return MixWord(hash, a_settings.smoothing);
}

GroupingResult GroupJob(const GroupingJob &a_job) {
  auto grouping = GroupMaterial(a_job.sample, ChartsOf(a_job), a_job.settings);
  if (!grouping) {
    return std::unexpected(grouping.error());
  }
  return std::make_shared<const MaterialGrouping>(std::move(*grouping));
}

MaterialGrouper::MaterialGrouper(Work a_work, std::size_t a_capacity,
                                 std::size_t a_cacheSize)
    : queue_(a_work, a_capacity),
      cacheSize_(std::max<std::size_t>(a_cacheSize, 1)) {}

MaterialGrouper &MaterialGrouper::GetSingleton() {
  static MaterialGrouper grouper(&GroupJob, kGroupingJobCapacity,
                                 kGroupingCacheSize);
  return grouper;
}

std::optional<GroupingResult>
MaterialGrouper::Poll(const GroupingTicket &a_ticket) {
  const std::scoped_lock held{lock_};
  CollectLocked();
  const std::uint64_t key = a_ticket.job.key;
  if (const auto found = done_.find(key); found != done_.end()) {
    return found->second;
  }
  if (IsPendingLocked(key)) {
    return std::nullopt;
  }
  auto job = queue_.Submit(a_ticket.job);
  if (job) {
    pending_.emplace(*job, key);
    return std::nullopt;
  }
  if (job.error() == JobRefusal::kFull) {
    return std::nullopt;
  }
  return GroupingResult{std::unexpected("the grouping worker is unavailable")};
}

bool MaterialGrouper::IsPendingLocked(std::uint64_t a_key) const {
  return std::ranges::any_of(pending_, [a_key](const auto &a_entry) {
    return a_entry.second == a_key;
  });
}

void MaterialGrouper::CollectLocked() {
  for (JobDone<GroupingResult> &done : queue_.TakeDone()) {
    const auto found = pending_.find(done.id);
    if (found == pending_.end()) {
      continue;
    }
    const std::uint64_t key = found->second;
    pending_.erase(found);
    RememberLocked(key,
                   done.result
                       ? std::move(*done.result)
                       : GroupingResult{std::unexpected(done.result.error())});
  }
}

void MaterialGrouper::RememberLocked(std::uint64_t a_key,
                                     GroupingResult a_result) {
  if (!done_.contains(a_key)) {
    order_.push_back(a_key);
  }
  done_.insert_or_assign(a_key, std::move(a_result));
  while (order_.size() > cacheSize_) {
    done_.erase(order_.front());
    order_.pop_front();
  }
}
}
