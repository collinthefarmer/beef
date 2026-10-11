// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/sim/Fixtures.h"
#include "test_support.h"

#include <string_view>
#include <utility>
#include <vector>

using namespace sim;
using test::Check;

namespace {
constexpr ActorId kActor{20};
constexpr ActorId kNewcomer{22};
constexpr ResolutionLevel kFull = ResolutionLevel::kFull;

const StepRow &RowNamed(std::string_view a_name) {
  for (const StepRow &row : StepRows()) {
    if (row.name == a_name) {
      return row;
    }
  }
  return StepRows().front();
}

World Settled() {
  World world;
  world.actors[kActor] =
      TrackedOf(RenderIn(ShownAt(kFull)), InView(0.5f, 100.0f));
  return WithEstimate(std::move(world));
}

std::vector<Violation> CheckedStep(std::string_view a_step,
                                   const World &a_before, World a_after,
                                   std::vector<Command> a_commands) {
  const TickOutput output{std::move(a_after), std::move(a_commands), {}, {}};
  return CheckStepOutput(RowNamed(a_step), a_before, output, 1);
}

void RowsFollowTheStepOrder() {
  const std::array<std::string_view, 5> order{
      "RecordSyncResults", "RecordRenderResults", "RunBudget", "RunSync",
      "RunLifecycle"};
  bool inOrder = true;
  for (std::size_t i = 0; i < order.size(); ++i) {
    inOrder = inOrder && StepRows().at(i).name == order.at(i);
  }
  Check(inOrder, "the rows list the steps in Tick's order");
}

void CommandsOutsideTheRowFire() {
  const World world = Settled();
  Check(Fired(CheckedStep(
                  "RunBudget", world, world,
                  {Install{kActor, {}, {}, InstallKind::kInstall, {}, 0}}),
              Rule::kStepCommandOutsideRow),
        "the budget step issuing an install fires");
  Check(
      Fired(CheckedStep("RecordSyncResults", world, world, {TickActor{kActor}}),
            Rule::kStepCommandOutsideRow),
      "the sync results step issuing any command fires");
  Check(Fired(CheckedStep("RunLifecycle", world, world, {Observe{kActor}}),
              Rule::kStepCommandOutsideRow),
        "the lifecycle step issuing an observe fires");
  Check(CheckedStep("RunBudget", world, world,
                    {ReleaseIntermediates{kActor, ReleaseReason::kBudget, 1},
                     Observe{kActor}})
            .empty(),
        "the budget step issuing its own kinds passes");
}

void WritesOutsideTheRowFire() {
  const World before = Settled();
  World syncLogged = before;
  syncLogged.syncLog.push_back({});
  Check(Fired(CheckedStep("RunBudget", before, syncLogged, {}),
              Rule::kStepWroteOutsideRow),
        "the budget step writing the sync log fires");
  World sighted = before;
  sighted.actors[kActor].sight = OutOfView(900.0f);
  Check(Fired(CheckedStep("RunLifecycle", before, sighted, {}),
              Rule::kStepWroteOutsideRow),
        "the lifecycle step writing a sighting fires");
  Check(CheckedStep("RecordRenderResults", before, sighted, {}).empty(),
        "the render results step writing a sighting passes");
  World erased = before;
  erased.actors[kActor].render.reset();
  Check(CheckedStep("RunSync", before, erased, {}).empty(),
        "the sync step erasing a render it retires passes");
  Check(Fired(CheckedStep("RunLifecycle", before, erased, {}),
              Rule::kStepWroteOutsideRow),
        "the lifecycle step erasing a render fires");
  Check(CheckedStep("RecordSyncResults", before, erased, {}).empty(),
        "the sync results step erasing a render passes");
  World costed = before;
  costed.actors[kActor].render->levelCosts = HalvingCosts(kFullBytes / 2);
  Check(Fired(CheckedStep("RecordSyncResults", before, costed, {}),
              Rule::kStepWroteOutsideRow),
        "the sync results step writing level costs fires");
  World inserted = before;
  inserted.actors[kNewcomer] = TrackedActor{};
  Check(Fired(CheckedStep("RunBudget", before, inserted, {}),
              Rule::kStepWroteOutsideRow),
        "the budget step adding an actor fires");
  Check(CheckedStep("RunSync", before, inserted, {}).empty(),
        "the sync step adding an actor passes");
  World synced = before;
  synced.actors[kActor].syncState = Actors::ActorState{};
  Check(CheckedStep("RunSync", before, synced, {}).empty(),
        "the sync step writing a sync state passes");
  World radius = before;
  radius.radius = BudgetRadius{500.0f, 1, false};
  Check(Fired(CheckedStep("RunLifecycle", before, radius, {}),
              Rule::kStepWroteOutsideRow),
        "the lifecycle step writing the radius fires");
  World timers = before;
  timers.timers.lastSlowCheck = 5;
  Check(Fired(CheckedStep("RunBudget", before, timers, {}),
              Rule::kStepWroteOutsideRow),
        "the budget step writing the timers fires");
  World waiting = before;
  waiting.waitingInstalls.push_back({});
  Check(Fired(CheckedStep("RunLifecycle", before, waiting, {}),
              Rule::kStepWroteOutsideRow),
        "the lifecycle step writing the waiting installs fires");
}

void APhaseChangeIsAlwaysAllowed() {
  const World before = Settled();
  World released = before;
  released.actors[kActor].render->phase = ShownAt(kFull, Freed{});
  for (const StepRow &row : StepRows()) {
    const TickOutput output{released, {}, {}, {}};
    Check(CheckStepOutput(row, before, output, 1).empty(),
          "a step that changes only a render phase passes");
  }
}

void StubStepsKeepTheirRows() {
  World world = Settled();
  world.actors[kNewcomer] = TrackedActor{};
  TickFacts facts;
  facts.sightings[kActor] = InView(0.5f, 100.0f);
  facts.sightings[kNewcomer] = OutOfView(300.0f);
  const StepsOutcome steps = RunStepsChecked(world, facts, 1);
  Check(steps.violations.empty(), "every step keeps to its row");
  const TickOutput tick = Tick(world, facts);
  Check(CheckTickMatchesSteps(tick, steps.output, 1).empty(),
        "Tick equals its steps run in order");
}

void TickDifferingFromStepsFires() {
  const World world = Settled();
  const TickOutput steps{world, {}, {}, {}};
  const TickOutput extra{world, {Observe{kActor}}, {}, {}};
  Check(Fired(CheckTickMatchesSteps(extra, steps, 1),
              Rule::kTickDiffersFromSteps),
        "a tick with a command its steps did not issue fires");
  World moved = world;
  moved.actors[kActor].render->phase = ShownAt(ResolutionLevel::kHalf);
  const TickOutput other{moved, {}, {}, {}};
  Check(Fired(CheckTickMatchesSteps(other, steps, 1),
              Rule::kTickDiffersFromSteps),
        "a tick whose world differs from its steps fires");
}
}

int main() {
  RowsFollowTheStepOrder();
  CommandsOutsideTheRowFire();
  WritesOutsideTheRowFire();
  APhaseChangeIsAlwaysAllowed();
  StubStepsKeepTheirRows();
  TickDifferingFromStepsFires();
  return test::Finish("orchestration_contract");
}
