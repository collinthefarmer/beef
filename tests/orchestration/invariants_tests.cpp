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
    world.actors[id] = TrackedOf(RenderIn(std::move(phase)));
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

Shown HeldSince(Millis a_since) { return ShownAt(kFull, Held{Hold{a_since}}); }

Running RunningResize(Level a_from, Level a_to) {
  return Running{Resize{a_from, a_to, IntermediateHolding::kHeld},
                 UnpublishedDraw{4, 0, 1, 0}};
}

Queued QueuedResize(Level a_from, Level a_to) {
  return Queued{Resize{a_from, a_to, IntermediateHolding::kHeld}};
}

void ASettledWorldPasses() {
  const World world = WorldOf(
      {{kActor, ShownAt(kFull)}, {kOther, RunningResize(kFull, kHalf)}});
  const std::vector<Violation> found =
      Checked(world, {TickActor{kActor, true}, SwapResize{kOther},
                      DrawUnpublished{{kOther}, 4000}});
  Check(found.empty(), "a world whose commands fit its phases passes");
}

void ReleaseOutsideShown() {
  const World held = WorldOf({{kActor, ShownAt(kFull)}});
  const ReleaseIntermediates release{kActor, ReleaseReason::kBudget, 1};
  Check(Fired(Checked(held, {release}), Rule::kReleaseOutsideShown),
        "a release that leaves the actor holding fires");
  const World drawing = WorldOf(
      {{kActor, Drawing{kFull, UnpublishedDraw{4, 0, 1, 0}, std::nullopt}}});
  Check(Fired(Checked(drawing, {release}), Rule::kReleaseOutsideShown),
        "a release of a first-drawing actor fires");
  const World released = WorldOf({{kActor, ShownAt(kFull, Freed{})}});
  Check(!Fired(Checked(held, FactsAt(kNow), released, {release}),
               Rule::kReleaseOutsideShown),
        "a release that leaves the actor live and released passes");
  const World steppedDown = WorldOf(
      {{kActor, Queued{Resize{kFull, kHalf, IntermediateHolding::kReleased}}}});
  Check(!Fired(Checked(held, FactsAt(kNow), steppedDown, {release}),
               Rule::kReleaseOutsideShown),
        "a budget release followed by a step-down in the same tick passes");
}

void OutOfViewReleaseWaitsForItsHold() {
  const World released = WorldOf({{kActor, ShownAt(kFull, Freed{})}});
  const ReleaseIntermediates release{kActor, ReleaseReason::kOutOfView, 1};
  const World early = WorldOf({{kActor, HeldSince(kNow - 100)}});
  Check(Fired(Checked(early, FactsAt(kNow), released, {release}),
              Rule::kOutOfViewReleaseEarly),
        "an out-of-view release before the hold delay fires");
  const World swapped = WorldOf({{kActor, RunningResize(kHalf, kFull)}});
  Check(Fired(Checked(swapped, FactsAt(kNow), released, {release}),
              Rule::kOutOfViewReleaseEarly),
        "an out-of-view release in the tick a swap lands fires");
  const World due = WorldOf({{kActor, HeldSince(kNow - 6000)}});
  Check(!Fired(Checked(due, FactsAt(kNow), released, {release}),
               Rule::kOutOfViewReleaseEarly),
        "an out-of-view release after the hold delay passes");
}

void OneResizeInFlight() {
  const World two = WorldOf({{kActor, RunningResize(kFull, kHalf)},
                             {kOther, RunningResize(kFull, kHalf)}});
  Check(Fired(Checked(two, {}), Rule::kTwoResizesInFlight),
        "two running resizes fire");
  const World starting = WorldOf({{kActor, RunningResize(kFull, kHalf)},
                                  {kOther, QueuedResize(kFull, kHalf)}});
  Check(Fired(Checked(starting, {StartResize{kOther, kHalf}}),
              Rule::kTwoResizesInFlight),
        "a resize started beside a running one fires");
  Check(!Fired(Checked(starting, {}), Rule::kTwoResizesInFlight),
        "a queued resize beside a running one passes");
  Check(!Fired(
            Checked(starting, {SwapResize{kActor}, StartResize{kOther, kHalf}}),
            Rule::kTwoResizesInFlight),
        "a resize started beside one that swaps passes");
}

void NoResizeToTheSameLevel() {
  const World same = WorldOf({{kActor, QueuedResize(kHalf, kHalf)}});
  Check(Fired(Checked(same, {}), Rule::kResizeToSameLevel),
        "a resize from a level to itself fires");
}

void NoIgnoredCreatingEvent() {
  const World live = WorldOf({{kActor, ShownAt(kFull)}});
  TickFacts facts = FactsAt(kNow);
  facts.results.push_back(
      ActorEvent{kActor, Installed{kFull, 4, HalvingCosts(kFullBytes), 1}});
  Check(Fired(Checked(live, facts, live, {}), Rule::kCreatingEventIgnored),
        "an install result for a live actor fires");
  const World awaiting = WorldOf({{kActor, Planning{PlanJobId{1}}}});
  const World drawing = WorldOf(
      {{kActor, Drawing{kFull, UnpublishedDraw{4, 0, 1, 0}, std::nullopt}}});
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
  Check(!Fired(Checked(awaiting, facts, World{}, {Forget{kActor}}),
               Rule::kCreatingEventIgnored),
        "an install result for an actor forgotten in the same tick passes");
}

void CommandsAgreeWithPhases() {
  const World live = WorldOf({{kActor, ShownAt(kFull)}});
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
  const World running = WorldOf({{kActor, RunningResize(kFull, kHalf)}});
  Check(Fired(Checked(running, {AbandonResize{kActor}}),
              Rule::kCommandDisagreesWithPhase),
        "an abandoned resize that still runs fires");
}

void DrawComesLast() {
  const World drawing = WorldOf(
      {{kActor, Drawing{kFull, UnpublishedDraw{4, 0, 1, 0}, std::nullopt}}});
  Check(Fired(Checked(drawing,
                      {DrawUnpublished{{kActor}, 4000}, TickActor{kActor}}),
              Rule::kDrawUnpublishedNotLast),
        "a command after the unpublished draw fires");
  Check(Fired(Checked(drawing, {DrawUnpublished{{}, 4000},
                                DrawUnpublished{{kActor}, 4000}}),
              Rule::kDrawUnpublishedNotLast),
        "two unpublished draws fire");
}

void EstimateMatchesTheEngineWhenSettled() {
  World world = WorldOf({{kActor, ShownAt(kFull)}});
  if (world.actors[kActor].render) {
    world.actors[kActor].render->renderedFrame = 1;
  }
  Shell seeded = Started(Script{}, world).shell;
  Check(Settled(world, {TickActor{kActor}}) &&
            !SettledEstimateViolation(1, world, {TickActor{kActor}}, seeded),
        "a settled estimate equal to the engine's held bytes passes");
  if (EngineActor *engine = &seeded.actors[kActor]; engine->render) {
    engine->render->holding = IntermediateHolding::kReleased;
  }
  Check(SettledEstimateViolation(1, world, {}, seeded).has_value(),
        "a settled estimate that differs from the engine fires");
  Check(!Settled(world, {Retire{kActor}}) &&
            !SettledEstimateViolation(1, world, {Retire{kActor}}, seeded),
        "a tick that changes resources is not settled");
  const World running = WorldOf({{kActor, RunningResize(kFull, kHalf)}});
  Check(!Settled(running, {}), "a running resize is not settled");
}

void FailedLevelNotAskedAgain() {
  Queued refused = QueuedResize(kFull, kHalf);
  refused.resize.failed = FailedLevel{kHalf, kHalf};
  const World world = WorldOf({{kActor, refused}});
  Check(Fired(Checked(world, {}), Rule::kFailedLevelAskedAgain),
        "a resize to a refused level fires");
  Check(Fired(Checked(world, {StartResize{kActor, kHalf}}),
              Rule::kFailedLevelAskedAgain),
        "a start to a refused level fires");
  Queued other = QueuedResize(kFull, ResolutionLevel::kQuarter);
  other.resize.failed = FailedLevel{kHalf, kHalf};
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
  ReleaseOutsideShown();
  OutOfViewReleaseWaitsForItsHold();
  OneResizeInFlight();
  NoResizeToTheSameLevel();
  NoIgnoredCreatingEvent();
  CommandsAgreeWithPhases();
  DrawComesLast();
  EstimateMatchesTheEngineWhenSettled();
  FailedLevelNotAskedAgain();
  ViolationsDescribeThemselves();
  return test::Finish("orchestration_invariants");
}
