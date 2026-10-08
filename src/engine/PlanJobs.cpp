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

ClosedPlanJob Closed(PlanJobBook a_book, JobId a_job) {
  const auto found = std::ranges::find(a_book.open, a_job, &OpenPlanJob::job);
  if (found == a_book.open.end()) {
    return {std::move(a_book), std::nullopt};
  }
  std::optional<OpenPlanJob> closed{std::move(*found)};
  a_book.open.erase(found);
  return {std::move(a_book), std::move(closed)};
}

PlanArrival ArrivalOf(bool a_open, bool a_built) noexcept {
  if (!a_open) {
    return PlanArrival::kStale;
  }
  return a_built ? PlanArrival::kBuilt : PlanArrival::kFailed;
}

AwaiterStep StepFor(std::optional<JobId> a_awaited, JobId a_arrived,
                    PlanArrival a_arrival) noexcept {
  if (a_awaited != a_arrived) {
    return AwaiterStep::kIgnore;
  }
  return a_arrival == PlanArrival::kFailed ? AwaiterStep::kFail
                                           : AwaiterStep::kRebuild;
}
}
