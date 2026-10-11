// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/sim/Scenario.h"
#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace sim;
using test::Check;
using test::Equal;

namespace {
constexpr ResolutionLevel kFull = ResolutionLevel::kFull;
constexpr ResolutionLevel kHalf = ResolutionLevel::kHalf;

void AnEmptyWorldStaysEmpty() {
  Script script;
  script.memory = MemoryScript{60, 512 * kMebibyte};
  const Simulation simulation = Ran(Started(script), 300);
  Check(simulation.violations.empty(), Report(simulation));
  Check(simulation.world.actors.empty(), "an empty world gains no actor");
  bool quiet = true;
  for (const TickRecord &record : simulation.history) {
    quiet = quiet && record.commands.empty();
  }
  Check(quiet, "an empty world issues no command");
}

void ASettledCrowdKeepsEveryInvariant() {
  Script script;
  World world;
  for (std::uint32_t form = 100; form < 120; ++form) {
    const ActorId actor = ActorOf(form);
    const Sighting sight =
        form % 2 == 0 ? InView(0.6f, 200.0f) : OutOfView(1500.0f);
    script.actors[actor] = ScriptedActor(sight);
    world.actors[actor] =
        RecordOf(RenderIn(ShownAt(ResolutionLevel::kFull)), sight);
  }
  const Simulation simulation =
      Ran(Started(std::move(script), WithEstimate(std::move(world))), 300);
  Check(simulation.violations.empty(), Report(simulation));
  Check(simulation.world.actors.size() == 20,
        "a settled crowd keeps its actors");
}

struct CrowdMember {
  ActorId actor{};
  bool inView = false;
  std::optional<float> screenFraction;
};

bool InstalledBefore(const CrowdMember &a_left, const CrowdMember &a_right) {
  if (a_left.inView != a_right.inView) {
    return a_left.inView;
  }
  return !a_left.inView || a_left.screenFraction >= a_right.screenFraction;
}

void ACrowdArrivesAtOnce() {
  Script script;
  script.memory = MemoryScript{30, std::uint64_t{8} << 30};
  std::map<ActorId, CrowdMember> crowd;
  for (std::uint32_t k = 0; k < 12; ++k) {
    const ActorId actor = ActorOf(200 + k);
    const bool inView = k % 3 != 2;
    const Sighting sight = inView ? InView(0.06f + 0.05f * k, 300.0f)
                                  : OutOfView(400.0f + 10.0f * k);
    ActorScript member =
        Arriving(sight, 1, PlanNamed("crowd" + std::to_string(k), 0x800 + k));
    if (!inView) {
      member.sightings.push_back({150, InView(0.1f, 400.0f)});
    }
    script.actors[actor] = std::move(member);
    crowd[actor] = {actor, inView, sight.screenFraction};
  }
  const Simulation sim = Ran(Started(std::move(script)), 400);
  Check(sim.violations.empty(), Report(sim));

  bool allShown = true;
  for (const auto &[actor, member] : crowd) {
    const RenderPhase *phase = PhaseNow(sim, actor);
    allShown = allShown && phase && Is<Shown>(*phase);
  }
  Check(allShown, "every actor of the crowd ends live");

  std::size_t mostPerTick = 0;
  for (const TickRecord &record : sim.history) {
    std::size_t installs = 0;
    for (const Command &command : record.commands) {
      installs += Is<Install>(command) ? 1 : 0;
    }
    mostPerTick = std::max(mostPerTick, installs);
  }
  Equal(mostPerTick, std::size_t{1}, "no more than one install per tick");

  const std::vector<Issued<Install>> installs = IssuedOf<Install>(sim);
  Equal(installs.size(), std::size_t{12}, "each actor installs once");
  bool inPrecedence = true;
  for (std::size_t i = 2; i < installs.size(); ++i) {
    inPrecedence =
        inPrecedence && InstalledBefore(crowd[installs[i - 1].command.actor],
                                        crowd[installs[i].command.actor]);
  }
  Check(inPrecedence, "waiting installs start in view and larger first");
}

void AnActorLeavesTheViewDuringAResize() {
  constexpr ActorId kActor{300};
  Script script;
  script.settings.outOfViewReleaseDelayMs = 1000;
  script.drawUnitsPerTick = 1;
  ActorScript actor = ScriptedActor(InView(0.6f, 300.0f));
  actor.stacks = 200;
  actor.sightings.push_back({12, OutOfView(300.0f)});
  script.actors[kActor] = actor;
  World world;
  world.actors[kActor] = RenderedRecord(ShownAt(kHalf), InView(0.6f, 300.0f));
  world = WithEstimate(std::move(world));
  const Simulation sim = Ran(Started(std::move(script), world), 200);
  Check(sim.violations.empty(), Report(sim));

  const std::vector<Issued<SwapResize>> swaps = IssuedOf<SwapResize>(sim);
  Check(swaps.size() == 1, "the resize swaps once");
  const TickRecord *swapTick =
      swaps.empty() ? nullptr : RecordAt(sim, swaps.front().tick);
  Check(swapTick && !swapTick->facts.sightings.at(kActor).inView,
        "the swap happens out of view");
  const std::vector<Reported<UnpublishedDrawn>> drawn =
      ReportedOf<UnpublishedDrawn>(sim, kActor);
  Check(!drawn.empty() && drawn.back().event.drawn < 200,
        "the swap does not wait for the draw to finish");

  const RenderPhase *phase = PhaseNow(sim, kActor);
  const Shown *shown = phase ? Get<Shown>(*phase) : nullptr;
  Check(shown && shown->level == kFull, "the actor ends live at the new level");

  const std::vector<Reported<ResizeSwapped>> swapped =
      ReportedOf<ResizeSwapped>(sim, kActor);
  const std::vector<Issued<ReleaseIntermediates>> releases =
      IssuedFor<ReleaseIntermediates>(sim, kActor);
  Check(swapped.size() == 1 && releases.size() == 1,
        "the swapped render is released once");
  if (swapped.size() == 1 && releases.size() == 1) {
    const Millis swappedAt = RecordAt(sim, swapped.front().tick)->facts.now;
    const Millis releasedAt = RecordAt(sim, releases.front().tick)->facts.now;
    Check(releases.front().command.reason == ReleaseReason::kOutOfView &&
              releasedAt >= swappedAt + 1000 &&
              releasedAt <= swappedAt + 1000 + 2 * sim.shell.script.tickMs,
          "the release comes one out-of-view hold after the swap");
  }
}

void PressureDuringABurstOfFirstDraws() {
  Script script;
  script.memory = MemoryScript{5, 384 * kMebibyte};
  script.settings.outOfViewReleaseDelayMs = 60000;
  script.drawUnitsPerTick = 2;
  World world;
  for (std::uint32_t k = 0; k < 4; ++k) {
    const ActorId actor = ActorOf(400 + k);
    const Sighting sight = k < 2 ? OutOfView(200.0f + 100.0f * k)
                                 : InView(0.6f, 200.0f + 100.0f * k);
    script.actors[actor] = ScriptedActor(sight);
    world.actors[actor] = RenderedRecord(ShownAt(kFull), sight);
  }
  for (std::uint32_t k = 0; k < 6; ++k) {
    const ActorId actor = ActorOf(410 + k);
    ActorScript arrival =
        Arriving(InView(0.6f, 250.0f + 100.0f * k), 1,
                 PlanNamed("burst" + std::to_string(k), 0x900 + k));
    arrival.slots = 40;
    script.actors[actor] = arrival;
  }
  world = WithEstimate(std::move(world));
  const WatchedRun run = RanWatched(Started(std::move(script), world), 400);
  const Simulation &sim = run.simulation;
  Check(sim.violations.empty(), Report(sim));

  std::optional<std::uint64_t> firstRelease;
  bool firstDrawsSpared = true;
  for (const Issued<ReleaseIntermediates> &release :
       IssuedOf<ReleaseIntermediates>(sim)) {
    if (release.command.reason != ReleaseReason::kBudget) {
      continue;
    }
    firstRelease = firstRelease.value_or(release.tick);
    firstDrawsSpared =
        firstDrawsSpared &&
        PhaseBefore(run, release.tick, release.command.actor) == "shown";
  }
  const std::vector<Traced> stepDowns = TracedOf(sim, "budget_step_down");
  for (const Traced &stepDown : stepDowns) {
    const ActorId actor = ActorOf(static_cast<std::uint32_t>(
        std::stoul(FieldOf(*stepDown.record, "actor"))));
    firstDrawsSpared =
        firstDrawsSpared && PhaseBefore(run, stepDown.tick, actor) != "drawing";
  }
  Check(firstRelease.has_value(), "the pressure releases intermediates");
  Check(!stepDowns.empty(), "the pressure steps actors down");
  Check(firstDrawsSpared, "no first-drawing actor is released or stepped down");
  Check(firstRelease && !stepDowns.empty() &&
            *firstRelease <= stepDowns.front().tick,
        "releases come before step-downs");
  Check(!TracedOf(sim, "budget_pressure").empty(),
        "the budget reports the pressure");
}

ActorScript ResizingActor(float a_screenFraction) {
  ActorScript actor = ScriptedActor(InView(a_screenFraction, 300.0f));
  actor.stacks = 4;
  return actor;
}

Simulation StartedAtHalf(ActorId a_actor, ActorScript a_script) {
  Script script;
  script.actors[a_actor] = std::move(a_script);
  World world;
  world.actors[a_actor] = RenderedRecord(
      ShownAt(kHalf), script.actors[a_actor].sightings.front().sighting);
  return Started(std::move(script), WithEstimate(std::move(world)));
}

std::size_t StartsTo(const Simulation &a_sim, ActorId a_actor,
                     ResolutionLevel a_level, std::uint64_t a_from,
                     std::uint64_t a_to) {
  std::size_t starts = 0;
  for (const Issued<StartResize> &start :
       IssuedFor<StartResize>(a_sim, a_actor)) {
    starts +=
        start.command.to == a_level && start.tick >= a_from && start.tick < a_to
            ? 1
            : 0;
  }
  return starts;
}

void AResizeThatDoesNotStart() {
  constexpr ActorId kActor{500};
  ActorScript actor = ResizingActor(0.6f);
  actor.faults.resizeNotStarted.At(kFull) = true;
  actor.sightings.push_back({100, InView(0.3f, 300.0f)});
  actor.sightings.push_back({150, InView(0.6f, 300.0f)});
  Simulation sim = Ran(StartedAtHalf(kActor, actor), 140);
  Check(sim.violations.empty(), Report(sim));
  Equal(StartsTo(sim, kActor, kFull, 0, 150), std::size_t{1},
        "the failed level is not asked for again while it stays wanted");
  const RenderPhase *phase = PhaseNow(sim, kActor);
  const Shown *shown = phase ? Get<Shown>(*phase) : nullptr;
  Check(shown && shown->level == kHalf,
        "the actor stays shown at its old level");

  sim.shell.script.actors[kActor].faults.resizeNotStarted.At(kFull) = false;
  sim = Ran(std::move(sim), 160);
  Check(sim.violations.empty(), Report(sim));
  Equal(StartsTo(sim, kActor, kFull, 150, 400), std::size_t{1},
        "the level is retried once the wanted level changed");
  phase = PhaseNow(sim, kActor);
  shown = phase ? Get<Shown>(*phase) : nullptr;
  Check(shown && shown->level == kFull, "the retried resize lands");
}

void ASwapMismatch() {
  constexpr ActorId kActor{600};
  ActorScript actor = ResizingActor(0.6f);
  actor.faults.swapMismatch.At(kFull) = true;
  const Simulation sim = Ran(StartedAtHalf(kActor, actor), 200);
  Check(sim.violations.empty(), Report(sim));
  Equal(ReportedOf<ResizeNotSwapped>(sim, kActor).size(), std::size_t{1},
        "the swap mismatches once");
  Equal(IssuedFor<AbandonResize>(sim, kActor).size(), std::size_t{1},
        "the mismatched resize is abandoned");
  const RenderPhase *phase = PhaseNow(sim, kActor);
  Check(phase && Is<Shown>(*phase) && AppliedLevel(*phase) == kHalf &&
            FailedLevelOf(*phase) == kFull,
        "the actor is live at its old level with the level recorded as failed");
  const auto engine = sim.shell.actors.find(kActor);
  Check(engine != sim.shell.actors.end() && !engine->second.resize &&
            HeldBytes(engine->second) ==
                HalvingCosts(kFullBytes).At(kHalf)->withIntermediates,
        "no resize work is left in the engine");
  Equal(StartsTo(sim, kActor, kFull, 0, 200), std::size_t{1},
        "the failed level is not asked for again");
}
}

int main() {
  AnEmptyWorldStaysEmpty();
  ASettledCrowdKeepsEveryInvariant();
  ACrowdArrivesAtOnce();
  AnActorLeavesTheViewDuringAResize();
  PressureDuringABurstOfFirstDraws();
  AResizeThatDoesNotStart();
  ASwapMismatch();
  return test::Finish("orchestration_scenarios");
}
