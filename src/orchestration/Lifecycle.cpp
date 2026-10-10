// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Lifecycle.h"

#include "orchestration/PhaseChanges.h"
#include "orchestration/Tick.h"
#include "planners/DrawPacing.h"
#include "planners/ResolutionResize.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
namespace {
constexpr std::string_view kNoPhase = "none";
constexpr std::uint64_t kMicrosPerMilli = 1000;

struct ScreenStep {
  ResolutionLevel from = ResolutionLevel::kFull;
  ResolutionLevel to = ResolutionLevel::kFull;
  bool up = false;
};

std::string ActorText(ActorId a_actor) {
  return std::to_string(FormOf(a_actor));
}

std::string FractionText(std::optional<float> a_fraction) {
  return a_fraction ? std::format("{:.3f}", *a_fraction)
                    : std::string{"unmeasured"};
}

TraceRecord Metrics(std::vector<Trace::Field> a_fields) {
  return {Trace::Event::kMetrics, std::move(a_fields)};
}

TraceRecord PhaseTrace(ActorId a_actor, std::string_view a_from,
                       std::string_view a_to, std::string_view a_event) {
  return Metrics({{"action", "actor_phase"},
                  {"actor", ActorText(a_actor)},
                  {"from", std::string{a_from}},
                  {"to", std::string{a_to}},
                  {"event", std::string{a_event}}});
}

TraceRecord IgnoredTrace(ActorId a_actor, std::string_view a_phase,
                         std::string_view a_event) {
  return Metrics({{"action", "actor_phase_ignored"},
                  {"actor", ActorText(a_actor)},
                  {"phase", std::string{a_phase}},
                  {"event", std::string{a_event}}});
}

void Append(std::vector<Command> &a_into, std::vector<Command> a_from) {
  a_into.insert(a_into.end(), std::make_move_iterator(a_from.begin()),
                std::make_move_iterator(a_from.end()));
}

void Apply(TickOutput &a_out, ActorId a_actor, InstalledRender &a_render,
           PhaseChange a_change, std::string_view a_event) {
  const std::string_view before = PhaseName(a_render.phase);
  if (a_change.ignored) {
    a_out.trace.push_back(IgnoredTrace(a_actor, before, a_event));
  }
  a_render.phase = std::move(a_change.phase);
  if (const std::string_view after = PhaseName(a_render.phase);
      after != before) {
    a_out.trace.push_back(PhaseTrace(a_actor, before, after, a_event));
  }
  Append(a_out.commands, std::move(a_change.commands));
}

bool RenderLoaded(const ActorRecord &a_record) noexcept {
  return a_record.render.has_value() && a_record.sight.loaded3D;
}

PerLevel<std::optional<LevelCost>> CostsOf(const RenderEvent &a_event) {
  const Installed *installed = Get<Installed>(a_event);
  return installed ? installed->levelCosts
                   : PerLevel<std::optional<LevelCost>>{};
}

void RecordSightings(World &a_world,
                     const std::map<ActorId, Sighting> &a_sightings) {
  for (const auto &[actor, sighting] : a_sightings) {
    if (const auto found = a_world.actors.find(actor);
        found != a_world.actors.end()) {
      found->second.sight = sighting;
    }
  }
}

void CreateRender(TickOutput &a_out, const ActorEvent &a_event,
                  std::uint64_t a_frame) {
  const std::string_view event = EventName(a_event.event);
  std::optional<PhaseChange> created =
      PhaseFor(a_event.actor, a_event.event, a_frame);
  if (!created) {
    a_out.trace.push_back(IgnoredTrace(a_event.actor, kNoPhase, event));
    return;
  }
  ActorRecord &record = a_out.world.actors[a_event.actor];
  record.render = InstalledRender{std::move(created->phase),
                                  CostsOf(a_event.event), std::nullopt};
  a_out.trace.push_back(PhaseTrace(a_event.actor, kNoPhase,
                                   PhaseName(record.render->phase), event));
  Append(a_out.commands, std::move(created->commands));
}

void RecordEvent(TickOutput &a_out, const ActorEvent &a_event,
                 std::uint64_t a_frame) {
  const auto found = a_out.world.actors.find(a_event.actor);
  if (found == a_out.world.actors.end() || !found->second.render) {
    CreateRender(a_out, a_event, a_frame);
    return;
  }
  InstalledRender &render = *found->second.render;
  const EventContext context{a_frame,
                             WantedLevel(ActorSightOf(found->second.sight))};
  PhaseChange change =
      OnEvent(a_event.actor, render.phase, a_event.event, context);
  if (!change.ignored) {
    if (const Installed *installed = Get<Installed>(a_event.event)) {
      render.levelCosts = installed->levelCosts;
    }
  }
  Apply(a_out, a_event.actor, render, std::move(change),
        EventName(a_event.event));
}

PlanJobId JobOf(const PlanJobDone &a_done) noexcept {
  return Match(
      a_done, [](const PlanJobBuilt &a_built) { return a_built.job; },
      [](const PlanJobFailed &a_failed) { return a_failed.job; });
}

void RecordJob(TickOutput &a_out, const PlanJobDone &a_done) {
  const PlanJobId job = JobOf(a_done);
  for (auto &[actor, record] : a_out.world.actors) {
    if (!record.render || AwaitedJob(record.render->phase) != job) {
      continue;
    }
    InstalledRender &render = *record.render;
    if (const PlanJobFailed *failed = Get<PlanJobFailed>(a_done)) {
      Apply(a_out, actor, render, FailPlan(actor, render.phase, *failed),
            "plan_failed");
      continue;
    }
    Apply(a_out, actor, render,
          PlanBuilt(actor, render.phase, job,
                    InstallBoundFor(record, a_out.world.budget)),
          "plan_built");
  }
}

void RecordRenderedFrames(World &a_world, std::uint64_t a_frame) {
  for (auto &[actor, record] : a_world.actors) {
    if (RenderLoaded(record) && record.sight.inView) {
      record.render->renderedFrame = a_frame;
    }
  }
}

std::uint64_t ReleasableBytes(const InstalledRender &a_render) noexcept {
  const std::optional<ResolutionLevel> level = AppliedLevel(a_render.phase);
  if (!level) {
    return 0;
  }
  const std::optional<LevelCost> &cost = a_render.levelCosts.At(*level);
  return cost ? IntermediateBytes(*cost) : 0;
}

void See(TickOutput &a_out, ActorId a_actor, ActorRecord &a_record,
         const TickFacts &a_facts) {
  InstalledRender &render = *a_record.render;
  const ActorSight sight = ActorSightOf(a_record.sight);
  const std::optional<ResolutionLevel> target = TargetLevel(render.phase);
  const SeenThisTick seen{sight.inView,
                          target && SmallFor(sight.screenFraction, *target),
                          WantedLevel(sight),
                          a_facts.now,
                          a_facts.settings.outOfViewReleaseDelayMs,
                          ReleasableBytes(render)};
  Apply(a_out, a_actor, render, SeeActor(a_actor, render.phase, seen), "seen");
}

std::optional<ScreenStep> ScreenStepOf(const InstalledRender &a_render,
                                       const ActorSight &a_sight,
                                       const TickFacts &a_facts) noexcept {
  const std::optional<ResolutionLevel> current = TargetLevel(a_render.phase);
  if (!current) {
    return std::nullopt;
  }
  const LevelStep step =
      LevelStepOf(*current, a_sight,
                  ScreenStepDownDue(a_render.phase, a_facts.now,
                                    a_facts.settings.screenStepDownHoldMs));
  if (const StepUpTo *up = Get<StepUpTo>(step)) {
    return ScreenStep{*current, up->level, true};
  }
  if (const StepDownTo *down = Get<StepDownTo>(step)) {
    return ScreenStep{*current, down->level, false};
  }
  return std::nullopt;
}

void BlockStepUp(TickOutput &a_out, ActorId a_actor, InstalledRender &a_render,
                 const ScreenStep &a_step, Admission a_admission,
                 std::uint64_t a_added) {
  Live *live = Get<Live>(a_render.phase);
  if (!live || live->stepUpBlockedFrom == a_step.from) {
    return;
  }
  live->stepUpBlockedFrom = a_step.from;
  const BudgetState &budget = a_out.world.budget;
  a_out.trace.push_back(Metrics(
      {{"action", "resolution_blocked"},
       {"actor", ActorText(a_actor)},
       {"from", std::string{LevelName(a_step.from)}},
       {"to", std::string{LevelName(a_step.to)}},
       {"reason", std::string{AdmissionName(a_admission)}},
       {"added_bytes", std::to_string(a_added)},
       {"planned_bytes", std::to_string(budget.planned)},
       {"allowance_bytes", std::to_string(budget.allowance.value_or(0))}}));
}

bool AdmitStepUp(TickOutput &a_out, ActorId a_actor, InstalledRender &a_render,
                 const ScreenStep &a_step) {
  const std::optional<LevelCost> &cost = a_render.levelCosts.At(a_step.to);
  const std::uint64_t added = cost ? cost->withIntermediates : 0;
  const Admission admission = AdmitGrowth(a_out.world.budget, added);
  if (admission != Admission::kAdmitted) {
    BlockStepUp(a_out, a_actor, a_render, a_step, admission, added);
    return false;
  }
  a_out.world.budget = Charged(a_out.world.budget, added);
  return true;
}

void StepLevel(TickOutput &a_out, ActorId a_actor, ActorRecord &a_record,
               const TickFacts &a_facts) {
  InstalledRender &render = *a_record.render;
  const ActorSight sight = ActorSightOf(a_record.sight);
  const std::optional<ScreenStep> step = ScreenStepOf(render, sight, a_facts);
  if (!step || FailedLevelOf(render.phase) == step->to) {
    return;
  }
  if (step->up && sight.role != ActorRole::kPlayer &&
      !AdmitStepUp(a_out, a_actor, render, *step)) {
    return;
  }
  a_out.trace.push_back(
      Metrics({{"action", "resolution_level"},
               {"actor", ActorText(a_actor)},
               {"from", std::string{LevelName(step->from)}},
               {"to", std::string{LevelName(step->to)}},
               {"reason", "screen"},
               {"screen_fraction", FractionText(sight.screenFraction)}}));
  Apply(a_out, a_actor, render, WantLevel(a_actor, render.phase, step->to),
        "level_wanted");
}

std::set<ActorId> ScheduleResizes(TickOutput &a_out, std::uint64_t a_frame) {
  std::set<ActorId> swapping;
  std::size_t running = 0;
  std::vector<ResizeCandidate> candidates;
  for (const auto &[actor, record] : a_out.world.actors) {
    if (!record.render) {
      continue;
    }
    const RenderPhase &phase = record.render->phase;
    const std::optional<ResizeProgress> progress = ResizeProgressOf(phase);
    const UnpublishedDraw *draw = UnpublishedDrawOf(phase);
    if (progress && draw && record.sight.loaded3D &&
        SwapDue(*progress,
                DrawOf({record.sight.inView, draw->drawn, draw->total}),
                a_frame)) {
      a_out.commands.push_back(SwapResize{actor});
      swapping.insert(actor);
    } else if (progress) {
      ++running;
    }
    const Resizing *resizing = Get<Resizing>(phase);
    if (record.sight.loaded3D && resizing &&
        Is<ResizeQueued>(resizing->stage)) {
      candidates.push_back({FormOf(actor), true, record.sight.inView,
                            record.sight.screenFraction});
    }
  }
  const std::optional<std::uint32_t> next = NextResize(candidates, running);
  if (!next) {
    return swapping;
  }
  const auto found = a_out.world.actors.find(ActorOf(*next));
  if (found == a_out.world.actors.end() || !found->second.render) {
    return swapping;
  }
  if (const std::optional<ResolutionLevel> to =
          TargetLevel(found->second.render->phase)) {
    a_out.commands.push_back(StartResize{found->first, *to});
  }
  return swapping;
}

void FinishFirstDraw(TickOutput &a_out, ActorId a_actor,
                     InstalledRender &a_render, std::uint64_t a_frame) {
  const FirstDrawing *drawing = Get<FirstDrawing>(a_render.phase);
  if (!drawing) {
    return;
  }
  const UnpublishedDraw &draw = drawing->draw;
  const FirstDrawProgress progress{draw.drawTicks, draw.total, draw.drawn};
  const std::optional<FirstDrawEnd> end = FirstDrawDone(progress);
  if (!end) {
    return;
  }
  a_out.trace.push_back(Metrics(
      {{"action", "first_draw"},
       {"actor", ActorText(a_actor)},
       {"end", std::string{FirstDrawEndName(*end)}},
       {"frames", std::to_string(a_frame - std::min(a_frame, draw.beganFrame))},
       {"draw_ticks", std::to_string(progress.drawTicks)},
       {"stacks", std::to_string(progress.stacks)},
       {"drawn", std::to_string(progress.drawn)}}));
  Apply(a_out, a_actor, a_render, EndFirstDraw(a_render.phase),
        "first_draw_ended");
}

std::optional<DrawUnpublished>
UnpublishedDraws(const World &a_world, const std::set<ActorId> &a_swapping,
                 const TickSettings &a_settings) {
  std::vector<UnpublishedDrawCandidate> candidates;
  for (const auto &[actor, record] : a_world.actors) {
    if (RenderLoaded(record) && record.sight.inView &&
        UnpublishedDrawOf(record.render->phase) &&
        !a_swapping.contains(actor)) {
      candidates.push_back(
          {FormOf(actor), record.sight.role, record.sight.screenFraction});
    }
  }
  if (candidates.empty()) {
    return std::nullopt;
  }
  DrawUnpublished draw{
      {}, std::uint64_t{a_settings.unpublishedDrawBudgetMs} * kMicrosPerMilli};
  for (const std::uint32_t actor : UnpublishedDrawOrder(candidates)) {
    draw.order.push_back(ActorOf(actor));
  }
  return draw;
}
}

ActorSight ActorSightOf(const Sighting &a_sight) noexcept {
  return {a_sight.screenFraction, a_sight.inView, a_sight.role};
}

InstallBound InstallBoundFor(const ActorRecord &a_record,
                             const BudgetState &a_budget) noexcept {
  const ActorSight sight = ActorSightOf(a_record.sight);
  if (sight.role == ActorRole::kPlayer) {
    return {a_record.reinstallLevel.value_or(ResolutionLevel::kFull),
            std::nullopt};
  }
  const std::optional<ResolutionLevel> level =
      a_record.reinstallLevel ? a_record.reinstallLevel : WantedLevel(sight);
  if (!level) {
    return {};
  }
  return {*level, HeadroomOf(a_budget, 0)};
}

TickActor TickActorFor(ActorId a_actor, const RenderPhase &a_phase,
                       const Sighting &a_sight,
                       const TickSettings &a_settings) noexcept {
  const bool firstDrawing = Is<FirstDrawing>(a_phase);
  return {a_actor,
          a_sight.inView,
          firstDrawing,
          !a_settings.publishEffects || firstDrawing ||
              Is<AwaitingPlan>(a_phase),
          firstDrawing,
          !firstDrawing || !a_sight.inView};
}

std::string_view EventName(const RenderEvent &a_event) noexcept {
  return Match(
      a_event,
      [](const PlanAwaited &) { return std::string_view{"plan_awaited"}; },
      [](const Installed &) { return std::string_view{"installed"}; },
      [](const InstalledWithoutRender &) {
        return std::string_view{"installed_without_render"};
      },
      [](const UnpublishedDrawn &) {
        return std::string_view{"unpublished_drawn"};
      },
      [](const ResizeStarted &) { return std::string_view{"resize_started"}; },
      [](const ResizeNotStarted &) {
        return std::string_view{"resize_not_started"};
      },
      [](const ResizeSwapped &) { return std::string_view{"resize_swapped"}; },
      [](const ResizeNotSwapped &) {
        return std::string_view{"resize_not_swapped"};
      });
}

TickOutput RecordRenderResults(World a_world, const TickFacts &a_facts) {
  TickOutput out{std::move(a_world), {}, {}, {}};
  RecordSightings(out.world, a_facts.sightings);
  for (const CommandResult &result : a_facts.results) {
    if (const ActorEvent *event = Get<ActorEvent>(result)) {
      RecordEvent(out, *event, a_facts.frame);
    }
  }
  for (const PlanJobDone &done : a_facts.jobs) {
    RecordJob(out, done);
  }
  RecordRenderedFrames(out.world, a_facts.frame);
  return out;
}

TickOutput RunLifecycle(World a_world, const TickFacts &a_facts) {
  TickOutput out{std::move(a_world), {}, {}, {}};
  for (auto &[actor, record] : out.world.actors) {
    if (RenderLoaded(record)) {
      See(out, actor, record, a_facts);
      StepLevel(out, actor, record, a_facts);
    }
  }
  const std::set<ActorId> swapping = ScheduleResizes(out, a_facts.frame);
  for (auto &[actor, record] : out.world.actors) {
    if (RenderLoaded(record)) {
      FinishFirstDraw(out, actor, *record.render, a_facts.frame);
    }
  }
  for (const auto &[actor, record] : out.world.actors) {
    if (RenderLoaded(record)) {
      out.commands.push_back(TickActorFor(actor, record.render->phase,
                                          record.sight, a_facts.settings));
    }
  }
  if (std::optional<DrawUnpublished> draw =
          UnpublishedDraws(out.world, swapping, a_facts.settings)) {
    out.commands.push_back(std::move(*draw));
  }
  return out;
}
}
