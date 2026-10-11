// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Budget.h"
#include "orchestration/Output.h"

#include "actors/Lifecycle.h"
#include "orchestration/PhaseChanges.h"
#include "orchestration/Tick.h"
#include "planners/RenderMemory.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <iterator>
#include <span>
#include <string>
#include <utility>

namespace BetterEnchantmentEffects::Orchestration {
namespace {
[[nodiscard]] std::uint32_t PlannerMs(Millis a_now) noexcept {
  return static_cast<std::uint32_t>(a_now);
}

[[nodiscard]] std::string FormText(ActorId a_actor) {
  return std::to_string(FormOf(a_actor));
}

[[nodiscard]] std::string RadiusText(float a_distance) {
  return std::format("{:.0f}", a_distance);
}

[[nodiscard]] const std::optional<LevelCost> &
CostAt(const InstalledRender &a_render, ResolutionLevel a_level) noexcept {
  return a_render.levelCosts.At(a_level);
}

[[nodiscard]] bool Rendered(const ActorRecord &a_record) noexcept {
  return a_record.render && a_record.render->renderedFrame;
}

[[nodiscard]] ActorSight SightOf(const Sighting &a_sight) noexcept {
  return {a_sight.screenFraction, a_sight.inView, a_sight.role};
}

[[nodiscard]] std::uint64_t
RunningResizeBytes(const InstalledRender &a_render) noexcept {
  const std::optional<ResizeProgress> progress =
      ResizeProgressOf(a_render.phase);
  if (!progress) {
    return 0;
  }
  const std::optional<LevelCost> &cost = CostAt(a_render, progress->to);
  return cost ? cost->withIntermediates : 0;
}

[[nodiscard]] std::optional<StepDownOption>
StepDownFor(const InstalledRender &a_render, ResolutionLevel a_level,
            const LevelCost &a_current) noexcept {
  if (!MayStepDown(a_render.phase)) {
    return std::nullopt;
  }
  const std::optional<ResolutionLevel> coarser = CoarserLevel(a_level);
  if (!coarser || FailedLevelOf(a_render.phase) == coarser) {
    return std::nullopt;
  }
  return StepDownOf(a_level, a_current, CostAt(a_render, *coarser),
                    HoldingOf(a_render.phase));
}

[[nodiscard]] std::uint64_t SurplusBytesOf(const InstalledRender &a_render,
                                           const Sighting &a_sight,
                                           ResolutionLevel a_level,
                                           std::uint64_t a_planned) noexcept {
  const std::optional<ResolutionLevel> wanted =
      WantedLevel({a_sight.screenFraction, a_sight.inView, a_sight.role});
  if (!wanted || LevelGap(a_level, *wanted) <= 0) {
    return 0;
  }
  const std::optional<LevelCost> &cost = CostAt(a_render, *wanted);
  if (!cost) {
    return 0;
  }
  return Less(a_planned, PlannedBytes(*cost, HoldingOf(a_render.phase)));
}

[[nodiscard]] bool AnyResizing(const World &a_world) noexcept {
  return std::ranges::any_of(a_world.actors, [](const auto &a_entry) {
    const ActorRecord &record = a_entry.second;
    return record.render && FindResize(record.render->phase);
  });
}

struct BudgetCosts {
  std::uint64_t releasable = 0;
  std::optional<StepDownOption> stepDown;
  std::uint64_t planned = 0;
  std::uint64_t surplus = 0;
};

[[nodiscard]] BudgetCosts BudgetCostsOf(const InstalledRender &a_render,
                                        const Sighting &a_sight,
                                        ResolutionLevel a_level) noexcept {
  const std::optional<LevelCost> &current = CostAt(a_render, a_level);
  if (!current) {
    return {};
  }
  const std::uint64_t planned =
      PlannedBytes(*current, HoldingOf(a_render.phase));
  return {.releasable =
              MayRelease(a_render.phase) ? IntermediateBytes(*current) : 0,
          .stepDown = StepDownFor(a_render, a_level, *current),
          .planned = planned,
          .surplus = SurplusBytesOf(a_render, a_sight, a_level, planned)};
}

void RecordPressure(TickOutput &a_output, const BudgetSample &a_sample,
                    const BudgetState &a_before) {
  const BudgetState &now = a_output.world.budget;
  const bool wasOver = IsOver(a_before.pressure);
  const bool isOver = IsOver(now.pressure);
  if (wasOver == isOver) {
    return;
  }
  AddLog(a_output, LogLevel::kInfo,
         isOver ? PressureStartLine(a_sample, now) : PressureEndLine(now));
  AddTrace(a_output,
           {{"action", "budget_pressure"},
            {"pressure", isOver ? "true" : "false"},
            {"planned_bytes", std::to_string(now.planned)},
            {"allowance_bytes", std::to_string(now.allowance.value_or(0))}});
}

void ApplyPhaseChange(TickOutput &a_output, InstalledRender &a_render,
                      PhaseChange a_change) {
  a_render.phase = a_change.phase;
  Append(a_output.commands, std::move(a_change.commands));
}

std::vector<Trace::Field> StepDownTrace(const StepDownReport &a_report) {
  std::vector<Trace::Field> fields{
      {"action", "budget_step_down"},
      {"actor", FormText(ActorOf(a_report.actor))},
      {"from", std::string{LevelName(a_report.from)}},
      {"to", std::string{LevelName(a_report.option.to)}},
      {"freed_bytes", std::to_string(a_report.option.freed)},
      {"reason", a_report.preemptedBy ? "preempted" : "render target budget"}};
  if (!a_report.preemptedBy) {
    return fields;
  }
  fields.push_back({"for_actor", FormText(ActorOf(*a_report.preemptedBy))});
  return fields;
}

void ReportStepDown(TickOutput &a_output, const StepDownReport &a_report) {
  AddLog(a_output, LogLevel::kInfo, StepDownLine(a_report));
  AddTrace(a_output, StepDownTrace(a_report));
}

void StepDownForBudget(TickOutput &a_output, ActorId a_actor,
                       const StepDownOption &a_option,
                       MaybeActor a_preemptedFor) {
  ActorRecord *record = TrackedActor(a_output.world, a_actor);
  if (!record || !record->render || !MayStepDown(record->render->phase)) {
    return;
  }
  InstalledRender &render = *record->render;
  const std::optional<ResolutionLevel> from = AppliedLevel(render.phase);
  if (!from) {
    return;
  }

  ApplyPhaseChange(a_output, render,
                   WantLevel(a_actor, render.phase, a_option.to));

  ReportStepDown(a_output, {.actor = FormOf(a_actor),
                            .name = record->sight.name,
                            .from = *from,
                            .option = a_option,
                            .preemptedBy = a_preemptedFor.transform(FormOf)});
}

void ReleaseOne(TickOutput &a_output, const BudgetRelease &a_release) {
  const ActorId actor = ActorOf(a_release.actor);
  ActorRecord *record = TrackedActor(a_output.world, actor);
  if (!record || !record->render) {
    return;
  }
  InstalledRender &render = *record->render;
  ApplyPhaseChange(a_output, render,
                   ReleaseForBudget(actor, render.phase, a_release.bytes));
}

void ReportRadiusShrink(TickOutput &a_output, const RadiusShrink &a_shrink,
                        std::string_view a_name) {
  AddLog(a_output, LogLevel::kInfo,
         RadiusShrinkLine(a_shrink.radius.distance, a_shrink.actor, a_name,
                          a_shrink.freed));
  AddTrace(a_output, {{"action", "budget_radius"},
                      {"change", "shrink"},
                      {"radius", RadiusText(a_shrink.radius.distance)},
                      {"actor", FormText(ActorOf(a_shrink.actor))},
                      {"freed_bytes", std::to_string(a_shrink.freed)}});
}

bool ShrinkBudgetRadius(TickOutput &a_output,
                        std::span<const BudgetActor> a_actors,
                        const TickFacts &a_facts) {
  const std::optional<RadiusShrink> shrink =
      ShrinkRadius(a_actors, a_output.world.radius, PlannerMs(a_facts.now));
  if (!shrink) {
    return false;
  }

  const ActorId evicted = ActorOf(shrink->actor);
  ApplyRadius(a_output, shrink->radius);
  Issue(a_output, Observe{evicted});

  const ActorRecord *record = TrackedActor(a_output.world, evicted);
  ReportRadiusShrink(a_output, *shrink, record ? record->sight.name : "");
  return true;
}

std::optional<ActorHolding> HoldingOf(ActorId a_actor,
                                      const ActorRecord &a_record) {
  if (!Rendered(a_record)) {
    return std::nullopt;
  }
  return ActorHolding{.actor = FormOf(a_actor),
                      .name = a_record.sight.name,
                      .ownBytes = PlannedBytesOf(a_record),
                      .sharedBytes = 0,
                      .screenFraction = a_record.sight.screenFraction,
                      .recipes = {}};
}

std::vector<ActorHolding> HoldingsByActor(const World &a_world) {
  return Choose(a_world.actors, [](const auto &a_entry) {
    return HoldingOf(a_entry.first, a_entry.second);
  });
}

void ShrinkRadiusOrWarn(TickOutput &a_output,
                        std::span<const BudgetActor> a_actors,
                        const TickFacts &a_facts) {
  if (ShrinkBudgetRadius(a_output, a_actors, a_facts)) {
    return;
  }
  if (a_output.world.budget.pressure != Pressure::kOver) {
    return;
  }
  ApplyPressure(a_output, Pressure::kOverWarned);
  AddLog(a_output, LogLevel::kWarning,
         ExhaustedLine(a_output.world.budget,
                       LargestHoldings(HoldingsByActor(a_output.world),
                                       kReportedHoldings)));
}

void RelieveBudget(TickOutput &a_output, std::uint64_t a_goal,
                   const TickFacts &a_facts) {
  const BudgetActors now = BudgetActorsOf(a_output.world);
  const BudgetResponse response = RespondToPressure(
      a_output.world.budget.planned, a_goal, now.actors, now.resizeBusy);

  for (const BudgetRelease &release : response.release) {
    ReleaseOne(a_output, release);
  }

  Match(
      response.next, [](const Enough &) {}, [](const AwaitResize &) {},
      [&](const StepDown &a_step) {
        StepDownForBudget(a_output, ActorOf(a_step.actor), a_step.option,
                          std::nullopt);
      },
      [&](const Exhausted &) {
        ShrinkRadiusOrWarn(a_output, now.actors, a_facts);
      });
}

struct Preemption {
  ActorId victim{};
  StepDownOption option;
  ActorId requester{};
};

const BudgetActor *CandidateFor(const BlockedStepUp &a_waiting,
                                std::span<const BudgetActor> a_actors) {
  const std::optional<std::size_t> chosen =
      PreemptionCandidate(a_waiting.request, a_actors);
  if (!chosen || *chosen >= a_actors.size()) {
    return nullptr;
  }
  return &a_actors[*chosen];
}

std::optional<Preemption>
ChoosePreemption(std::span<const BlockedStepUp> a_waiting,
                 std::span<const BudgetActor> a_actors) {
  for (const BlockedStepUp &requester : a_waiting) {
    const BudgetActor *victim = CandidateFor(requester, a_actors);
    if (!victim) {
      continue;
    }
    if (!victim->stepDown) {
      return std::nullopt;
    }
    return Preemption{ActorOf(victim->actor), *victim->stepDown,
                      requester.actor};
  }
  return std::nullopt;
}

void MakeRoomForWaitingStepUp(TickOutput &a_output) {
  const std::vector<BlockedStepUp> waiting = WaitingStepUpsOf(a_output.world);
  if (waiting.empty()) {
    return;
  }
  const BudgetActors now = BudgetActorsOf(a_output.world);
  if (now.resizeBusy) {
    return;
  }
  const std::optional<Preemption> preemption =
      ChoosePreemption(waiting, now.actors);
  if (!preemption) {
    return;
  }
  StepDownForBudget(a_output, preemption->victim, preemption->option,
                    preemption->requester);
}

void ReportRadiusGrowth(TickOutput &a_output, const RadiusGrowth &a_growth) {
  if (const RadiusGrown *grown = Get<RadiusGrown>(a_growth)) {
    AddLog(a_output, LogLevel::kInfo, RadiusGrowLine(grown->radius.distance));
    AddTrace(a_output,
             {{"action", "budget_radius"},
              {"change", "grow"},
              {"radius", RadiusText(grown->radius.distance)},
              {"returning_bytes", std::to_string(grown->returningBytes)}});
    return;
  }
  if (const RadiusBlocked *blocked = Get<RadiusBlocked>(a_growth)) {
    if (!blocked->firstInEpisode) {
      return;
    }
    AddLog(a_output, LogLevel::kInfo, RadiusBlockedLine(*blocked));
    AddTrace(a_output,
             {{"action", "budget_radius"},
              {"change", "blocked"},
              {"reason", "ring"},
              {"radius", RadiusText(blocked->radius.distance)},
              {"ring_bytes", std::to_string(blocked->ringBytes)},
              {"headroom_bytes", std::to_string(blocked->room.headroom)},
              {"margin_bytes", std::to_string(blocked->room.margin)}});
    return;
  }
  if (const RadiusCleared *cleared = Get<RadiusCleared>(a_growth)) {
    AddLog(a_output, LogLevel::kInfo, RadiusClearLine());
    AddTrace(a_output,
             {{"action", "budget_radius"},
              {"change", "clear"},
              {"radius", "none"},
              {"returning_bytes", std::to_string(cleared->returningBytes)}});
  }
}

[[nodiscard]] std::optional<BudgetRadius>
RadiusAfter(const RadiusGrowth &a_growth) noexcept {
  return Match(
      a_growth,
      [](const RadiusUnchanged &a_unchanged) -> std::optional<BudgetRadius> {
        return a_unchanged.radius;
      },
      [](const RadiusGrown &a_grown) -> std::optional<BudgetRadius> {
        return a_grown.radius;
      },
      [](const RadiusBlocked &a_blocked) -> std::optional<BudgetRadius> {
        return a_blocked.radius;
      },
      [](const RadiusCleared &) -> std::optional<BudgetRadius> {
        return std::nullopt;
      });
}

std::optional<ResolutionLevel>
KnownLevel(const ActorRecord &a_record) noexcept {
  const std::optional<ResolutionLevel> heading =
      a_record.render ? TargetLevel(a_record.render->phase) : std::nullopt;
  return heading ? heading : a_record.reinstallLevel;
}

std::optional<ResolutionLevel>
FirstLevel(const ActorRecord &a_record, const Sighting &a_sight,
           const TickSettings &a_settings) noexcept {
  if (const std::optional<ResolutionLevel> known = KnownLevel(a_record)) {
    return known;
  }
  if (!a_settings.sizeByScreen) {
    return std::nullopt;
  }
  return WantedLevel(
      {a_sight.screenFraction, a_sight.inView, ActorRole::kOther});
}

const Shown *ShownOf(const ActorRecord &a_record) noexcept {
  return a_record.render ? Get<Shown>(a_record.render->phase) : nullptr;
}

std::optional<ResolutionLevel> SelectStepUpLevel(const ActorRecord &a_record) {
  const Shown *shown = ShownOf(a_record);
  if (!shown || !shown->stepUpBlockedFrom) {
    return std::nullopt;
  }
  const std::optional<ResolutionLevel> wanted =
      WantedLevel(SightOf(a_record.sight));
  if (!wanted || LevelGap(*wanted, shown->level) <= 0) {
    return std::nullopt;
  }
  return wanted;
}

std::uint64_t BytesAtLevel(const InstalledRender &a_render,
                           ResolutionLevel a_level) noexcept {
  const std::optional<LevelCost> &cost = CostAt(a_render, a_level);
  return cost ? cost->withIntermediates : 0;
}

std::optional<BlockedStepUp> WaitingStepUpOf(ActorId a_actor,
                                             const ActorRecord &a_record,
                                             const BudgetState &a_budget) {
  const std::optional<ResolutionLevel> wanted = SelectStepUpLevel(a_record);
  if (!wanted || !a_record.render) {
    return std::nullopt;
  }
  const std::uint64_t added = BytesAtLevel(*a_record.render, *wanted);
  return BlockedStepUp{a_actor,
                       {SightOf(a_record.sight), ShortfallOf(a_budget, added)}};
}

bool LargerOnScreenFirst(const BlockedStepUp &a_left,
                         const BlockedStepUp &a_right) noexcept {
  return LargerOnScreen(a_left.request.sight.screenFraction,
                        a_right.request.sight.screenFraction);
}

bool Evicted(const ActorRecord &a_record) noexcept {
  const SyncState &state = a_record.syncState;
  return !a_record.render && state && !(state->plan == Actors::ActorPlan{}) &&
         !Actors::AnythingInstalled(state);
}

std::optional<EvictedActor> EvictedActorOf(const ActorRecord &a_record) {
  if (!Evicted(a_record)) {
    return std::nullopt;
  }
  return EvictedActor{a_record.sight.distance, a_record.returnBytes};
}

std::optional<float> TrackedDistance(const ActorRecord &a_record) noexcept {
  const std::optional<float> &distance = a_record.sight.distance;
  if (!a_record.syncState || !distance || !std::isfinite(*distance)) {
    return std::nullopt;
  }
  return distance;
}

void RespondToBudget(TickOutput &a_output, const TickFacts &a_facts) {
  const BudgetState &budget = a_output.world.budget;
  if (!budget.allowance) {
    return;
  }
  if (!IsOver(budget.pressure)) {
    MakeRoomForWaitingStepUp(a_output);
    return;
  }
  RelieveBudget(a_output, PressureGoal(*budget.allowance), a_facts);
}

void GrowBudgetRadius(TickOutput &a_output, const TickFacts &a_facts) {
  const World &world = a_output.world;
  if (!world.radius) {
    return;
  }
  const float evictDistance = a_facts.settings.evictDistance;
  const std::optional<float> farthest = LimitsDistance(evictDistance)
                                            ? std::nullopt
                                            : FarthestTrackedDistance(world);
  const std::vector<EvictedActor> evicted = EvictedActorsOf(world);
  const RadiusGrowth growth =
      GrowRadius({*world.radius, world.budget, PlannerMs(a_facts.now),
                  evictDistance, farthest, evicted, LargestReturnBytes(world)});
  ApplyRadius(a_output, RadiusAfter(growth));
  ReportRadiusGrowth(a_output, growth);
}
}

std::uint64_t PlannedBytesOf(const ActorRecord &a_record) noexcept {
  if (!Rendered(a_record)) {
    return 0;
  }
  const InstalledRender &render = *a_record.render;
  const std::optional<ResolutionLevel> applied = AppliedLevel(render.phase);
  if (!applied) {
    return 0;
  }
  const std::optional<LevelCost> &cost = CostAt(render, *applied);
  const std::uint64_t attached =
      cost ? PlannedBytes(*cost, HoldingOf(render.phase)) : 0;
  return Sum(attached, RunningResizeBytes(render));
}

std::uint64_t PlannedBytesOf(const World &a_world) noexcept {
  std::uint64_t planned = 0;
  for (const auto &[actor, record] : a_world.actors) {
    planned = Sum(planned, PlannedBytesOf(record));
  }
  return planned;
}

InstallBound InstallBoundOf(const ActorRecord &a_record,
                            const Sighting &a_sight,
                            const BudgetState &a_budget,
                            std::uint64_t a_replaced,
                            const TickSettings &a_settings) noexcept {
  if (a_sight.role == ActorRole::kPlayer) {
    return {KnownLevel(a_record).value_or(ResolutionLevel::kFull),
            std::nullopt};
  }

  const std::optional<ResolutionLevel> level =
      FirstLevel(a_record, a_sight, a_settings);
  if (!level) {
    return {};
  }
  return {*level, HeadroomOf(a_budget, a_replaced)};
}

std::optional<BudgetActor> BudgetActorOf(ActorId a_actor,
                                         const ActorRecord &a_record) noexcept {
  if (!Rendered(a_record)) {
    return std::nullopt;
  }
  const InstalledRender &render = *a_record.render;
  const std::optional<ResolutionLevel> level = AppliedLevel(render.phase);
  if (!level) {
    return std::nullopt;
  }

  const Sighting &sight = a_record.sight;
  const BudgetCosts costs = BudgetCostsOf(render, sight, *level);
  return BudgetActor{.actor = FormOf(a_actor),
                     .role = sight.role,
                     .inView = sight.inView,
                     .resizing = FindResize(render.phase) != nullptr,
                     .level = *level,
                     .screenFraction = sight.screenFraction,
                     .releasable = costs.releasable,
                     .stepDown = costs.stepDown,
                     .plannedBytes = costs.planned,
                     .distance = sight.distance,
                     .surplusBytes = costs.surplus};
}

BudgetActors BudgetActorsOf(const World &a_world) {
  return {.actors = Choose(a_world.actors,
                           [](const auto &a_entry) {
                             return BudgetActorOf(a_entry.first,
                                                  a_entry.second);
                           }),
          .resizeBusy = AnyResizing(a_world)};
}

std::vector<BlockedStepUp> WaitingStepUpsOf(const World &a_world) {
  std::vector<BlockedStepUp> blocked =
      Choose(a_world.actors, [&](const auto &a_entry) {
        return WaitingStepUpOf(a_entry.first, a_entry.second, a_world.budget);
      });
  std::ranges::stable_sort(blocked, LargerOnScreenFirst);
  return blocked;
}

std::vector<EvictedActor> EvictedActorsOf(const World &a_world) {
  return Choose(a_world.actors, [](const auto &a_entry) {
    return EvictedActorOf(a_entry.second);
  });
}

std::uint64_t LargestReturnBytes(const World &a_world) noexcept {
  return MaxOf(a_world.actors,
               [](const auto &a_entry) { return a_entry.second.returnBytes; })
      .value_or(0);
}

std::optional<float> FarthestTrackedDistance(const World &a_world) noexcept {
  return MaxOf(a_world.actors, [](const auto &a_entry) {
    return TrackedDistance(a_entry.second);
  });
}

TickOutput RunBudget(World a_world, const TickFacts &a_facts) {
  TickOutput output{std::move(a_world), {}, {}, {}};
  if (!a_facts.memory) {
    return output;
  }

  const BudgetSample &sample = *a_facts.memory;
  const BudgetState before = output.world.budget;
  const std::uint64_t held =
      sample.heldBytes.value_or(PlannedBytesOf(output.world));
  ApplyBudget(output, NextBudget(before, AllowanceOf(sample), held));
  RecordPressure(output, sample, before);

  RespondToBudget(output, a_facts);
  GrowBudgetRadius(output, a_facts);
  return output;
}
}
