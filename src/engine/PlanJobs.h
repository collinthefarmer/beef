// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "engine/JobQueue.h"
#include "orchestration/Facts.h"
#include "orchestration/RenderPhase.h"
#include "planners/RenderPlanJob.h"
#include "planners/RenderPlanKey.h"
#include "recipe/RecipeGraph.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
using PlanQueue = JobQueue<PlanBuildInput, PlanBuildOutput>;

inline constexpr std::size_t kPlanJobCapacity = 16;

struct OpenPlanJob {
  JobId job{};
  RenderPlanKey key;
  std::vector<std::shared_ptr<const RecipeGraph>> graphs;
  std::uint32_t actor = 0;
};

struct PlanJobBook {
  std::vector<OpenPlanJob> open;
};

struct BookAfterClose {
  PlanJobBook book;
  std::optional<OpenPlanJob> job;
};

[[nodiscard]] std::size_t FreePlanSlots(std::size_t a_unfinished) noexcept;
[[nodiscard]] std::optional<JobId> OpenJobFor(const PlanJobBook &a_book,
                                              const RenderPlanKey &a_key);
[[nodiscard]] PlanJobBook Opened(PlanJobBook a_book, OpenPlanJob a_job);
[[nodiscard]] BookAfterClose Closed(PlanJobBook a_book, JobId a_job);
[[nodiscard]] Orchestration::PlanJobId PlanJobIdOf(JobId a_job) noexcept;
[[nodiscard]] JobId JobIdOf(Orchestration::PlanJobId a_job) noexcept;
[[nodiscard]] Orchestration::PlanJobDone
PlanJobDoneOf(JobId a_job, const std::optional<OpenPlanJob> &a_open,
              const std::expected<PlanBuildOutput, std::string> &a_result);
}
