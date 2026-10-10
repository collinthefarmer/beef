// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Budget.h"
#include "orchestration/sim/Scenario.h"
#include "test_support.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

using namespace sim;
using test::Check;
using test::Equal;

namespace {
constexpr ResolutionLevel kHalf = ResolutionLevel::kHalf;

Sighting Unloaded(float a_distance) {
  return Sighting{ActorRole::kOther, false, false, std::nullopt, a_distance};
}

bool IssuedAfter(const Simulation &a_sim, ActorId a_actor,
                 std::uint64_t a_tick) {
  for (const TickRecord &record : a_sim.history) {
    for (const Command &command : record.commands) {
      if (record.tick > a_tick && CommandActor(command) == a_actor) {
        return true;
      }
    }
  }
  return false;
}

void APlanJobThatFails() {
  constexpr ActorId kActor{700};
  Script script;
  ActorScript actor =
      Arriving(InView(0.6f, 300.0f), 1, PlanNamed("failing", 0xA00));
  actor.planTicks = 5;
  actor.faults.planFails = true;
  script.actors[kActor] = actor;
  const Simulation sim = Ran(Started(std::move(script)), 200);
  Check(sim.violations.empty(), Report(sim));
  const RenderPhase *phase = PhaseNow(sim, kActor);
  Check(phase && Is<WithoutRender>(*phase), "the actor ends without a render");
  Equal(IssuedFor<FailAwaitedRender>(sim, kActor).size(), std::size_t{1},
        "the awaited render is failed once");
  Check(IssuedFor<BuildAwaitedRender>(sim, kActor).empty(),
        "nothing is built from a failed plan");
  Equal(sim.shell.unitsDrawn, std::size_t{0}, "nothing is drawn");
  bool listed = false;
  for (const Issued<DrawUnpublished> &draw : IssuedOf<DrawUnpublished>(sim)) {
    for (const ActorId drawn : draw.command.order) {
      listed = listed || drawn == kActor;
    }
  }
  Check(!listed, "the actor is never listed for an unpublished draw");
}

void TheRadiusShrinksAndGrows() {
  constexpr ActorId kNear{800};
  constexpr ActorId kFar{801};
  Script script;
  script.tickMs = 100;
  script.memory = MemoryScript{5, std::uint64_t{8} << 30};
  script.actors[kNear] =
      Arriving(InView(0.6f, 500.0f), 1, PlanNamed("near", 0xB00));
  script.actors[kFar] =
      Arriving(InView(0.6f, 2000.0f), 1, PlanNamed("far", 0xB01));
  Simulation sim = Ran(Started(std::move(script)), 60);
  Check(sim.violations.empty(), Report(sim));

  sim.shell.script.memory.allowanceBytes = 5 * kMebibyte / 2;
  sim = Ran(std::move(sim), 400);
  Check(sim.violations.empty(), Report(sim));
  const std::vector<Traced> shrinks = TracedOf(sim, "budget_radius");
  std::optional<std::uint64_t> shrunkAt;
  for (const Traced &radius : shrinks) {
    if (FieldOf(*radius.record, "change") == "shrink" && !shrunkAt) {
      shrunkAt = radius.tick;
      Check(FieldOf(*radius.record, "actor") == std::to_string(FormOf(kFar)),
            "the radius evicts the farthest actor");
    }
  }
  Check(shrunkAt.has_value(), "exhaustion shrinks the radius");
  Check(sim.world.radius.has_value(), "the radius stays while it is needed");
  const std::vector<Issued<Retire>> retired = IssuedFor<Retire>(sim, kFar);
  Check(retired.size() == 1 && shrunkAt && retired.front().tick > *shrunkAt,
        "the evicted actor is observed and retired");
  const ActorRecord *far = RecordIn(sim.world, kFar);
  Check(far && !far->render,
        "the evicted actor keeps its record and no render");
  const RenderPhase *near = PhaseNow(sim, kNear);
  Check(near && Is<Live>(*near), "the near actor stays live");
  Check(IssuedFor<Retire>(sim, kNear).empty(), "only one actor is evicted");

  const std::size_t installsBefore = IssuedFor<Install>(sim, kFar).size();
  sim.shell.script.memory.allowanceBytes = std::uint64_t{8} << 30;
  sim = Ran(std::move(sim), 300);
  Check(sim.violations.empty(), Report(sim));
  Check(!sim.world.radius.has_value(), "the radius clears once pressure ends");
  const std::vector<Issued<Install>> installs = IssuedFor<Install>(sim, kFar);
  Check(installs.size() == installsBefore + 1,
        "the evicted actor installs again");
  const RenderPhase *returned = PhaseNow(sim, kFar);
  Check(returned && Is<Live>(*returned), "the evicted actor returns live");
}

void AReinstallDuringAResize() {
  constexpr ActorId kActor{900};
  Script script;
  script.drawUnitsPerTick = 4;
  ActorScript actor =
      Arriving(InView(0.3f, 300.0f), 1, PlanNamed("first", 0xC00));
  actor.stacks = 400;
  actor.sightings.push_back({40, InView(0.6f, 300.0f)});
  actor.stale.push_back({60, PlanNamed("second", 0xC01)});
  script.actors[kActor] = actor;
  Simulation sim = Ran(Started(std::move(script)), 59);
  Check(sim.violations.empty(), Report(sim));
  const RenderPhase *phase = PhaseNow(sim, kActor);
  Check(phase && IsRunning(*phase), "the actor is resizing when it changes");
  const ActorRecord *before = RecordIn(sim.world, kActor);
  const std::uint64_t replaced = before ? PlannedBytesOf(*before) : 0;
  const std::uint64_t plannedBefore = sim.world.budget.planned;

  sim = Advanced(std::move(sim));
  const TickRecord &reinstall = sim.history.back();
  std::vector<std::size_t> kinds;
  const Install *install = nullptr;
  for (const Command &command : reinstall.commands) {
    if (CommandActor(command) != kActor) {
      continue;
    }
    if (Is<Retire>(command)) {
      kinds.push_back(0);
    }
    if (const Install *found = Get<Install>(command)) {
      kinds.push_back(1);
      install = found;
    }
  }
  Check(kinds == std::vector<std::size_t>{0, 1},
        "the reinstall retires then installs in one tick");
  Check(install && install->kind == InstallKind::kReinstall &&
            install->replacedBytes == replaced &&
            replaced == kFullBytes + kFullBytes / 4,
        "the install replaces the attached render and its running resize");
  Equal(sim.world.budget.planned, plannedBefore - replaced,
        "the replaced bytes are freed when the install is issued");

  const std::uint64_t plannedIssued = sim.world.budget.planned;
  sim = Advanced(std::move(sim));
  const std::vector<Reported<Installed>> installed =
      ReportedOf<Installed>(sim, kActor);
  Check(installed.size() == 2 &&
            sim.world.budget.planned ==
                plannedIssued + installed.back().event.chargedBytes,
        "the new render is charged when its install arrives");

  sim = Ran(std::move(sim), 200);
  Check(sim.violations.empty(), Report(sim));
  const auto engine = sim.shell.actors.find(kActor);
  Check(engine != sim.shell.actors.end() && engine->second.render &&
            !engine->second.resize,
        "the old render's resize work does not leak");
  phase = PhaseNow(sim, kActor);
  Check(phase && Is<Live>(*phase), "the reinstalled actor ends live");
}

void AnActorUnloadsDuringAResize() {
  constexpr ActorId kActor{950};
  Script script;
  script.drawUnitsPerTick = 1;
  ActorScript actor = ScriptedActor(InView(0.6f, 300.0f));
  actor.stacks = 200;
  actor.sightings.push_back({20, Unloaded(300.0f)});
  script.actors[kActor] = actor;
  World world;
  world.actors[kActor] = RenderedRecord(LiveAt(kHalf), InView(0.6f, 300.0f));
  Simulation sim =
      Ran(Started(std::move(script), WithEstimate(std::move(world))), 19);
  const RenderPhase *phase = PhaseNow(sim, kActor);
  Check(phase && IsRunning(*phase), "the actor is resizing when it unloads");
  sim = Ran(std::move(sim), 200);
  Check(sim.violations.empty(), Report(sim));
  const std::vector<Issued<Forget>> forgets = IssuedFor<Forget>(sim, kActor);
  Check(forgets.size() == 1 && forgets.front().tick == 20,
        "the actor is forgotten in the tick its 3D unloads");
  Check(!RecordIn(sim.world, kActor), "the actor's record is erased");
  Check(!sim.shell.actors.contains(kActor), "the engine holds nothing for it");
  Check(!forgets.empty() && !IssuedAfter(sim, kActor, forgets.front().tick),
        "no command follows the forget");
  Check(IssuedFor<SwapResize>(sim, kActor).empty(),
        "the unloaded actor's resize never swaps");
}

void AReinstallTickSendsNothingForTheOldRender() {
  constexpr ActorId kActor{960};
  Script script;
  ActorScript actor =
      Arriving(InView(0.3f, 300.0f), 1, PlanNamed("first", 0xD00));
  actor.stale.push_back({60, PlanNamed("second", 0xD01)});
  script.actors[kActor] = actor;
  const Simulation sim = Ran(Started(std::move(script)), 80);
  Check(sim.violations.empty(), Report(sim));
  const std::vector<Issued<Retire>> retired = IssuedFor<Retire>(sim, kActor);
  Check(retired.size() == 1, "the plan change retires the actor once");
  bool concealed = true;
  bool unlisted = true;
  for (const Issued<Retire> &retire : retired) {
    for (const Issued<TickActor> &tick : IssuedFor<TickActor>(sim, kActor)) {
      concealed =
          concealed && (tick.tick != retire.tick ||
                        (tick.command.concealSlots && tick.command.hideShells &&
                         tick.command.hideLights));
    }
    for (const Issued<DrawUnpublished> &draw : IssuedOf<DrawUnpublished>(sim)) {
      for (const ActorId listed : draw.command.order) {
        unlisted = unlisted && (draw.tick != retire.tick || listed != kActor);
      }
    }
  }
  bool untouched = true;
  for (const Issued<Retire> &retire : retired) {
    for (const Issued<TickActor> &tick : IssuedFor<TickActor>(sim, kActor)) {
      untouched = untouched && tick.tick != retire.tick;
    }
  }
  Check(untouched && concealed,
        "the reinstall tick sends no TickActor for the retired render");
  Check(unlisted, "the reinstall tick does not draw the retired phase's "
                  "unpublished work");
}
}

int main() {
  APlanJobThatFails();
  TheRadiusShrinksAndGrows();
  AReinstallDuringAResize();
  AnActorUnloadsDuringAResize();
  AReinstallTickSendsNothingForTheOldRender();
  return test::Finish("orchestration_scenarios_lifetime");
}
