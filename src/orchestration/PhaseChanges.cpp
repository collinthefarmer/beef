// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/PhaseChanges.h"

#include <algorithm>
#include <utility>

namespace BetterEnchantmentEffects::Orchestration {
namespace {
PhaseChange Unchanged(RenderPhase a_phase) { return {std::move(a_phase), {}}; }

PhaseChange Ignored(RenderPhase a_phase) {
  return {std::move(a_phase), {}, true};
}

Intermediates IntermediatesFrom(IntermediateHolding a_holding) {
  if (a_holding == IntermediateHolding::kReleased) {
    return IntermediatesReleased{};
  }
  return IntermediatesHeld{};
}

Live LiveAt(ResolutionLevel a_level, Intermediates a_intermediates,
            std::optional<FailedLevel> a_failed = std::nullopt) {
  return Live{a_level, std::move(a_intermediates), {}, std::nullopt, a_failed};
}

PhaseChange Applied(ActorId a_actor, RenderPhase a_phase,
                    ResolutionLevel a_level) {
  return {std::move(a_phase), {SetAppliedLevel{a_actor, a_level}}};
}

PhaseChange ResizeOrStay(ResolutionLevel a_from, ResolutionLevel a_to,
                         IntermediateHolding a_holding,
                         std::optional<FailedLevel> a_failed) {
  if (a_from == a_to) {
    return {LiveAt(a_from, IntermediatesFrom(a_holding), a_failed), {}};
  }
  return {Resizing{a_from, a_to, a_holding, ResizeQueued{}, a_failed}, {}};
}

bool Refused(const std::optional<FailedLevel> &a_failed,
             ResolutionLevel a_level) noexcept {
  return a_failed && a_failed->level == a_level;
}

void ForgetStaleFailure(std::optional<FailedLevel> &a_failed,
                        const std::optional<ResolutionLevel> &a_wanted) {
  if (a_failed && a_failed->wantedThen != a_wanted) {
    a_failed.reset();
  }
}

PhaseChange Created(ActorId a_actor, const RenderEvent &a_event,
                    std::uint64_t a_frame) {
  if (const PlanAwaited *awaited = Get<PlanAwaited>(a_event)) {
    return {AwaitingPlan{awaited->job}, {}};
  }
  if (const Installed *installed = Get<Installed>(a_event)) {
    return Applied(
        a_actor,
        FirstDrawing{installed->level,
                     UnpublishedDraw{installed->total, 0, a_frame, 0},
                     std::nullopt},
        installed->level);
  }
  return {WithoutRender{}, {}};
}

bool CreatesAPhase(const RenderEvent &a_event) noexcept {
  return Is<PlanAwaited>(a_event) || Is<Installed>(a_event) ||
         Is<InstalledWithoutRender>(a_event);
}

UnpublishedDraw *UnpublishedDrawIn(RenderPhase &a_phase) noexcept {
  if (FirstDrawing *drawing = Get<FirstDrawing>(a_phase)) {
    return &drawing->draw;
  }
  if (Resizing *resizing = Get<Resizing>(a_phase)) {
    if (ResizeRunning *running = Get<ResizeRunning>(resizing->stage)) {
      return &running->draw;
    }
  }
  return nullptr;
}

PhaseChange Drawn(RenderPhase a_phase, const UnpublishedDrawn &a_drawn) {
  UnpublishedDraw *draw = UnpublishedDrawIn(a_phase);
  if (!draw) {
    return Ignored(std::move(a_phase));
  }
  draw->drawn = std::min(a_drawn.drawn, draw->total);
  draw->drawTicks += a_drawn.drewThisTick ? 1 : 0;
  return Unchanged(std::move(a_phase));
}

PhaseChange FailedResize(const Resizing &a_resizing,
                         const EventContext &a_context) {
  return {LiveAt(a_resizing.from, IntermediatesFrom(a_resizing.holding),
                 FailedLevel{a_resizing.to, a_context.wanted}),
          {}};
}

PhaseChange OnResize(ActorId a_actor, RenderPhase a_phase,
                     const RenderEvent &a_event,
                     const EventContext &a_context) {
  Resizing *resizing = Get<Resizing>(a_phase);
  if (!resizing) {
    return Ignored(std::move(a_phase));
  }
  const bool queued = Is<ResizeQueued>(resizing->stage);
  if (const ResizeStarted *started = Get<ResizeStarted>(a_event);
      started && queued) {
    resizing->stage =
        ResizeRunning{UnpublishedDraw{started->total, 0, a_context.frame, 0}};
    return Unchanged(std::move(a_phase));
  }
  if (Is<ResizeNotStarted>(a_event) && queued) {
    return FailedResize(*resizing, a_context);
  }
  if (Is<ResizeSwapped>(a_event) && !queued) {
    const ResolutionLevel to = resizing->to;
    return Applied(a_actor, LiveAt(to, IntermediatesHeld{}, resizing->failed),
                   to);
  }
  if (Is<ResizeNotSwapped>(a_event) && !queued) {
    PhaseChange failed = FailedResize(*resizing, a_context);
    failed.commands.push_back(AbandonResize{a_actor});
    return failed;
  }
  return Ignored(std::move(a_phase));
}
}

std::optional<PhaseChange> PhaseFor(ActorId a_actor, const RenderEvent &a_event,
                                    std::uint64_t a_frame) {
  if (!CreatesAPhase(a_event)) {
    return std::nullopt;
  }
  return Created(a_actor, a_event, a_frame);
}

PhaseChange OnEvent(ActorId a_actor, RenderPhase a_phase,
                    const RenderEvent &a_event, const EventContext &a_context) {
  if (CreatesAPhase(a_event)) {
    if (!Is<AwaitingPlan>(a_phase)) {
      return Ignored(std::move(a_phase));
    }
    return Created(a_actor, a_event, a_context.frame);
  }
  if (const UnpublishedDrawn *drawn = Get<UnpublishedDrawn>(a_event)) {
    return Drawn(std::move(a_phase), *drawn);
  }
  return OnResize(a_actor, std::move(a_phase), a_event, a_context);
}

PhaseChange PlanBuilt(ActorId a_actor, RenderPhase a_phase, PlanJobId a_job,
                      InstallBound a_bound) {
  if (AwaitedJob(a_phase) != a_job) {
    return Unchanged(std::move(a_phase));
  }
  return {std::move(a_phase), {BuildAwaitedRender{a_actor, a_bound}}};
}

PhaseChange FailPlan(ActorId a_actor, RenderPhase a_phase,
                     const PlanJobFailed &a_failed) {
  if (AwaitedJob(a_phase) != a_failed.job) {
    return Unchanged(std::move(a_phase));
  }
  return {WithoutRender{}, {FailAwaitedRender{a_actor, a_failed.problem}}};
}

PhaseChange WantLevel(ActorId a_actor, RenderPhase a_phase,
                      ResolutionLevel a_level) {
  if (FirstDrawing *drawing = Get<FirstDrawing>(a_phase)) {
    drawing->next = a_level;
    return Unchanged(std::move(a_phase));
  }
  if (Live *live = Get<Live>(a_phase)) {
    if (live->level == a_level || Refused(live->failed, a_level)) {
      return Unchanged(std::move(a_phase));
    }
    return ResizeOrStay(live->level, a_level, HoldingOf(live->intermediates),
                        live->failed);
  }
  if (Resizing *resizing = Get<Resizing>(a_phase)) {
    if (resizing->to == a_level || Refused(resizing->failed, a_level)) {
      return Unchanged(std::move(a_phase));
    }
    const bool running = Is<ResizeRunning>(resizing->stage);
    PhaseChange next = ResizeOrStay(resizing->from, a_level, resizing->holding,
                                    resizing->failed);
    if (running) {
      next.commands.push_back(AbandonResize{a_actor});
    }
    return next;
  }
  return Unchanged(std::move(a_phase));
}

PhaseChange ReleaseForBudget(ActorId a_actor, RenderPhase a_phase,
                             std::uint64_t a_bytes) {
  Live *live = Get<Live>(a_phase);
  if (!live || !Is<IntermediatesHeld>(live->intermediates)) {
    return Unchanged(std::move(a_phase));
  }
  live->intermediates = IntermediatesReleased{};
  return {std::move(a_phase),
          {ReleaseIntermediates{a_actor, ReleaseReason::kBudget, a_bytes}}};
}

PhaseChange SeeActor(ActorId a_actor, RenderPhase a_phase,
                     const SeenThisTick &a_seen) {
  if (Resizing *resizing = Get<Resizing>(a_phase)) {
    ForgetStaleFailure(resizing->failed, a_seen.wanted);
    if (a_seen.inView) {
      resizing->holding = IntermediateHolding::kHeld;
    }
    return Unchanged(std::move(a_phase));
  }
  Live *live = Get<Live>(a_phase);
  if (!live) {
    return Unchanged(std::move(a_phase));
  }
  ForgetStaleFailure(live->failed, a_seen.wanted);
  live->smallOnScreen = live->smallOnScreen.Update(a_seen.small, a_seen.now);
  if (a_seen.inView) {
    live->intermediates = IntermediatesHeld{};
    return Unchanged(std::move(a_phase));
  }
  IntermediatesHeld *held = Get<IntermediatesHeld>(live->intermediates);
  if (!held) {
    return Unchanged(std::move(a_phase));
  }
  held->outOfView = held->outOfView.Update(true, a_seen.now);
  if (!held->outOfView.Due(a_seen.now, a_seen.outOfViewReleaseDelay)) {
    return Unchanged(std::move(a_phase));
  }
  live->intermediates = IntermediatesReleased{};
  return {std::move(a_phase),
          {ReleaseIntermediates{a_actor, ReleaseReason::kOutOfView,
                                a_seen.releasableBytes}}};
}

PhaseChange EndFirstDraw(RenderPhase a_phase) {
  const FirstDrawing *drawing = Get<FirstDrawing>(a_phase);
  if (!drawing) {
    return Unchanged(std::move(a_phase));
  }
  return ResizeOrStay(drawing->level, drawing->next.value_or(drawing->level),
                      IntermediateHolding::kHeld, std::nullopt);
}
}
