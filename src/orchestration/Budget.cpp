// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Budget.h"

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

[[nodiscard]] std::uint64_t
SurplusBytesOf(const InstalledRender &a_render,
               const BudgetActor &a_actor) noexcept {
  const std::optional<ResolutionLevel> wanted =
      WantedLevel({a_actor.screenFraction, a_actor.inView, a_actor.role});
  if (!wanted || LevelGap(a_actor.level, *wanted) <= 0) {
    return 0;
  }
  const std::optional<LevelCost> &cost = CostAt(a_render, *wanted);
  return cost ? Less(a_actor.plannedBytes,
                     PlannedBytes(*cost, HoldingOf(a_render.phase)))
              : 0;
}

template <class T> void Append(std::vector<T> &a_into, std::vector<T> a_from) {
  a_into.insert(a_into.end(), std::make_move_iterator(a_from.begin()),
                std::make_move_iterator(a_from.end()));
}

void AddTrace(TickOutput &a_output, std::vector<Trace::Field> a_fields) {
  a_output.trace.push_back({Trace::Event::kMetrics, std::move(a_fields)});
}

void AddLog(TickOutput &a_output, LogLevel a_level, std::string a_text) {
  a_output.logs.push_back({a_level, std::move(a_text)});
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

void StepDownForBudget(TickOutput &a_output, ActorId a_actor,
                       const StepDownOption &a_option,
                       std::optional<ActorId> a_preemptedBy) {
  const auto found = a_output.world.actors.find(a_actor);
  if (found == a_output.world.actors.end() || !found->second.render) {
    return;
  }
  InstalledRender &render = *found->second.render;
  const std::optional<ResolutionLevel> from = AppliedLevel(render.phase);
  if (!from || !MayStepDown(render.phase)) {
    return;
  }
  PhaseChange change = WantLevel(a_actor, std::move(render.phase), a_option.to);
  render.phase = std::move(change.phase);
  Append(a_output.commands, std::move(change.commands));
  const std::optional<std::uint32_t> preemptedBy =
      a_preemptedBy ? std::optional<std::uint32_t>{FormOf(*a_preemptedBy)}
                    : std::nullopt;
  AddLog(a_output, LogLevel::kInfo,
         StepDownLine({FormOf(a_actor), found->second.sight.name, *from,
                       a_option, preemptedBy}));
  std::vector<Trace::Field> fields{
      {"action", "budget_step_down"},
      {"actor", FormText(a_actor)},
      {"from", std::string{LevelName(*from)}},
      {"to", std::string{LevelName(a_option.to)}},
      {"freed_bytes", std::to_string(a_option.freed)},
      {"reason", a_preemptedBy ? "preempted" : "render target budget"}};
  if (a_preemptedBy) {
    fields.push_back({"for_actor", FormText(*a_preemptedBy)});
  }
  AddTrace(a_output, std::move(fields));
}

void ReleaseOne(TickOutput &a_output, const BudgetRelease &a_release) {
  const auto found = a_output.world.actors.find(ActorOf(a_release.actor));
  if (found == a_output.world.actors.end() || !found->second.render) {
    return;
  }
  InstalledRender &render = *found->second.render;
  PhaseChange change =
      ReleaseForBudget(found->first, std::move(render.phase), a_release.bytes);
  render.phase = std::move(change.phase);
  Append(a_output.commands, std::move(change.commands));
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
  const auto record = a_output.world.actors.find(evicted);
  const std::string name =
      record != a_output.world.actors.end() ? record->second.sight.name : "";
  a_output.world.radius = shrink->radius;
  a_output.commands.push_back(Observe{evicted});
  AddLog(a_output, LogLevel::kInfo,
         RadiusShrinkLine(shrink->radius.distance, shrink->actor, name,
                          shrink->freed));
  AddTrace(a_output, {{"action", "budget_radius"},
                      {"change", "shrink"},
                      {"radius", RadiusText(shrink->radius.distance)},
                      {"actor", FormText(evicted)},
                      {"freed_bytes", std::to_string(shrink->freed)}});
  return true;
}

std::vector<ActorHolding> HoldingsByActor(const World &a_world) {
  std::vector<ActorHolding> holdings;
  for (const auto &[actor, record] : a_world.actors) {
    if (Rendered(record)) {
      holdings.push_back({FormOf(actor),
                          {},
                          PlannedBytesOf(record),
                          0,
                          record.sight.screenFraction,
                          {}});
    }
  }
  return holdings;
}

void RelieveBudget(TickOutput &a_output, std::uint64_t a_goal,
                   const TickFacts &a_facts) {
  const BudgetActors now = BudgetActorsOf(a_output.world);
  const BudgetResponse response = RespondToPressure(
      a_output.world.budget.planned, a_goal, now.actors, now.resizeBusy);
  for (const BudgetRelease &release : response.release) {
    ReleaseOne(a_output, release);
  }
  if (const StepDown *stepDown = Get<StepDown>(response.next)) {
    StepDownForBudget(a_output, ActorOf(stepDown->actor), stepDown->option,
                      std::nullopt);
  }
  if (Is<Exhausted>(response.next) &&
      !ShrinkBudgetRadius(a_output, now.actors, a_facts) &&
      a_output.world.budget.pressure == Pressure::kOver) {
    a_output.world.budget.pressure = Pressure::kOverWarned;
    AddLog(a_output, LogLevel::kWarning,
           ExhaustedLine(a_output.world.budget,
                         LargestHoldings(HoldingsByActor(a_output.world),
                                         kReportedHoldings)));
  }
}

void PreemptForBlockedStepUp(TickOutput &a_output) {
  const std::vector<BlockedStepUp> blocked = BlockedStepUpsOf(a_output.world);
  if (blocked.empty()) {
    return;
  }
  const BudgetActors now = BudgetActorsOf(a_output.world);
  if (now.resizeBusy) {
    return;
  }
  for (const BlockedStepUp &requester : blocked) {
    const std::optional<std::size_t> chosen =
        PreemptionCandidate(requester.request, now.actors);
    if (!chosen || *chosen >= now.actors.size()) {
      continue;
    }
    const BudgetActor &victim = now.actors[*chosen];
    if (victim.stepDown) {
      StepDownForBudget(a_output, ActorOf(victim.actor), *victim.stepDown,
                        requester.actor);
    }
    return;
  }
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
  a_output.world.radius = RadiusAfter(growth);
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
  std::optional<ResolutionLevel> level =
      a_record.render ? TargetLevel(a_record.render->phase) : std::nullopt;
  if (!level) {
    level = a_record.reinstallLevel;
  }
  if (a_sight.role == ActorRole::kPlayer) {
    return {level.value_or(ResolutionLevel::kFull), std::nullopt};
  }
  if (!level && !a_settings.sizeByScreen) {
    return {};
  }
  if (!level) {
    level = WantedLevel(
        {a_sight.screenFraction, a_sight.inView, ActorRole::kOther});
  }
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
  const std::optional<ResolutionLevel> applied = AppliedLevel(render.phase);
  if (!applied) {
    return std::nullopt;
  }
  BudgetActor actor{FormOf(a_actor),
                    a_record.sight.role,
                    a_record.sight.inView,
                    !MayEvict(render.phase),
                    *applied,
                    a_record.sight.screenFraction,
                    0,
                    std::nullopt,
                    0,
                    a_record.sight.distance};
  const std::optional<LevelCost> &current = CostAt(render, *applied);
  if (!current) {
    return actor;
  }
  actor.plannedBytes = PlannedBytes(*current, HoldingOf(render.phase));
  if (MayRelease(render.phase)) {
    actor.releasable = IntermediateBytes(*current);
  }
  actor.stepDown = StepDownFor(render, *applied, *current);
  actor.surplusBytes = SurplusBytesOf(render, actor);
  return actor;
}

BudgetActors BudgetActorsOf(const World &a_world) {
  BudgetActors now;
  for (const auto &[id, record] : a_world.actors) {
    now.resizeBusy =
        now.resizeBusy || (record.render && ResizeIn(record.render->phase));
    if (const std::optional<BudgetActor> actor = BudgetActorOf(id, record)) {
      now.actors.push_back(*actor);
    }
  }
  return now;
}

std::vector<BlockedStepUp> BlockedStepUpsOf(const World &a_world) {
  std::vector<BlockedStepUp> blocked;
  for (const auto &[id, record] : a_world.actors) {
    if (!record.render) {
      continue;
    }
    const Shown *shown = Get<Shown>(record.render->phase);
    if (!shown || !shown->stepUpBlockedFrom) {
      continue;
    }
    const ActorSight sight = SightOf(record.sight);
    const std::optional<ResolutionLevel> wanted = WantedLevel(sight);
    if (!sight.inView || !wanted || LevelGap(*wanted, shown->level) <= 0) {
      continue;
    }
    const std::optional<LevelCost> &cost = CostAt(*record.render, *wanted);
    const std::uint64_t added = cost ? cost->withIntermediates : 0;
    blocked.push_back({id, {sight, ShortfallOf(a_world.budget, added)}});
  }
  std::ranges::stable_sort(
      blocked, [](const BlockedStepUp &a_left, const BlockedStepUp &a_right) {
        return LargerOnScreen(a_left.request.sight.screenFraction,
                              a_right.request.sight.screenFraction);
      });
  return blocked;
}

std::vector<EvictedActor> EvictedActorsOf(const World &a_world) {
  std::vector<EvictedActor> evicted;
  for (const auto &[id, record] : a_world.actors) {
    const std::optional<Actors::ActorState> &state = record.syncState;
    if (record.render || !state || state->plan == Actors::ActorPlan{} ||
        Actors::AnythingInstalled(state)) {
      continue;
    }
    evicted.push_back({record.sight.distance, record.returnBytes});
  }
  return evicted;
}

std::uint64_t LargestReturnBytes(const World &a_world) noexcept {
  std::uint64_t largest = 0;
  for (const auto &[id, record] : a_world.actors) {
    largest = std::max(largest, record.returnBytes.value_or(0));
  }
  return largest;
}

std::optional<float> FarthestTrackedDistance(const World &a_world) noexcept {
  std::optional<float> farthest;
  for (const auto &[id, record] : a_world.actors) {
    const std::optional<float> &distance = record.sight.distance;
    if (record.syncState && distance && std::isfinite(*distance) &&
        (!farthest || *distance > *farthest)) {
      farthest = distance;
    }
  }
  return farthest;
}

TickOutput RunBudget(World a_world, const TickFacts &a_facts) {
  TickOutput output{std::move(a_world), {}, {}, {}};
  if (!a_facts.memory) {
    return output;
  }
  const BudgetSample &sample = *a_facts.memory;
  const BudgetState before = output.world.budget;
  output.world.budget =
      NextBudget(before, AllowanceOf(sample),
                 sample.heldBytes.value_or(PlannedBytesOf(output.world)));
  RecordPressure(output, sample, before);
  const BudgetState &budget = output.world.budget;
  if (IsOver(budget.pressure) && budget.allowance) {
    RelieveBudget(output, PressureGoal(*budget.allowance), a_facts);
  } else if (budget.allowance) {
    PreemptForBlockedStepUp(output);
  }
  GrowBudgetRadius(output, a_facts);
  return output;
}
}
