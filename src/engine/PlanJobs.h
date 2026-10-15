// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Facts.h"
#include "orchestration/RenderPhase.h"
#include "planners/JobQueue.h"
#include "planners/RenderPlanJob.h"
#include "planners/RenderPlanKey.h"
#include "recipe/RecipeGraph.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects {
using PlanQueue = JobQueue<PlanBuildInput, PlanBuildOutput>;

inline constexpr std::size_t kPlanJobCapacity = 16;

using PlanDestination = std::variant<RenderPlanKey, PlanBuildInput>;

struct OpenPlanJob {
  JobId job{};
  PlanDestination destination;
  std::vector<std::shared_ptr<const RecipeGraph>> graphs;
  std::uint32_t actor = 0;
};

struct HandedPlan {
  std::uint32_t actor = 0;
  PlanBuildInput input;
  RenderPlan plan;
};

inline constexpr std::size_t kHandedPlanCapacity = kPlanJobCapacity;

struct HandOverTaken {
  std::vector<HandedPlan> handed;
  std::optional<RenderPlan> plan;
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
[[nodiscard]] std::vector<HandedPlan>
HandedOver(std::vector<HandedPlan> a_handed, HandedPlan a_plan);
[[nodiscard]] bool AwaitsHandOver(std::span<const HandedPlan> a_handed,
                                  std::uint32_t a_actor) noexcept;
[[nodiscard]] HandOverTaken TakeHandedPlan(std::vector<HandedPlan> a_handed,
                                           std::uint32_t a_actor,
                                           const PlanBuildInput &a_current);
[[nodiscard]] Orchestration::PlanJobId PlanJobIdOf(JobId a_job) noexcept;
[[nodiscard]] JobId JobIdOf(Orchestration::PlanJobId a_job) noexcept;
[[nodiscard]] Orchestration::PlanJobDone
PlanJobDoneOf(JobId a_job, const std::optional<OpenPlanJob> &a_open,
              const std::expected<PlanBuildOutput, std::string> &a_result);
}
