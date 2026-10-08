// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/PlanJobs.h"
#include "test_support.h"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
RenderPlanKey KeyNamed(std::size_t a_output) {
  StackKey stack;
  stack.output = a_output;
  stack.material = "material";
  stack.meshHash = "mesh:1";
  return RenderPlanKey{{stack}, {}};
}

OpenPlanJob JobOf(std::uint64_t a_job, std::size_t a_output) {
  return {JobId{a_job}, KeyNamed(a_output), {}, 0x14};
}

void OneJobPerKey() {
  PlanJobBook book;
  Check(!OpenJobFor(book, KeyNamed(1)).has_value(),
        "an empty book has no job for a key");
  book = Opened(std::move(book), JobOf(1, 1));
  book = Opened(std::move(book), JobOf(2, 2));
  Check(OpenJobFor(book, KeyNamed(1)) == JobId{1},
        "a second actor with the same key shares the open job");
  Check(OpenJobFor(book, KeyNamed(2)) == JobId{2},
        "another key has its own job");
  Check(!OpenJobFor(book, KeyNamed(3)).has_value(),
        "an unknown key has no job");
}

void ClosingFreesTheKey() {
  PlanJobBook book = Opened({}, JobOf(1, 1));
  BookAfterClose closed = Closed(std::move(book), JobId{1});
  Check(closed.job.has_value() && closed.job->key == KeyNamed(1),
        "closing an open job returns its key");
  Check(!OpenJobFor(closed.book, KeyNamed(1)).has_value(),
        "a closed job no longer takes awaiters");
  BookAfterClose again = Closed(std::move(closed.book), JobId{1});
  Check(!again.job.has_value(), "a job closes once");
}

void AResetBookMakesResultsStale() {
  PlanJobBook book = Opened({}, JobOf(1, 1));
  book = {};
  const BookAfterClose closed = Closed(std::move(book), JobId{1});
  Check(!closed.job.has_value(), "a reset book does not know the old job");
  const std::expected<PlanBuildOutput, std::string> built =
      PlanBuildOutput{RenderPlan{}, {}, {}};
  const std::expected<PlanBuildOutput, std::string> unlowered =
      PlanBuildOutput{std::unexpected(std::string{"no plan"}), {}, {}};
  const std::expected<PlanBuildOutput, std::string> thrown =
      std::unexpected(std::string{"the job threw"});
  const std::optional<OpenPlanJob> open = JobOf(1, 1);
  Check(ArrivalOf(closed.job, built) == PlanArrival::kStale,
        "a built plan for a job the book dropped is stale");
  Check(ArrivalOf(std::nullopt, thrown) == PlanArrival::kStale,
        "a failed job the book dropped is stale, not a failure");
  Check(ArrivalOf(open, built) == PlanArrival::kBuilt, "an open job builds");
  Check(ArrivalOf(open, unlowered) == PlanArrival::kFailed,
        "an open job can fail to lower");
  Check(ArrivalOf(open, thrown) == PlanArrival::kFailed,
        "an open job can fail in the worker");
}

void AwaitersFollowTheirJob() {
  Check(StepFor(std::nullopt, JobId{1}, PlanArrival::kBuilt) ==
            AwaiterStep::kIgnore,
        "an actor that awaits nothing ignores a result");
  Check(StepFor(JobId{2}, JobId{1}, PlanArrival::kBuilt) ==
            AwaiterStep::kIgnore,
        "an actor that reinstalled onto another job ignores the old one");
  Check(StepFor(JobId{1}, JobId{1}, PlanArrival::kBuilt) ==
            AwaiterStep::kRebuild,
        "an actor rebuilds from a built plan");
  Check(StepFor(JobId{1}, JobId{1}, PlanArrival::kStale) ==
            AwaiterStep::kRebuild,
        "an actor whose job went stale asks again");
  Check(StepFor(JobId{1}, JobId{1}, PlanArrival::kFailed) == AwaiterStep::kFail,
        "an actor whose job failed fails its outputs");
}

void BookHoldsGraphsUntilClosed() {
  const std::shared_ptr<const RecipeGraph> graph =
      std::make_shared<const RecipeGraph>(RecipeGraph::Compile(Recipe{}));
  const std::weak_ptr<const RecipeGraph> watched = graph;
  PlanJobBook book;
  {
    OpenPlanJob job = JobOf(1, 1);
    job.graphs.push_back(graph);
    book = Opened(std::move(book), std::move(job));
  }
  Equal(watched.use_count(), long{2}, "the open job holds the graph");
  BookAfterClose closed = Closed(std::move(book), JobId{1});
  Check(closed.job && closed.job->graphs.size() == 1,
        "the closed job hands its graphs to the cache");
}
}

int main() {
  OneJobPerKey();
  ClosingFreesTheKey();
  AResetBookMakesResultsStale();
  AwaitersFollowTheirJob();
  BookHoldsGraphsUntilClosed();
  return test::Finish("engine_planjobs");
}
