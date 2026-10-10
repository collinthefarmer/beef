// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Agreement.h"
#include "orchestration/sim/Held.h"
#include "orchestration/sim/Scenario.h"
#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <format>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace sim;
using test::Check;
using test::Equal;

namespace {
using Found = std::vector<Orchestration::Disagreement>;
using Store = std::map<ActorId, HeldResources>;

constexpr ActorId kActor{0x14};
constexpr ResolutionLevel kFull = ResolutionLevel::kFull;
constexpr ResolutionLevel kHalf = ResolutionLevel::kHalf;
constexpr ResolutionLevel kQuarter = ResolutionLevel::kQuarter;

World WorldIn(RenderPhase a_phase) {
  World world;
  world.actors[kActor] = RecordOf(RenderIn(std::move(a_phase)));
  return world;
}

Store StoreOf(HeldResources a_held) { return Store{{kActor, a_held}}; }

HeldResources
RenderAt(ResolutionLevel a_level,
         IntermediateHolding a_holding = IntermediateHolding::kHeld) {
  HeldResources held;
  held.render = HeldRender{a_level, a_holding};
  return held;
}

FirstDrawing FirstDrawingAt(ResolutionLevel a_level, std::size_t a_total,
                            std::size_t a_drawn) {
  FirstDrawing drawing;
  drawing.level = a_level;
  drawing.draw = UnpublishedDraw{a_total, a_drawn, 0, 0};
  return drawing;
}

Resizing ResizingFrom(ResolutionLevel a_from, ResolutionLevel a_to,
                      std::variant<ResizeQueued, ResizeRunning> a_stage) {
  Resizing resizing;
  resizing.from = a_from;
  resizing.to = a_to;
  resizing.holding = IntermediateHolding::kHeld;
  resizing.stage = a_stage;
  return resizing;
}

bool Agrees(const World &a_world, const Store &a_store) {
  return Disagreements(a_world, a_store).empty();
}

void NoRenderMeansNoEntry() {
  Check(Agrees(World{}, Store{}), "no record and no entry agree");
  Check(!Agrees(World{}, StoreOf(RenderAt(kFull))),
        "an entry for an actor without a record disagrees");
  World recordOnly;
  recordOnly.actors[kActor] = ActorRecord{};
  Check(Agrees(recordOnly, Store{}),
        "a record without a render and no entry agree");
  Check(!Agrees(recordOnly, StoreOf(HeldResources{})),
        "an entry for a record without a render disagrees");
}

void WithoutRenderHoldsNothing() {
  const World world = WorldIn(WithoutRender{});
  Check(Agrees(world, Store{}), "an actor without a render may have no entry");
  Check(Agrees(world, StoreOf(HeldResources{})),
        "an actor without a render may have an empty entry");
  Check(!Agrees(world, StoreOf(RenderAt(kFull))),
        "an actor without a render may not hold a render");
  HeldResources awaiting;
  awaiting.awaited = PlanJobId{3};
  Check(!Agrees(world, StoreOf(awaiting)),
        "an actor without a render may not await a plan job");
}

void AwaitingPlanHoldsItsJob() {
  const World world = WorldIn(AwaitingPlan{PlanJobId{5}});
  HeldResources held;
  held.awaited = PlanJobId{5};
  Check(Agrees(world, StoreOf(held)), "the store awaits the same job");
  held.awaited = PlanJobId{6};
  Check(!Agrees(world, StoreOf(held)), "the store awaits another job");
  Check(!Agrees(world, Store{}),
        "the store has no entry for the awaiting actor");
  HeldResources rendered = RenderAt(kFull);
  rendered.awaited = PlanJobId{5};
  Check(!Agrees(world, StoreOf(rendered)),
        "an awaiting actor may not hold a render");
}

void FirstDrawingHoldsItsCount() {
  const World world = WorldIn(FirstDrawingAt(kHalf, 4, 2));
  HeldResources held = RenderAt(kHalf);
  held.firstDraw = UnpublishedCount{4, 2};
  Check(Agrees(world, StoreOf(held)), "the first draw counts agree");
  HeldResources behind = held;
  behind.firstDraw = UnpublishedCount{4, 1};
  Check(!Agrees(world, StoreOf(behind)),
        "a first draw behind the core disagrees");
  HeldResources wrongLevel = held;
  wrongLevel.render = HeldRender{kFull, IntermediateHolding::kHeld};
  Check(!Agrees(world, StoreOf(wrongLevel)),
        "a first draw at another level disagrees");
  HeldResources resizing = held;
  resizing.resize = HeldResize{kFull, {4, 0}};
  Check(!Agrees(world, StoreOf(resizing)),
        "a first draw with a resize disagrees");
  Check(!Agrees(world, StoreOf(RenderAt(kHalf))),
        "a first-drawing actor without a first draw disagrees");
}

void LiveHoldsItsLevelAndHolding() {
  const World world = WorldIn(LiveAt(kFull, IntermediatesReleased{}));
  Check(Agrees(world, StoreOf(RenderAt(kFull, IntermediateHolding::kReleased))),
        "a live render at the same level and holding agrees");
  Check(!Agrees(world, StoreOf(RenderAt(kFull))),
        "a live render that still holds its intermediates disagrees");
  Check(
      !Agrees(world, StoreOf(RenderAt(kHalf, IntermediateHolding::kReleased))),
      "a live render at another level disagrees");
  HeldResources unpublished = RenderAt(kFull, IntermediateHolding::kReleased);
  unpublished.firstDraw = UnpublishedCount{4, 4};
  Check(!Agrees(world, StoreOf(unpublished)),
        "a live render that keeps its first draw disagrees");
  Check(!Agrees(world, Store{}), "a live actor without an entry disagrees");
}

void AQueuedResizeHoldsNoResize() {
  const World world = WorldIn(ResizingFrom(kHalf, kFull, ResizeQueued{}));
  Check(Agrees(world, StoreOf(RenderAt(kHalf))),
        "a queued resize holds the render it resizes");
  HeldResources started = RenderAt(kHalf);
  started.resize = HeldResize{kFull, {4, 0}};
  Check(!Agrees(world, StoreOf(started)),
        "a queued resize with a resize in flight disagrees");
  Check(
      !Agrees(world, StoreOf(RenderAt(kHalf, IntermediateHolding::kReleased))),
      "a queued resize with another holding disagrees");
}

void ARunningResizeHoldsItsDraw() {
  const World world = WorldIn(
      ResizingFrom(kHalf, kFull, ResizeRunning{UnpublishedDraw{4, 1, 0, 0}}));
  HeldResources held = RenderAt(kHalf);
  held.resize = HeldResize{kFull, {4, 1}};
  Check(Agrees(world, StoreOf(held)), "a running resize agrees");
  HeldResources elsewhere = held;
  elsewhere.resize = HeldResize{kQuarter, {4, 1}};
  Check(!Agrees(world, StoreOf(elsewhere)),
        "a resize to another level disagrees");
  HeldResources behind = held;
  behind.resize = HeldResize{kFull, {4, 0}};
  Check(!Agrees(world, StoreOf(behind)), "a resize behind the core disagrees");
  Check(!Agrees(world, StoreOf(RenderAt(kHalf))),
        "a running resize without a resize disagrees");
}

void DisagreementsNameTheActor() {
  const Found found = Disagreements(WorldIn(LiveAt(kFull)), Store{});
  Check(!found.empty() && found.front().actor == kActor,
        "a disagreement names its actor");
}

void TheSettledWorldRecordsResultsButNotJobs() {
  constexpr ActorId kWaiting{0x20};
  World world = WorldIn(LiveAt(kFull));
  world.actors[kWaiting] = RecordOf(RenderIn(AwaitingPlan{PlanJobId{9}}));
  TickFacts facts;
  facts.now = 1000;
  facts.frame = 10;
  facts.sightings[kActor] = InView(0.6f, 200.0f);
  facts.sightings[kWaiting] = InView(0.6f, 200.0f);
  facts.results.push_back(ActorEvent{kActor, ResizeStarted{4}});
  facts.results.push_back(Retired{kActor});
  facts.jobs.push_back(PlanJobBuilt{PlanJobId{9}});
  const World settled = SettledWorld(world, facts);
  const ActorRecord *retired = RecordIn(settled, kActor);
  Check(retired && !retired->render, "a retire result erases the render");
  const RenderPhase *waiting = PhaseIn(settled, kWaiting);
  Check(waiting && Is<AwaitingPlan>(*waiting),
        "a finished job does not move the awaiting actor before it builds");
}

struct AgreementRun {
  Simulation simulation;
  std::vector<std::string> found;
};

std::vector<std::string> FoundBefore(const Simulation &a_simulation) {
  const Turn peek = NextFacts(a_simulation.shell, a_simulation.world);
  const World settled = SettledWorld(a_simulation.world, peek.facts);
  std::vector<std::string> found;
  for (const Orchestration::Disagreement &disagreement :
       Disagreements(settled, HeldOf(a_simulation.shell))) {
    found.push_back(std::format("tick {} actor {:X}: {}", peek.shell.tick,
                                FormOf(disagreement.actor), disagreement.what));
  }
  return found;
}

AgreementRun RanAgreeing(Simulation a_simulation, std::uint64_t a_ticks) {
  AgreementRun run{std::move(a_simulation), {}};
  for (std::uint64_t i = 0; i < a_ticks; ++i) {
    for (std::string &found : FoundBefore(run.simulation)) {
      run.found.push_back(std::move(found));
    }
    run.simulation = Advanced(std::move(run.simulation));
  }
  for (std::string &found : FoundBefore(run.simulation)) {
    run.found.push_back(std::move(found));
  }
  return run;
}

std::string Listed(const std::vector<std::string> &a_found) {
  std::string text;
  for (std::size_t i = 0; i < a_found.size() && i < 10; ++i) {
    text += a_found[i] + "\n";
  }
  return text;
}

void Agreed(const AgreementRun &a_run, std::string_view a_scenario) {
  Check(a_run.simulation.violations.empty(), Report(a_run.simulation));
  Check(a_run.found.empty(), std::format("{} keeps the store in agreement\n{}",
                                         a_scenario, Listed(a_run.found)));
}

void ACrowdArrivingAgreesEveryTick() {
  Script script;
  script.memory = MemoryScript{30, std::uint64_t{8} << 30};
  for (std::uint32_t k = 0; k < 12; ++k) {
    const bool inView = k % 3 != 2;
    const Sighting sight = inView ? InView(0.06f + 0.05f * k, 300.0f)
                                  : OutOfView(400.0f + 10.0f * k);
    ActorScript member =
        Arriving(sight, 1, PlanNamed("crowd" + std::to_string(k), 0x800 + k));
    if (!inView) {
      member.sightings.push_back({150, InView(0.1f, 400.0f)});
    }
    if (k % 4 == 1) {
      member.planTicks = 3;
    }
    script.actors[ActorOf(200 + k)] = std::move(member);
  }
  Agreed(RanAgreeing(Started(std::move(script)), 400), "a crowd arriving");
}

void AnActorLeavingTheViewDuringAResizeAgreesEveryTick() {
  constexpr ActorId kLeaving{300};
  Script script;
  script.settings.outOfViewReleaseDelayMs = 1000;
  script.drawUnitsPerTick = 1;
  ActorScript actor = ScriptedActor(InView(0.6f, 300.0f));
  actor.stacks = 200;
  actor.sightings.push_back({12, OutOfView(300.0f)});
  script.actors[kLeaving] = actor;
  World world;
  world.actors[kLeaving] = RenderedRecord(LiveAt(kHalf), InView(0.6f, 300.0f));
  Agreed(RanAgreeing(Started(std::move(script), WithEstimate(std::move(world))),
                     200),
         "an actor leaving the view during a resize");
}

void PressureDuringFirstDrawsAgreesEveryTick() {
  Script script;
  script.memory = MemoryScript{5, 384 * kMebibyte};
  script.settings.outOfViewReleaseDelayMs = 60000;
  script.drawUnitsPerTick = 2;
  World world;
  for (std::uint32_t k = 0; k < 4; ++k) {
    const Sighting sight = k < 2 ? OutOfView(200.0f + 100.0f * k)
                                 : InView(0.6f, 200.0f + 100.0f * k);
    script.actors[ActorOf(400 + k)] = ScriptedActor(sight);
    world.actors[ActorOf(400 + k)] = RenderedRecord(LiveAt(kFull), sight);
  }
  for (std::uint32_t k = 0; k < 6; ++k) {
    ActorScript arrival =
        Arriving(InView(0.6f, 250.0f + 100.0f * k), 1,
                 PlanNamed("burst" + std::to_string(k), 0x900 + k));
    arrival.slots = 40;
    script.actors[ActorOf(410 + k)] = arrival;
  }
  Agreed(RanAgreeing(Started(std::move(script), WithEstimate(std::move(world))),
                     400),
         "pressure during a burst of first draws");
}

void AFailedPlanJobAgreesEveryTick() {
  Script script;
  ActorScript actor =
      Arriving(InView(0.6f, 300.0f), 1, PlanNamed("failing", 0xA00));
  actor.planTicks = 5;
  actor.faults.planFails = true;
  script.actors[ActorOf(700)] = actor;
  Agreed(RanAgreeing(Started(std::move(script)), 200), "a failed plan job");
}

void AForgetRequestAgreesEveryTick() {
  constexpr ActorId kForgotten{800};
  Script script;
  ActorScript actor =
      Arriving(InView(0.6f, 300.0f), 1, PlanNamed("forgotten", 0xB00));
  actor.forgets.push_back(30);
  script.actors[kForgotten] = actor;
  const AgreementRun run = RanAgreeing(Started(std::move(script)), 60);
  Agreed(run, "a forget request");
  Check(!RecordIn(run.simulation.world, kForgotten),
        "the forgotten actor leaves the world");
  Check(!run.simulation.shell.actors.contains(kForgotten),
        "the forgotten actor leaves the engine");
  Equal(IssuedFor<Forget>(run.simulation, kForgotten).size(), std::size_t{1},
        "the forget request issues one forget");
}
}

int main() {
  NoRenderMeansNoEntry();
  WithoutRenderHoldsNothing();
  AwaitingPlanHoldsItsJob();
  FirstDrawingHoldsItsCount();
  LiveHoldsItsLevelAndHolding();
  AQueuedResizeHoldsNoResize();
  ARunningResizeHoldsItsDraw();
  DisagreementsNameTheActor();
  TheSettledWorldRecordsResultsButNotJobs();
  ACrowdArrivingAgreesEveryTick();
  AnActorLeavingTheViewDuringAResizeAgreesEveryTick();
  PressureDuringFirstDrawsAgreesEveryTick();
  AFailedPlanJobAgreesEveryTick();
  AForgetRequestAgreesEveryTick();
  return test::Finish("orchestration_agreement");
}
