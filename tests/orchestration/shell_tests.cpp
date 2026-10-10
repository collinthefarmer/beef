// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/sim/Fixtures.h"
#include "test_support.h"

#include <cstddef>
#include <utility>
#include <vector>

using namespace sim;
using test::Check;
using test::Equal;

namespace {
constexpr ActorId kActor{20};
constexpr ResolutionLevel kFull = ResolutionLevel::kFull;
constexpr ResolutionLevel kHalf = ResolutionLevel::kHalf;
constexpr ResolutionLevel kQuarter = ResolutionLevel::kQuarter;

Script OneActor(ActorScript a_actor) {
  Script script;
  script.actors[kActor] = std::move(a_actor);
  return script;
}

Shell ShellFor(ActorScript a_actor) {
  Shell shell;
  shell.script = OneActor(std::move(a_actor));
  return shell;
}

struct Executing {
  Shell shell;
  std::vector<Disagreement> disagreements;
  TickFacts facts;
};

Executing Execute(Shell a_shell, std::vector<Command> a_commands) {
  Execution execution = Executed(std::move(a_shell), a_commands);
  Turn turn = NextFacts(std::move(execution.shell), World{});
  return {std::move(turn.shell), std::move(execution.disagreements),
          std::move(turn.facts)};
}

const RenderEvent *OnlyEvent(const TickFacts &a_facts) {
  if (a_facts.results.size() != 1) {
    return nullptr;
  }
  const ActorEvent *event = Get<ActorEvent>(a_facts.results.front());
  return event ? &event->event : nullptr;
}

Install InstallOf(ActorId a_actor, InstallBound a_bound = {}) {
  return Install{a_actor, {}, {}, InstallKind::kInstall, a_bound, 0};
}

void InstallBuildsARender() {
  const Executing done = Execute(ShellFor(ScriptedActor(InView(0.5f, 100.0f))),
                                 {InstallOf(kActor)});
  const RenderEvent *event = OnlyEvent(done.facts);
  const Installed *installed = event ? Get<Installed>(*event) : nullptr;
  Check(installed && installed->level == kFull && installed->total == 4 &&
            installed->chargedBytes == kFullBytes,
        "an install builds a render at the finest level and charges it");
  Equal(HeldBytes(done.shell), kFullBytes, "the shell holds the built render");
  Check(done.disagreements.empty(), "a fresh install agrees with the engine");

  const Executing bounded =
      Execute(ShellFor(ScriptedActor(InView(0.5f, 100.0f))),
              {InstallOf(kActor, InstallBound{kFull, kFullBytes / 3})});
  const RenderEvent *boundedEvent = OnlyEvent(bounded.facts);
  const Installed *fitted =
      boundedEvent ? Get<Installed>(*boundedEvent) : nullptr;
  Check(fitted && fitted->level == kHalf,
        "an install with little headroom builds the level that fits");
}

void InstallFailures() {
  ActorScript failing = ScriptedActor(InView(0.5f, 100.0f));
  failing.faults.installFails = true;
  const Executing failed = Execute(ShellFor(failing), {InstallOf(kActor)});
  Check(failed.facts.results.size() == 1 &&
            Is<InstallFailed>(failed.facts.results.front()),
        "an injected install failure reports InstallFailed");

  ActorScript bare = ScriptedActor(InView(0.5f, 100.0f));
  bare.withoutRender = true;
  const Executing without = Execute(ShellFor(bare), {InstallOf(kActor)});
  const RenderEvent *event = OnlyEvent(without.facts);
  Check(event && Is<InstalledWithoutRender>(*event),
        "an install without a render reports InstalledWithoutRender");
  Equal(HeldBytes(without.shell), std::uint64_t{0},
        "an install without a render holds nothing");
}

void PlanJobsFinishAfterTheirTicks() {
  ActorScript awaiting = ScriptedActor(InView(0.5f, 100.0f));
  awaiting.planTicks = 3;
  Executing done = Execute(ShellFor(awaiting), {InstallOf(kActor)});
  const RenderEvent *event = OnlyEvent(done.facts);
  const PlanAwaited *awaited = event ? Get<PlanAwaited>(*event) : nullptr;
  Check(awaited != nullptr, "an install with a plan job reports PlanAwaited");
  Check(done.facts.jobs.empty(), "the plan job is not done at once");
  Turn turn = NextFacts(std::move(done.shell), World{});
  Check(turn.facts.jobs.empty(), "the plan job is not done after two ticks");
  turn = NextFacts(std::move(turn.shell), World{});
  Check(turn.facts.jobs.size() == 1 &&
            Is<PlanJobBuilt>(turn.facts.jobs.front()),
        "the plan job is built after three ticks");
  const Executing built =
      Execute(std::move(turn.shell), {BuildAwaitedRender{kActor, {}}});
  const RenderEvent *builtEvent = OnlyEvent(built.facts);
  Check(builtEvent && Is<Installed>(*builtEvent) && built.disagreements.empty(),
        "a finished plan job builds the awaited render");

  ActorScript failing = awaiting;
  failing.faults.planFails = true;
  failing.planTicks = 1;
  Executing failed = Execute(ShellFor(failing), {InstallOf(kActor)});
  Check(failed.facts.jobs.size() == 1 &&
            Is<PlanJobFailed>(failed.facts.jobs.front()),
        "an injected plan failure reports PlanJobFailed");
  const Executing marked = Execute(
      std::move(failed.shell), {FailAwaitedRender{kActor, "plan job failed"}});
  Check(marked.disagreements.empty() && HeldBytes(marked.shell) == 0,
        "a failed awaited render holds nothing");
}

Shell InstalledShell(ActorScript a_actor) {
  Executing done = Execute(ShellFor(std::move(a_actor)), {InstallOf(kActor)});
  return std::move(done.shell);
}

void ResizesStartDrawAndSwap() {
  ActorScript actor = ScriptedActor(InView(0.5f, 100.0f));
  actor.stacks = 5;
  Executing started =
      Execute(InstalledShell(actor), {StartResize{kActor, kHalf}});
  const RenderEvent *event = OnlyEvent(started.facts);
  const ResizeStarted *resize = event ? Get<ResizeStarted>(*event) : nullptr;
  Check(resize && resize->total == 5, "a resize starts with its stack count");
  Equal(HeldBytes(started.shell), kFullBytes + kFullBytes / 4,
        "a running resize holds the target level beside the attached render");

  started.shell.script.drawUnitsPerTick = 3;
  Executing drawn =
      Execute(std::move(started.shell), {DrawUnpublished{{kActor}, 4000}});
  event = OnlyEvent(drawn.facts);
  const UnpublishedDrawn *first =
      event ? Get<UnpublishedDrawn>(*event) : nullptr;
  Check(first && first->drawn == 3 && first->drewThisTick,
        "a draw draws the configured units per tick");
  drawn = Execute(std::move(drawn.shell), {DrawUnpublished{{kActor}, 4000}});
  event = OnlyEvent(drawn.facts);
  const UnpublishedDrawn *second =
      event ? Get<UnpublishedDrawn>(*event) : nullptr;
  Check(second && second->drawn == 5, "a draw stops at the total");
  drawn = Execute(std::move(drawn.shell), {DrawUnpublished{{kActor}, 4000}});
  event = OnlyEvent(drawn.facts);
  const UnpublishedDrawn *third =
      event ? Get<UnpublishedDrawn>(*event) : nullptr;
  Check(third && third->drawn == 5 && !third->drewThisTick,
        "a finished draw reports that it drew nothing");

  const Executing swapped =
      Execute(std::move(drawn.shell),
              {SwapResize{kActor}, SetAppliedLevel{kActor, kHalf}});
  Check(swapped.facts.results.size() == 1 && swapped.disagreements.empty(),
        "a swap applies the new level");
  event = OnlyEvent(swapped.facts);
  Check(event && Is<ResizeSwapped>(*event), "a swap reports ResizeSwapped");
  Equal(HeldBytes(swapped.shell), kFullBytes / 4,
        "a swapped render holds only the new level");
}

void ResizeFailures() {
  ActorScript actor = ScriptedActor(InView(0.5f, 100.0f));
  actor.faults.resizeNotStarted.At(kHalf) = true;
  actor.faults.swapMismatch.At(kQuarter) = true;
  Executing notStarted =
      Execute(InstalledShell(actor), {StartResize{kActor, kHalf}});
  const RenderEvent *event = OnlyEvent(notStarted.facts);
  Check(event && Is<ResizeNotStarted>(*event),
        "an injected start failure reports ResizeNotStarted");
  Equal(HeldBytes(notStarted.shell), kFullBytes,
        "a resize that did not start holds nothing new");

  Executing started =
      Execute(std::move(notStarted.shell), {StartResize{kActor, kQuarter}});
  Executing mismatch = Execute(std::move(started.shell), {SwapResize{kActor}});
  event = OnlyEvent(mismatch.facts);
  Check(event && Is<ResizeNotSwapped>(*event),
        "an injected swap mismatch reports ResizeNotSwapped");
  Equal(HeldBytes(mismatch.shell), kFullBytes + kFullBytes / 16,
        "a mismatched swap keeps the resize until it is abandoned");
  const Executing abandoned =
      Execute(std::move(mismatch.shell), {AbandonResize{kActor}});
  Equal(HeldBytes(abandoned.shell), kFullBytes,
        "an abandoned resize holds nothing");
  Check(abandoned.disagreements.empty(), "abandoning a resize agrees");
}

void ReleasesAndRestores() {
  const LevelCost full = *HalvingCosts(kFullBytes).At(kFull);
  Executing released =
      Execute(InstalledShell(ScriptedActor(InView(0.5f, 100.0f))),
              {ReleaseIntermediates{kActor, ReleaseReason::kOutOfView,
                                    IntermediateBytes(full)}});
  Equal(HeldBytes(released.shell), full.whenReleased,
        "a release holds the released cost");
  Check(released.disagreements.empty(), "a release of held bytes agrees");
  const Executing twice =
      Execute(std::move(released.shell),
              {ReleaseIntermediates{kActor, ReleaseReason::kBudget, 1}});
  Check(twice.disagreements.size() == 1,
        "a second release is an engine disagreement");
  const Executing restored =
      Execute(std::move(twice.shell),
              {TickActor{kActor, true, false, false, false, false}});
  Equal(HeldBytes(restored.shell), full.withIntermediates,
        "drawing stacks restores the intermediates");
}

void RetireForgetObserve() {
  Executing retired = Execute(
      InstalledShell(ScriptedActor(InView(0.5f, 100.0f))), {Retire{kActor}});
  Check(retired.facts.results.size() == 1 &&
            Is<Retired>(retired.facts.results.front()),
        "a retire reports Retired");
  Check(retired.shell.actors.contains(kActor) && HeldBytes(retired.shell) == 0,
        "a retire drops the render and keeps the tracking");
  Executing forgotten = Execute(std::move(retired.shell), {Forget{kActor}});
  Check(!forgotten.shell.actors.contains(kActor) &&
            forgotten.facts.results.empty(),
        "a forget drops the tracking and reports nothing");
  const Executing observed =
      Execute(std::move(forgotten.shell), {Observe{kActor}});
  Check(observed.facts.stale.size() == 1 &&
            observed.facts.stale.front().actor == kActor &&
            observed.facts.sightings.contains(kActor),
        "an observe reports a stale actor with a sighting next tick");

  ActorScript reinstalled = ScriptedActor(InView(0.5f, 100.0f));
  const Executing again = Execute(
      InstalledShell(reinstalled),
      {Install{kActor, {}, {}, InstallKind::kReinstall, {}, kFullBytes}});
  Check(again.facts.results.size() == 2 &&
            Is<Retired>(again.facts.results.front()) &&
            Is<ActorEvent>(again.facts.results.back()) &&
            again.disagreements.empty(),
        "a reinstall reports Retired before its install result");
}

void Disagreements() {
  Shell shell = ShellFor(ScriptedActor(InView(0.5f, 100.0f)));
  const Executing swap = Execute(shell, {SwapResize{kActor}});
  Check(swap.disagreements.size() == 1, "a swap without a resize disagrees");
  const Executing applied =
      Execute(InstalledShell(ScriptedActor(InView(0.5f, 100.0f))),
              {SetAppliedLevel{kActor, kHalf}});
  Check(applied.disagreements.size() == 1,
        "an applied level the render does not have disagrees");
  const Executing doubled = Execute(
      InstalledShell(ScriptedActor(InView(0.5f, 100.0f))), {InstallOf(kActor)});
  Check(doubled.disagreements.size() == 1,
        "a fresh install over an installed actor disagrees");
}

void ScriptedFacts() {
  ActorScript actor = ScriptedActor(OutOfView(500.0f));
  actor.sightings.push_back({3, InView(0.25f, 200.0f)});
  actor.stale.push_back({2, {}});
  Shell shell = ShellFor(actor);
  shell.script.memory = MemoryScript{2, 100 * kMebibyte};
  shell.script.tickMs = 10;
  World world;
  world.actors[kActor] = ActorRecord{};
  Turn turn = NextFacts(std::move(shell), world);
  Check(!turn.facts.sightings.at(kActor).inView && turn.facts.stale.empty() &&
            !turn.facts.memory,
        "tick 1 sights the first keyframe, nothing stale, no sample");
  Equal(turn.facts.now, Millis{10}, "the time advances by the tick interval");
  turn = NextFacts(std::move(turn.shell), world);
  Check(turn.facts.stale.size() == 1 && turn.facts.memory &&
            AllowanceOf(*turn.facts.memory) == 100 * kMebibyte,
        "tick 2 marks the actor stale and samples memory at the allowance");
  turn = NextFacts(std::move(turn.shell), world);
  Check(turn.facts.sightings.at(kActor).inView,
        "tick 3 sights the next keyframe");
  const Turn empty = NextFacts(Shell{}, World{});
  Check(empty.facts.sightings.empty(),
        "an actor outside the world and not stale is not sighted");
}

void CountsCommands() {
  const Execution execution =
      Executed(InstalledShell(ScriptedActor(InView(0.5f, 100.0f))),
               {TickActor{kActor}, TickActor{kActor}, KeepModels{kActor, {}}});
  Equal(CountOf<TickActor>(execution.shell), std::size_t{2},
        "the shell counts each command kind");
  Equal(CountOf<KeepModels>(execution.shell), std::size_t{1},
        "the shell counts commands it only records");
  Equal(CountOf<Install>(execution.shell), std::size_t{1},
        "the counters keep earlier executions");
}
}

int main() {
  InstallBuildsARender();
  InstallFailures();
  PlanJobsFinishAfterTheirTicks();
  ResizesStartDrawAndSwap();
  ResizeFailures();
  ReleasesAndRestores();
  RetireForgetObserve();
  Disagreements();
  ScriptedFacts();
  CountsCommands();
  return test::Finish("orchestration_shell");
}
