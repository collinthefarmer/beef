// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/PlanJobs.h"
#include "test_support.h"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>

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
  const std::expected<PlanBuildOutput, std::string> thrown =
      std::unexpected(std::string{"the job threw"});
  Check(Is<Orchestration::PlanJobBuilt>(
            PlanJobDoneOf(JobId{1}, closed.job, built)),
        "a built plan for a job the book dropped asks its awaiter again");
  Check(Is<Orchestration::PlanJobBuilt>(
            PlanJobDoneOf(JobId{1}, std::nullopt, thrown)),
        "a failed job the book dropped is not a failure");
}

void JobIdsConvertBothWays() {
  Check(PlanJobIdOf(JobId{7}) == Orchestration::PlanJobId{7},
        "an engine job id keeps its number in the core");
  Check(JobIdOf(Orchestration::PlanJobId{7}) == JobId{7},
        "a core job id keeps its number in the engine");
  Check(JobIdOf(PlanJobIdOf(JobId{0xFFFF'FFFF'FFFF})) ==
            JobId{0xFFFF'FFFF'FFFF},
        "a job id survives the round trip");
}

void DoneJobsBecomeCoreFacts() {
  const std::expected<PlanBuildOutput, std::string> built =
      PlanBuildOutput{RenderPlan{}, {}, {}};
  const std::expected<PlanBuildOutput, std::string> unlowered =
      PlanBuildOutput{std::unexpected(std::string{"no plan"}), {}, {}};
  const std::expected<PlanBuildOutput, std::string> thrown =
      std::unexpected(std::string{"the job threw"});
  const std::optional<OpenPlanJob> open = JobOf(3, 1);
  const Orchestration::PlanJobDone fromBuilt =
      PlanJobDoneOf(JobId{3}, open, built);
  const auto *builtJob = Get<Orchestration::PlanJobBuilt>(fromBuilt);
  Check(builtJob && builtJob->job == Orchestration::PlanJobId{3},
        "a built plan reports its job as built");
  const Orchestration::PlanJobDone fromUnlowered =
      PlanJobDoneOf(JobId{3}, open, unlowered);
  const auto *unloweredJob = Get<Orchestration::PlanJobFailed>(fromUnlowered);
  Check(unloweredJob && unloweredJob->job == Orchestration::PlanJobId{3} &&
            unloweredJob->problem == "no plan",
        "a plan that did not lower fails with its problem");
  const Orchestration::PlanJobDone fromThrown =
      PlanJobDoneOf(JobId{3}, open, thrown);
  const auto *thrownJob = Get<Orchestration::PlanJobFailed>(fromThrown);
  Check(thrownJob && thrownJob->problem == "the job threw",
        "a job that failed in the worker fails with its problem");
  const Orchestration::PlanJobDone fromStale =
      PlanJobDoneOf(JobId{3}, std::nullopt, thrown);
  const auto *staleJob = Get<Orchestration::PlanJobBuilt>(fromStale);
  Check(staleJob && staleJob->job == Orchestration::PlanJobId{3},
        "a job the book dropped reports built so its awaiter asks again");
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
  BookHoldsGraphsUntilClosed();
  JobIdsConvertBothWays();
  DoneJobsBecomeCoreFacts();
  return test::Finish("engine_planjobs");
}
