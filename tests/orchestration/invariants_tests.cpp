// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/sim/Fixtures.h"
#include "test_support.h"

#include <utility>
#include <vector>

using namespace sim;
using test::Check;

namespace {
constexpr ActorId kActor{20};
constexpr ActorId kOther{21};
constexpr ResolutionLevel kFull = ResolutionLevel::kFull;
constexpr ResolutionLevel kHalf = ResolutionLevel::kHalf;
constexpr Millis kNow = 10000;

World WorldOf(std::vector<std::pair<ActorId, RenderPhase>> a_phases) {
  World world;
  for (auto &[id, phase] : a_phases) {
    world.actors[id] = RecordOf(RenderIn(std::move(phase)));
  }
  return WithEstimate(std::move(world));
}

TickFacts FactsAt(Millis a_now) {
  TickFacts facts;
  facts.now = a_now;
  return facts;
}

std::vector<Violation> Checked(const World &a_before, const TickFacts &a_facts,
                               World a_after, std::vector<Command> a_commands) {
  const TickOutput output{std::move(a_after), std::move(a_commands), {}, {}};
  return CheckTick(TickView{1, a_before, a_facts, output});
}

std::vector<Violation> Checked(const World &a_world,
                               std::vector<Command> a_commands) {
  return Checked(a_world, FactsAt(kNow), a_world, std::move(a_commands));
}

Live HeldSince(Millis a_since) {
  return LiveAt(kFull, IntermediatesHeld{Hold{a_since}});
}

Resizing Running(ResolutionLevel a_from, ResolutionLevel a_to) {
  return Resizing{a_from, a_to, IntermediateHolding::kHeld,
                  ResizeRunning{UnpublishedDraw{4, 0, 1, 0}}, std::nullopt};
}

Resizing Queued(ResolutionLevel a_from, ResolutionLevel a_to) {
  return Resizing{a_from, a_to, IntermediateHolding::kHeld, ResizeQueued{},
                  std::nullopt};
}

void ASettledWorldPasses() {
  const World world =
      WorldOf({{kActor, LiveAt(kFull)}, {kOther, Running(kFull, kHalf)}});
  const std::vector<Violation> found =
      Checked(world, {TickActor{kActor, true}, SwapResize{kOther},
                      DrawUnpublished{{kOther}, 4000}});
  Check(found.empty(), "a world whose commands fit its phases passes");
}

void ReleaseOutsideLive() {
  const World held = WorldOf({{kActor, LiveAt(kFull)}});
  const ReleaseIntermediates release{kActor, ReleaseReason::kBudget, 1};
  Check(Fired(Checked(held, {release}), Rule::kReleaseOutsideLive),
        "a release that leaves the actor holding fires");
  const World drawing =
      WorldOf({{kActor, FirstDrawing{kFull, UnpublishedDraw{4, 0, 1, 0},
                                     std::nullopt}}});
  Check(Fired(Checked(drawing, {release}), Rule::kReleaseOutsideLive),
        "a release of a first-drawing actor fires");
  const World released =
      WorldOf({{kActor, LiveAt(kFull, IntermediatesReleased{})}});
  Check(!Fired(Checked(held, FactsAt(kNow), released, {release}),
               Rule::kReleaseOutsideLive),
        "a release that leaves the actor live and released passes");
}

void OutOfViewReleaseWaitsForItsHold() {
  const World released =
      WorldOf({{kActor, LiveAt(kFull, IntermediatesReleased{})}});
  const ReleaseIntermediates release{kActor, ReleaseReason::kOutOfView, 1};
  const World early = WorldOf({{kActor, HeldSince(kNow - 100)}});
  Check(Fired(Checked(early, FactsAt(kNow), released, {release}),
              Rule::kOutOfViewReleaseEarly),
        "an out-of-view release before the hold delay fires");
  const World swapped = WorldOf({{kActor, Running(kHalf, kFull)}});
  Check(Fired(Checked(swapped, FactsAt(kNow), released, {release}),
              Rule::kOutOfViewReleaseEarly),
        "an out-of-view release in the tick a swap lands fires");
  const World due = WorldOf({{kActor, HeldSince(kNow - 6000)}});
  Check(!Fired(Checked(due, FactsAt(kNow), released, {release}),
               Rule::kOutOfViewReleaseEarly),
        "an out-of-view release after the hold delay passes");
}

void OneResizeInFlight() {
  const World two = WorldOf(
      {{kActor, Running(kFull, kHalf)}, {kOther, Running(kFull, kHalf)}});
  Check(Fired(Checked(two, {}), Rule::kTwoResizesInFlight),
        "two running resizes fire");
  const World starting = WorldOf(
      {{kActor, Running(kFull, kHalf)}, {kOther, Queued(kFull, kHalf)}});
  Check(Fired(Checked(starting, {StartResize{kOther, kHalf}}),
              Rule::kTwoResizesInFlight),
        "a resize started beside a running one fires");
  Check(!Fired(Checked(starting, {}), Rule::kTwoResizesInFlight),
        "a queued resize beside a running one passes");
}

void NoResizeToTheSameLevel() {
  const World same = WorldOf({{kActor, Queued(kHalf, kHalf)}});
  Check(Fired(Checked(same, {}), Rule::kResizeToSameLevel),
        "a resize from a level to itself fires");
}

void NoIgnoredCreatingEvent() {
  const World live = WorldOf({{kActor, LiveAt(kFull)}});
  TickFacts facts = FactsAt(kNow);
  facts.results.push_back(
      ActorEvent{kActor, Installed{kFull, 4, HalvingCosts(kFullBytes), 1}});
  Check(Fired(Checked(live, facts, live, {}), Rule::kCreatingEventIgnored),
        "an install result for a live actor fires");
  const World awaiting = WorldOf({{kActor, AwaitingPlan{PlanJobId{1}}}});
  const World drawing =
      WorldOf({{kActor, FirstDrawing{kFull, UnpublishedDraw{4, 0, 1, 0},
                                     std::nullopt}}});
  Check(!Fired(Checked(awaiting, facts, drawing, {}),
               Rule::kCreatingEventIgnored),
        "an install result for an awaiting actor passes");
  Check(
      Fired(Checked(World{}, facts, World{}, {}), Rule::kCreatingEventIgnored),
      "an install result that creates no render fires");
  TickFacts reinstall = facts;
  reinstall.results.insert(reinstall.results.begin(), Retired{kActor});
  Check(!Fired(Checked(live, reinstall, drawing, {}),
               Rule::kCreatingEventIgnored),
        "an install result after a retire of the same actor passes");
}

void CommandsAgreeWithPhases() {
  const World live = WorldOf({{kActor, LiveAt(kFull)}});
  Check(Fired(Checked(live, {SwapResize{kActor}}),
              Rule::kCommandDisagreesWithPhase),
        "a swap for a live actor fires");
  Check(Fired(Checked(live, {StartResize{kActor, kHalf}}),
              Rule::kCommandDisagreesWithPhase),
        "a resize start for a live actor fires");
  Check(Fired(Checked(live, {SetAppliedLevel{kActor, kHalf}}),
              Rule::kCommandDisagreesWithPhase),
        "an applied level the phase does not have fires");
  Check(Fired(Checked(live, {BuildAwaitedRender{kActor, {}}}),
              Rule::kCommandDisagreesWithPhase),
        "an awaited render built for a live actor fires");
  Check(Fired(Checked(live, {DrawUnpublished{{kActor}, 4000}}),
              Rule::kCommandDisagreesWithPhase),
        "an unpublished draw for a live actor fires");
  Check(Fired(Checked(live,
                      {Install{kActor, {}, {}, InstallKind::kInstall, {}, 0}}),
              Rule::kCommandDisagreesWithPhase),
        "a fresh install over a render fires");
  Check(Fired(Checked(World{}, {TickActor{kActor}}),
              Rule::kCommandDisagreesWithPhase),
        "a tick of an actor without a render fires");
  const World running = WorldOf({{kActor, Running(kFull, kHalf)}});
  Check(Fired(Checked(running, {AbandonResize{kActor}}),
              Rule::kCommandDisagreesWithPhase),
        "an abandoned resize that still runs fires");
}

void DrawComesLast() {
  const World drawing =
      WorldOf({{kActor, FirstDrawing{kFull, UnpublishedDraw{4, 0, 1, 0},
                                     std::nullopt}}});
  Check(Fired(Checked(drawing,
                      {DrawUnpublished{{kActor}, 4000}, TickActor{kActor}}),
              Rule::kDrawUnpublishedNotLast),
        "a command after the unpublished draw fires");
  Check(Fired(Checked(drawing, {DrawUnpublished{{}, 4000},
                                DrawUnpublished{{kActor}, 4000}}),
              Rule::kDrawUnpublishedNotLast),
        "two unpublished draws fire");
}

void EstimateEqualsHeldCosts() {
  World world = WorldOf({{kActor, LiveAt(kFull)},
                         {kOther, LiveAt(kHalf, IntermediatesReleased{})}});
  const PerLevel<std::optional<LevelCost>> costs = HalvingCosts(kFullBytes);
  Check(world.budget.planned ==
            costs.At(kFull)->withIntermediates + costs.At(kHalf)->whenReleased,
        "the estimate sums each actor's cost at its level and holding");
  Check(!Fired(Checked(world, {}), Rule::kEstimateDiffers),
        "an estimate equal to the held costs passes");
  world.budget.planned -= 1;
  Check(Fired(Checked(world, {}), Rule::kEstimateDiffers),
        "an estimate below the held costs fires");
  World retiring = WorldOf({{kActor, LiveAt(kFull)}});
  retiring.budget.planned = 0;
  Check(!Fired(Checked(retiring, {Retire{kActor}}), Rule::kEstimateDiffers),
        "a retiring actor may already be released");

  const World running = WorldOf({{kActor, Running(kFull, kHalf)}});
  Check(running.budget.planned == costs.At(kFull)->withIntermediates +
                                      costs.At(kHalf)->withIntermediates,
        "a running resize adds its target's full cost");
  World starting = WorldOf({{kActor, Queued(kFull, kHalf)}});
  starting.budget.planned = running.budget.planned;
  Check(!Fired(Checked(starting, {StartResize{kActor, kHalf}}),
               Rule::kEstimateDiffers),
        "a resize started this tick counts its target");
  Check(Fired(Checked(starting, {}), Rule::kEstimateDiffers),
        "a queued resize not started counts no target");
}

void FailedLevelNotAskedAgain() {
  Resizing refused = Queued(kFull, kHalf);
  refused.failed = FailedLevel{kHalf, kHalf};
  const World world = WorldOf({{kActor, refused}});
  Check(Fired(Checked(world, {}), Rule::kFailedLevelAskedAgain),
        "a resize to a refused level fires");
  Check(Fired(Checked(world, {StartResize{kActor, kHalf}}),
              Rule::kFailedLevelAskedAgain),
        "a start to a refused level fires");
  Resizing other = Queued(kFull, ResolutionLevel::kQuarter);
  other.failed = FailedLevel{kHalf, kHalf};
  Check(!Fired(Checked(WorldOf({{kActor, other}}), {}),
               Rule::kFailedLevelAskedAgain),
        "a resize to another level keeps the refusal and passes");
}

void ViolationsDescribeThemselves() {
  const Violation violation{7, Rule::kTwoResizesInFlight, kActor, "two"};
  Check(Describe(violation) == "tick 7: two resizes in flight (actor 20): two",
        "a violation names its tick, rule, actor and detail");
}
}

int main() {
  ASettledWorldPasses();
  ReleaseOutsideLive();
  OutOfViewReleaseWaitsForItsHold();
  OneResizeInFlight();
  NoResizeToTheSameLevel();
  NoIgnoredCreatingEvent();
  CommandsAgreeWithPhases();
  DrawComesLast();
  EstimateEqualsHeldCosts();
  FailedLevelNotAskedAgain();
  ViolationsDescribeThemselves();
  return test::Finish("orchestration_invariants");
}
