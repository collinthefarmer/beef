// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Lifecycle.h"
#include "orchestration/Output.h"

#include "orchestration/Budget.h"
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

struct ScheduledResizes {
  ActorSet swapping;
  MaybeActor starting;

  [[nodiscard]] bool Includes(ActorId a_actor) const {
    return swapping.contains(a_actor) || starting == a_actor;
  }
};

bool RenderLoaded(const TrackedActor &a_record) noexcept {
  return a_record.render.has_value() && a_record.sight.loaded3D;
}

void RecordSightings(World &a_world,
                     const std::map<ActorId, Sighting> &a_sightings);
std::vector<ActorEvent>
SelectCurrentEvents(const std::vector<CommandResult> &a_results);
void RecordEvent(TickOutput &a_out, const ActorEvent &a_event,
                 std::uint64_t a_frame);
void CreateRender(TickOutput &a_out, const ActorEvent &a_event,
                  std::uint64_t a_frame);
PerLevel<std::optional<LevelCost>> CostsOf(const RenderEvent &a_event);
void KeepInstalledCosts(InstalledRender &a_render, const PhaseChange &a_change,
                        const RenderEvent &a_event);
std::uint64_t RestagedBytes(const RenderPhase &a_phase,
                            const RenderEvent &a_event) noexcept;
void ReleaseRestaged(TickOutput &a_out, const PhaseChange &a_change,
                     std::uint64_t a_bytes);
void RecordJob(TickOutput &a_out, const PlanJobDone &a_done,
               const TickSettings &a_settings);
bool AwaitsJob(const TrackedActor &a_record, PlanJobId a_job) noexcept;
void ApplyJob(TickOutput &a_out, ActorId a_actor, TrackedActor &a_record,
              const PlanJobDone &a_done, const TickSettings &a_settings);
void RecordRenderedFrames(World &a_world, std::uint64_t a_frame);
ScheduledResizes ScheduleResizes(TickOutput &a_out, std::uint64_t a_frame);
Command SwapCommandFor(ActorId a_actor, const World &a_world);
bool SwapDueFor(const TrackedActor &a_record, std::uint64_t a_frame);
std::optional<ResizeProgress> SwapProgressOf(const RenderPhase &a_phase);
ActorSet SwapsDue(const World &a_world, std::uint64_t a_frame);
std::size_t RunningResizes(const World &a_world, const ActorSet &a_swapping);
std::optional<ResizeCandidate> ResizeCandidateOf(ActorId a_actor,
                                                 const TrackedActor &a_record);
MaybeActor StartNextResize(TickOutput &a_out, std::size_t a_running);
void SeeActors(TickOutput &a_out, const ScheduledResizes &a_scheduled,
               const TickFacts &a_facts);
void See(TickOutput &a_out, ActorId a_actor, TrackedActor &a_record,
         const TickFacts &a_facts);
std::uint64_t ReleasableBytes(const InstalledRender &a_render) noexcept;
void StepLevel(TickOutput &a_out, ActorId a_actor, TrackedActor &a_record,
               const TickFacts &a_facts);
std::optional<ScreenStep> ScreenStepOf(const InstalledRender &a_render,
                                       const ActorVisibility &a_visibility,
                                       const TickFacts &a_facts) noexcept;
bool AdmitStepUp(TickOutput &a_out, ActorId a_actor, InstalledRender &a_render,
                 const ScreenStep &a_step);
void BlockStepUp(TickOutput &a_out, ActorId a_actor, InstalledRender &a_render,
                 const ScreenStep &a_step, Admission a_admission,
                 std::uint64_t a_added);
void ReportStepUpBlocked(TickOutput &a_out, ActorId a_actor,
                         const ScreenStep &a_step, Admission a_admission,
                         std::uint64_t a_added);
void ReportLevelStep(TickOutput &a_out, ActorId a_actor,
                     const ScreenStep &a_step,
                     const ActorVisibility &a_visibility);
std::string FractionText(std::optional<float> a_fraction);
void FinishFirstDraws(TickOutput &a_out, std::uint64_t a_frame);
void FinishFirstDraw(TickOutput &a_out, ActorId a_actor,
                     InstalledRender &a_render, std::uint64_t a_frame);
void ReportFirstDraw(TickOutput &a_out, ActorId a_actor,
                     const UnpublishedDraw &a_draw, FirstDrawEnd a_end,
                     std::uint64_t a_frame);
void IssueTickActors(TickOutput &a_out, const TickSettings &a_settings);
bool IsRestageUnanswered(const RenderPhase &a_phase) noexcept;
std::optional<DrawUnpublished> UnpublishedDraws(const World &a_world,
                                                const ActorSet &a_swapping,
                                                const TickSettings &a_settings);
std::optional<UnpublishedDrawCandidate>
UnpublishedCandidateOf(ActorId a_actor, const TrackedActor &a_record,
                       const ActorSet &a_swapping);
}

TickOutput RecordRenderResults(World a_world, const TickFacts &a_facts) {
  TickOutput out{std::move(a_world), {}, {}, {}};
  RecordSightings(out.world, a_facts.sightings);

  for (const ActorEvent &event : SelectCurrentEvents(a_facts.results)) {
    RecordEvent(out, event, a_facts.frame);
  }

  for (const PlanJobDone &done : a_facts.jobs) {
    RecordJob(out, done, a_facts.settings);
  }

  RecordRenderedFrames(out.world, a_facts.frame);
  return out;
}

namespace {
void RecordSightings(World &a_world,
                     const std::map<ActorId, Sighting> &a_sightings) {
  for (const auto &[actor, sighting] : a_sightings) {
    if (TrackedActor *record = FindActor(a_world, actor)) {
      record->sight = sighting;
    }
  }
}

std::vector<ActorEvent>
SelectCurrentEvents(const std::vector<CommandResult> &a_results) {
  std::set<ActorId> retiredLater;
  std::vector<ActorEvent> current;
  for (auto result = a_results.rbegin(); result != a_results.rend(); ++result) {
    if (const Retired *retired = Get<Retired>(*result)) {
      retiredLater.insert(retired->actor);
      continue;
    }
    const ActorEvent *event = Get<ActorEvent>(*result);
    if (!event || retiredLater.contains(event->actor)) {
      continue;
    }
    current.push_back(*event);
  }
  std::ranges::reverse(current);
  return current;
}

void RecordEvent(TickOutput &a_out, const ActorEvent &a_event,
                 std::uint64_t a_frame) {
  TrackedActor *record = FindActor(a_out.world, a_event.actor);
  if (!record || !record->render) {
    CreateRender(a_out, a_event, a_frame);
    return;
  }

  InstalledRender &render = *record->render;
  const EventContext context{a_frame, LevelFor(VisibilityOf(record->sight))};
  PhaseChange change =
      OnEvent(a_event.actor, render.phase, a_event.event, context);
  const std::uint64_t restaged = RestagedBytes(render.phase, a_event.event);

  KeepInstalledCosts(render, change, a_event.event);
  ReleaseRestaged(a_out, change, restaged);
  ApplyPhaseChange(a_out, a_event.actor, render, std::move(change),
                   EventName(a_event.event));
}

void CreateRender(TickOutput &a_out, const ActorEvent &a_event,
                  std::uint64_t a_frame) {
  const std::string_view event = EventName(a_event.event);
  std::optional<PhaseChange> created =
      PhaseFor(a_event.actor, a_event.event, a_frame);
  if (!created) {
    TraceIgnored(a_out, a_event.actor, kNoPhase, event);
    return;
  }

  TrackedActor &record = a_out.world.actors[a_event.actor];
  record.render =
      InstalledRender{created->phase, CostsOf(a_event.event), std::nullopt};

  TracePhase(a_out, a_event.actor, kNoPhase, PhaseName(record.render->phase),
             event);
  Append(a_out.commands, std::move(created->commands));
}

PerLevel<std::optional<LevelCost>> CostsOf(const RenderEvent &a_event) {
  const Installed *installed = Get<Installed>(a_event);
  return installed ? installed->levelCosts
                   : PerLevel<std::optional<LevelCost>>{};
}

void KeepInstalledCosts(InstalledRender &a_render, const PhaseChange &a_change,
                        const RenderEvent &a_event) {
  const Installed *installed = Get<Installed>(a_event);
  if (a_change.ignored || !installed) {
    return;
  }
  a_render.levelCosts = installed->levelCosts;
}

std::uint64_t RestagedBytes(const RenderPhase &a_phase,
                            const RenderEvent &a_event) noexcept {
  const Restaging *restaging = Get<Restaging>(a_phase);
  if (!restaging) {
    return 0;
  }
  if (Is<RestageSwapped>(a_event)) {
    return restaging->shownBytes;
  }
  return Is<RestageNotSwapped>(a_event) ? StagedBytesOf(*restaging) : 0;
}

void ReleaseRestaged(TickOutput &a_out, const PhaseChange &a_change,
                     std::uint64_t a_bytes) {
  if (a_change.ignored || a_bytes == 0) {
    return;
  }
  ApplyBudget(a_out, Released(a_out.world.budget, a_bytes));
}

void RecordJob(TickOutput &a_out, const PlanJobDone &a_done,
               const TickSettings &a_settings) {
  const PlanJobId job = JobOf(a_done);
  for (auto &[actor, record] : a_out.world.actors) {
    if (AwaitsJob(record, job)) {
      ApplyJob(a_out, actor, record, a_done, a_settings);
    }
  }
}

bool AwaitsJob(const TrackedActor &a_record, PlanJobId a_job) noexcept {
  return a_record.render && AwaitedJob(a_record.render->phase) == a_job;
}

void ApplyJob(TickOutput &a_out, ActorId a_actor, TrackedActor &a_record,
              const PlanJobDone &a_done, const TickSettings &a_settings) {
  InstalledRender &render = *a_record.render;
  if (const PlanJobFailed *failed = Get<PlanJobFailed>(a_done)) {
    ApplyPhaseChange(a_out, a_actor, render,
                     FailPlan(a_actor, render.phase, *failed), "plan_failed");
    return;
  }

  const InstallBounds bounds = InstallBoundsOf(
      a_record, a_record.sight, a_out.world.budget, 0, a_settings);
  ApplyPhaseChange(a_out, a_actor, render,
                   PlanBuilt(a_actor, render.phase, JobOf(a_done), bounds),
                   "plan_built");
}

void RecordRenderedFrames(World &a_world, std::uint64_t a_frame) {
  for (auto &[actor, record] : a_world.actors) {
    if (RenderLoaded(record) && record.sight.shown) {
      record.render->renderedFrame = a_frame;
    }
  }
}
}

TickOutput RunLifecycle(World a_world, const TickFacts &a_facts) {
  TickOutput out{std::move(a_world), {}, {}, {}};
  const ScheduledResizes scheduled = ScheduleResizes(out, a_facts.frame);

  SeeActors(out, scheduled, a_facts);
  FinishFirstDraws(out, a_facts.frame);
  IssueTickActors(out, a_facts.settings);

  if (std::optional<DrawUnpublished> draw =
          UnpublishedDraws(out.world, scheduled.swapping, a_facts.settings)) {
    Issue(out, std::move(*draw));
  }
  return out;
}

namespace {
ScheduledResizes ScheduleResizes(TickOutput &a_out, std::uint64_t a_frame) {
  const ActorSet swapping = SwapsDue(a_out.world, a_frame);
  for (const ActorId actor : swapping) {
    Issue(a_out, SwapCommandFor(actor, a_out.world));
  }

  const MaybeActor starting =
      StartNextResize(a_out, RunningResizes(a_out.world, swapping));
  return {swapping, starting};
}

Command SwapCommandFor(ActorId a_actor, const World &a_world) {
  const TrackedActor *record = FindActor(a_world, a_actor);
  if (record && record->render && Is<Restaging>(record->render->phase)) {
    return SwapRestage{a_actor};
  }
  return SwapResize{a_actor};
}

bool SwapDueFor(const TrackedActor &a_record, std::uint64_t a_frame) {
  if (!a_record.render || !a_record.sight.loaded3D) {
    return false;
  }
  const RenderPhase &phase = a_record.render->phase;
  const std::optional<ResizeProgress> progress = SwapProgressOf(phase);
  const UnpublishedDraw *draw = UnpublishedDrawOf(phase);
  if (!progress || !draw) {
    return false;
  }
  const ResizeDraw seen =
      DrawOf({a_record.sight.shown, draw->drawn, draw->total});
  return SwapDue(*progress, seen, a_frame);
}

std::optional<ResizeProgress> SwapProgressOf(const RenderPhase &a_phase) {
  const RestageDrawing *staged = RestageDrawingOf(a_phase);
  if (!staged) {
    return ResizeProgressOf(a_phase);
  }
  const Restaging &restaging = *Get<Restaging>(a_phase);
  return ResizeProgress{restaging.shown.level, staged->level,
                        staged->draw.beganFrame};
}

ActorSet SwapsDue(const World &a_world, std::uint64_t a_frame) {
  ActorSet due;
  for (const auto &[actor, record] : a_world.actors) {
    if (SwapDueFor(record, a_frame)) {
      due.insert(actor);
    }
  }
  return due;
}

std::size_t RunningResizes(const World &a_world, const ActorSet &a_swapping) {
  return static_cast<std::size_t>(
      std::ranges::count_if(a_world.actors, [&](const auto &a_entry) {
        const auto &[actor, record] = a_entry;
        return record.render && ResizeProgressOf(record.render->phase) &&
               !a_swapping.contains(actor);
      }));
}

std::optional<ResizeCandidate> ResizeCandidateOf(ActorId a_actor,
                                                 const TrackedActor &a_record) {
  if (!a_record.render || !a_record.sight.loaded3D ||
      !Is<Queued>(a_record.render->phase)) {
    return std::nullopt;
  }
  return ResizeCandidate{.actor = FormOf(a_actor),
                         .pending = true,
                         .shown = a_record.sight.shown,
                         .screenFraction = a_record.sight.screenFraction};
}

MaybeActor StartNextResize(TickOutput &a_out, std::size_t a_running) {
  const std::vector<ResizeCandidate> candidates =
      Choose(a_out.world.actors, [](const auto &a_entry) {
        return ResizeCandidateOf(a_entry.first, a_entry.second);
      });
  const std::optional<std::uint32_t> next = NextResize(candidates, a_running);
  if (!next) {
    return std::nullopt;
  }
  const ActorId actor = ActorOf(*next);
  const TrackedActor *record = FindActor(a_out.world, actor);
  if (!record || !record->render) {
    return std::nullopt;
  }
  const std::optional<ResolutionLevel> to = TargetLevel(record->render->phase);
  if (!to) {
    return std::nullopt;
  }
  Issue(a_out, StartResize{actor, *to});
  return actor;
}

void SeeActors(TickOutput &a_out, const ScheduledResizes &a_scheduled,
               const TickFacts &a_facts) {
  for (auto &[actor, record] : a_out.world.actors) {
    if (!RenderLoaded(record)) {
      continue;
    }
    See(a_out, actor, record, a_facts);
    if (!a_scheduled.Includes(actor)) {
      StepLevel(a_out, actor, record, a_facts);
    }
  }
}

void See(TickOutput &a_out, ActorId a_actor, TrackedActor &a_record,
         const TickFacts &a_facts) {
  InstalledRender &render = *a_record.render;
  const ActorVisibility visibility = VisibilityOf(a_record.sight);
  const std::optional<ResolutionLevel> target = TargetLevel(render.phase);
  const SeenThisTick seen{
      .shown = visibility.shown,
      .small = target && SmallFor(visibility.screenFraction, *target),
      .wanted = LevelFor(visibility),
      .now = a_facts.now,
      .outOfViewReleaseDelay = a_facts.settings.outOfViewReleaseDelayMs,
      .releasableBytes = ReleasableBytes(render)};
  ApplyPhaseChange(a_out, a_actor, render,
                   SeeActor(a_actor, render.phase, seen), "seen");
}

std::uint64_t ReleasableBytes(const InstalledRender &a_render) noexcept {
  const std::optional<ResolutionLevel> level = AppliedLevel(a_render.phase);
  if (!level) {
    return 0;
  }
  const std::optional<LevelCost> &cost = a_render.levelCosts.At(*level);
  return cost ? IntermediateBytes(*cost) : 0;
}

void StepLevel(TickOutput &a_out, ActorId a_actor, TrackedActor &a_record,
               const TickFacts &a_facts) {
  InstalledRender &render = *a_record.render;
  if (Is<Restaging>(render.phase)) {
    return;
  }
  const ActorVisibility visibility = VisibilityOf(a_record.sight);
  const std::optional<ScreenStep> step =
      ScreenStepOf(render, visibility, a_facts);
  if (!step || FailedLevelOf(render.phase) == step->to) {
    return;
  }
  const bool needsRoom = step->up && visibility.role != ActorRole::kPlayer;
  if (needsRoom && !AdmitStepUp(a_out, a_actor, render, *step)) {
    return;
  }

  ReportLevelStep(a_out, a_actor, *step, visibility);
  ApplyPhaseChange(a_out, a_actor, render,
                   WantLevel(a_actor, render.phase, step->to), "level_wanted");
}

std::optional<ScreenStep> ScreenStepOf(const InstalledRender &a_render,
                                       const ActorVisibility &a_visibility,
                                       const TickFacts &a_facts) noexcept {
  const std::optional<ResolutionLevel> current = TargetLevel(a_render.phase);
  if (!current) {
    return std::nullopt;
  }
  const bool stepDownDue = ScreenStepDownDue(
      a_render.phase, a_facts.now, a_facts.settings.screenStepDownHoldMs);
  return Match(
      LevelStepOf(*current, a_visibility, stepDownDue),
      [](const KeepLevel &) -> std::optional<ScreenStep> {
        return std::nullopt;
      },
      [&](const StepUpTo &a_up) -> std::optional<ScreenStep> {
        return ScreenStep{*current, a_up.level, true};
      },
      [&](const StepDownTo &a_down) -> std::optional<ScreenStep> {
        return ScreenStep{*current, a_down.level, false};
      });
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
  ApplyBudget(a_out, Charged(a_out.world.budget, added));
  return true;
}

void BlockStepUp(TickOutput &a_out, ActorId a_actor, InstalledRender &a_render,
                 const ScreenStep &a_step, Admission a_admission,
                 std::uint64_t a_added) {
  Shown *shown = Get<Shown>(a_render.phase);
  if (!shown || shown->waitingForRoomFrom == a_step.from) {
    return;
  }

  shown->waitingForRoomFrom = a_step.from;

  ReportStepUpBlocked(a_out, a_actor, a_step, a_admission, a_added);
}

void ReportStepUpBlocked(TickOutput &a_out, ActorId a_actor,
                         const ScreenStep &a_step, Admission a_admission,
                         std::uint64_t a_added) {
  const BudgetState &budget = a_out.world.budget;
  AddTrace(a_out,
           {{"action", "resolution_blocked"},
            {"actor", ActorText(a_actor)},
            {"from", std::string{LevelName(a_step.from)}},
            {"to", std::string{LevelName(a_step.to)}},
            {"reason", std::string{AdmissionName(a_admission)}},
            {"added_bytes", std::to_string(a_added)},
            {"planned_bytes", std::to_string(budget.planned)},
            {"allowance_bytes", std::to_string(budget.allowance.value_or(0))}});
}

void ReportLevelStep(TickOutput &a_out, ActorId a_actor,
                     const ScreenStep &a_step,
                     const ActorVisibility &a_visibility) {
  AddTrace(a_out,
           {{"action", "resolution_level"},
            {"actor", ActorText(a_actor)},
            {"from", std::string{LevelName(a_step.from)}},
            {"to", std::string{LevelName(a_step.to)}},
            {"reason", "screen"},
            {"screen_fraction", FractionText(a_visibility.screenFraction)}});
}

std::string FractionText(std::optional<float> a_fraction) {
  return a_fraction ? std::format("{:.3f}", *a_fraction)
                    : std::string{"unmeasured"};
}

void FinishFirstDraws(TickOutput &a_out, std::uint64_t a_frame) {
  for (auto &[actor, record] : a_out.world.actors) {
    if (RenderLoaded(record)) {
      FinishFirstDraw(a_out, actor, *record.render, a_frame);
    }
  }
}

void FinishFirstDraw(TickOutput &a_out, ActorId a_actor,
                     InstalledRender &a_render, std::uint64_t a_frame) {
  const Drawing *drawing = Get<Drawing>(a_render.phase);
  if (!drawing) {
    return;
  }
  const UnpublishedDraw &draw = drawing->draw;
  const std::optional<FirstDrawEnd> end =
      FirstDrawDone({draw.drawTicks, draw.total, draw.drawn});
  if (!end) {
    return;
  }

  ReportFirstDraw(a_out, a_actor, draw, *end, a_frame);
  ApplyPhaseChange(a_out, a_actor, a_render, EndFirstDraw(a_render.phase),
                   "first_draw_ended");
}

void ReportFirstDraw(TickOutput &a_out, ActorId a_actor,
                     const UnpublishedDraw &a_draw, FirstDrawEnd a_end,
                     std::uint64_t a_frame) {
  const std::uint64_t frames = a_frame - std::min(a_frame, a_draw.beganFrame);
  AddTrace(a_out, {{"action", "first_draw"},
                   {"actor", ActorText(a_actor)},
                   {"end", std::string{FirstDrawEndName(a_end)}},
                   {"frames", std::to_string(frames)},
                   {"draw_ticks", std::to_string(a_draw.drawTicks)},
                   {"stacks", std::to_string(a_draw.total)},
                   {"drawn", std::to_string(a_draw.drawn)}});
}

void IssueTickActors(TickOutput &a_out, const TickSettings &a_settings) {
  for (const auto &[actor, record] : a_out.world.actors) {
    if (RenderLoaded(record) && !IsRestageUnanswered(record.render->phase)) {
      Issue(a_out, TickActorFor(actor, record.render->phase, record.sight,
                                a_settings));
    }
  }
}

bool IsRestageUnanswered(const RenderPhase &a_phase) noexcept {
  const Restaging *restaging = Get<Restaging>(a_phase);
  return restaging && Is<RestageRequested>(restaging->step);
}

std::optional<DrawUnpublished>
UnpublishedDraws(const World &a_world, const ActorSet &a_swapping,
                 const TickSettings &a_settings) {
  const std::vector<UnpublishedDrawCandidate> candidates =
      Choose(a_world.actors, [&](const auto &a_entry) {
        return UnpublishedCandidateOf(a_entry.first, a_entry.second,
                                      a_swapping);
      });
  if (candidates.empty()) {
    return std::nullopt;
  }
  return DrawUnpublished{
      .order = Map(UnpublishedDrawOrder(candidates), ActorOf),
      .budgetUs =
          std::uint64_t{a_settings.unpublishedDrawBudgetMs} * kMicrosPerMilli};
}

std::optional<UnpublishedDrawCandidate>
UnpublishedCandidateOf(ActorId a_actor, const TrackedActor &a_record,
                       const ActorSet &a_swapping) {
  if (!RenderLoaded(a_record) || !a_record.sight.shown ||
      !UnpublishedDrawOf(a_record.render->phase) ||
      a_swapping.contains(a_actor)) {
    return std::nullopt;
  }
  return UnpublishedDrawCandidate{.actor = FormOf(a_actor),
                                  .role = a_record.sight.role,
                                  .screenFraction =
                                      a_record.sight.screenFraction};
}
}

TickActor TickActorFor(ActorId a_actor, const RenderPhase &a_phase,
                       const Sighting &a_sight,
                       const TickSettings &a_settings) noexcept {
  const bool firstDrawing = Is<Drawing>(a_phase);
  const bool unpublished = firstDrawing || Is<Planning>(a_phase);
  const bool settled = !unpublished && !Is<Restaging>(a_phase);
  return {.actor = a_actor,
          .drawStacks = a_sight.shown,
          .concealSlots = unpublished,
          .hideShells = !a_settings.publishEffects || unpublished,
          .hideLights = firstDrawing,
          .finishApplications = settled || !a_sight.shown};
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
      },
      [](const RestageSwapped &) {
        return std::string_view{"restage_swapped"};
      },
      [](const RestageNotSwapped &) {
        return std::string_view{"restage_not_swapped"};
      });
}
}
