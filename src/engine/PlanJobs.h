// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "engine/JobQueue.h"
#include "planners/RenderPlanJob.h"
#include "planners/RenderPlanKey.h"
#include "recipe/RecipeGraph.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace BetterEnchantmentEffects {
using PlanQueue = JobQueue<PlanBuildInput, PlanBuildOutput>;

inline constexpr std::size_t kPlanJobCapacity = 16;

struct PlanAwaited {
  JobId job{};
};

struct OpenPlanJob {
  JobId job{};
  RenderPlanKey key;
  std::vector<std::shared_ptr<const RecipeGraph>> graphs;
  std::uint32_t actor = 0;
};

struct PlanJobBook {
  std::vector<OpenPlanJob> open;
};

struct ClosedPlanJob {
  PlanJobBook book;
  std::optional<OpenPlanJob> job;
};

enum class PlanArrival { kBuilt, kFailed, kStale };

enum class AwaiterStep { kIgnore, kRebuild, kFail };

[[nodiscard]] std::optional<JobId> OpenJobFor(const PlanJobBook &a_book,
                                              const RenderPlanKey &a_key);
[[nodiscard]] PlanJobBook Opened(PlanJobBook a_book, OpenPlanJob a_job);
[[nodiscard]] ClosedPlanJob Closed(PlanJobBook a_book, JobId a_job);
[[nodiscard]] PlanArrival ArrivalOf(bool a_open, bool a_built) noexcept;
[[nodiscard]] AwaiterStep StepFor(std::optional<JobId> a_awaited,
                                  JobId a_arrived,
                                  PlanArrival a_arrival) noexcept;
}
