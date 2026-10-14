// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/MaterialGrouper.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
[[nodiscard]] std::uint64_t Mix(std::uint64_t a_hash,
                                std::uint64_t a_value) noexcept {
  std::uint8_t bytes[sizeof(a_value)];
  std::memcpy(bytes, &a_value, sizeof(a_value));
  return HashBytes(bytes, a_hash);
}

[[nodiscard]] std::vector<std::uint16_t> ChartsOf(const GroupingJob &a_job) {
  const std::uint32_t side = a_job.sample.width;
  std::vector<std::uint16_t> charts;
  if (a_job.mesh) {
    charts = ChartRasterOf(*a_job.mesh, a_job.analysis, side);
  }
  const bool covered = std::ranges::any_of(
      charts, [](std::uint16_t a_chart) { return a_chart != kNoChart; });
  if (!covered) {
    charts.assign(static_cast<std::size_t>(side) * side, 0);
  }
  return charts;
}
}

std::uint64_t GroupingKeyOf(const MaterialSample &a_sample,
                            std::uint64_t a_meshHash,
                            const ClusterSettings &a_settings) noexcept {
  const auto bytes = std::as_bytes(std::span(a_sample.texels));
  std::uint64_t hash = HashBytes(std::span(
      reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size()));
  hash = Mix(hash, a_sample.width);
  hash = Mix(hash, a_meshHash);
  hash = Mix(hash, std::bit_cast<std::uint32_t>(a_settings.merge));
  return Mix(hash, a_settings.smoothing);
}

MaterialGrouper &MaterialGrouper::GetSingleton() {
  static MaterialGrouper grouper;
  return grouper;
}

MaterialGrouper::MaterialGrouper()
    : queue_(&MaterialGrouper::Work, kGroupingJobCapacity) {}

std::optional<GroupingResult> MaterialGrouper::Find(std::uint64_t a_key) {
  const std::scoped_lock held{lock_};
  CollectLocked();
  const auto found = done_.find(a_key);
  if (found == done_.end()) {
    return std::nullopt;
  }
  return found->second;
}

bool MaterialGrouper::IsPending(std::uint64_t a_key) {
  const std::scoped_lock held{lock_};
  CollectLocked();
  return std::ranges::any_of(pending_, [a_key](const auto &a_entry) {
    return a_entry.second == a_key;
  });
}

bool MaterialGrouper::Request(GroupingJob a_job) {
  const std::scoped_lock held{lock_};
  CollectLocked();
  const std::uint64_t key = a_job.key;
  if (done_.contains(key) ||
      std::ranges::any_of(pending_, [key](const auto &a_entry) {
        return a_entry.second == key;
      })) {
    return true;
  }
  auto job = queue_.Submit(std::move(a_job));
  if (!job) {
    return false;
  }
  pending_.emplace(*job, key);
  return true;
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
  while (order_.size() > kGroupingCacheSize) {
    done_.erase(order_.front());
    order_.pop_front();
  }
}

GroupingResult MaterialGrouper::Work(const GroupingJob &a_job) {
  auto grouping = GroupMaterial(a_job.sample, ChartsOf(a_job), a_job.settings);
  if (!grouping) {
    return std::unexpected(grouping.error());
  }
  return std::make_shared<const MaterialGrouping>(std::move(*grouping));
}
}
