// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/PlanJobs.h"

#include <algorithm>
#include <utility>

namespace BetterEnchantmentEffects {
std::optional<JobId> OpenJobFor(const PlanJobBook &a_book,
                                const RenderPlanKey &a_key) {
  const auto found = std::ranges::find(a_book.open, a_key, &OpenPlanJob::key);
  if (found == a_book.open.end()) {
    return std::nullopt;
  }
  return found->job;
}

PlanJobBook Opened(PlanJobBook a_book, OpenPlanJob a_job) {
  a_book.open.push_back(std::move(a_job));
  return a_book;
}

BookAfterClose Closed(PlanJobBook a_book, JobId a_job) {
  const auto found = std::ranges::find(a_book.open, a_job, &OpenPlanJob::job);
  if (found == a_book.open.end()) {
    return {std::move(a_book), std::nullopt};
  }
  std::optional<OpenPlanJob> closed{std::move(*found)};
  a_book.open.erase(found);
  return {std::move(a_book), std::move(closed)};
}

PlanArrival ArrivalOf(
    const std::optional<OpenPlanJob> &a_job,
    const std::expected<PlanBuildOutput, std::string> &a_result) noexcept {
  if (!a_job) {
    return PlanArrival::kStale;
  }
  return a_result && a_result->plan ? PlanArrival::kBuilt
                                    : PlanArrival::kFailed;
}

AwaiterStep StepFor(std::optional<JobId> a_awaited, JobId a_arrived,
                    PlanArrival a_arrival) noexcept {
  if (a_awaited != a_arrived) {
    return AwaiterStep::kIgnore;
  }
  return a_arrival == PlanArrival::kFailed ? AwaiterStep::kFail
                                           : AwaiterStep::kRebuild;
}

Orchestration::PlanJobId PlanJobIdOf(JobId a_job) noexcept {
  return Orchestration::PlanJobId{static_cast<std::uint64_t>(a_job)};
}

JobId JobIdOf(Orchestration::PlanJobId a_job) noexcept {
  return JobId{static_cast<std::uint64_t>(a_job)};
}

Orchestration::PlanJobDone
PlanJobDoneOf(JobId a_job, const std::optional<OpenPlanJob> &a_open,
              const std::expected<PlanBuildOutput, std::string> &a_result) {
  const Orchestration::PlanJobId job = PlanJobIdOf(a_job);
  if (!a_open) {
    return Orchestration::PlanJobBuilt{job};
  }
  if (!a_result) {
    return Orchestration::PlanJobFailed{job, a_result.error()};
  }
  if (!a_result->plan) {
    return Orchestration::PlanJobFailed{job, a_result->plan.error()};
  }
  return Orchestration::PlanJobBuilt{job};
}
}
