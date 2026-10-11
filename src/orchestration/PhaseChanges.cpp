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
    return Freed{};
  }
  return Held{};
}

Shown ShownAt(ResolutionLevel a_level, Intermediates a_intermediates,
              std::optional<FailedLevel> a_failed = std::nullopt) {
  return Shown{a_level, std::move(a_intermediates), {}, std::nullopt, a_failed};
}

PhaseChange Applied(ActorId a_actor, RenderPhase a_phase,
                    ResolutionLevel a_level) {
  return {std::move(a_phase), {SetAppliedLevel{a_actor, a_level}}};
}

PhaseChange ResizeOrStay(ResolutionLevel a_from, ResolutionLevel a_to,
                         IntermediateHolding a_holding,
                         std::optional<FailedLevel> a_failed) {
  if (a_from == a_to) {
    return {ShownAt(a_from, IntermediatesFrom(a_holding), a_failed), {}};
  }
  return {Resizing{a_from, a_to, a_holding, Queued{}, a_failed}, {}};
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
    return {Planning{awaited->job}, {}};
  }
  if (const Installed *installed = Get<Installed>(a_event)) {
    return Applied(a_actor,
                   Drawing{installed->level,
                           UnpublishedDraw{installed->total, 0, a_frame, 0},
                           std::nullopt},
                   installed->level);
  }
  return {Empty{}, {}};
}

bool CreatesAPhase(const RenderEvent &a_event) noexcept {
  return Is<PlanAwaited>(a_event) || Is<Installed>(a_event) ||
         Is<InstalledWithoutRender>(a_event);
}

UnpublishedDraw *UnpublishedDrawIn(RenderPhase &a_phase) noexcept {
  if (Drawing *drawing = Get<Drawing>(a_phase)) {
    return &drawing->draw;
  }
  if (Resizing *resizing = Get<Resizing>(a_phase)) {
    if (Running *running = Get<Running>(resizing->stage)) {
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
  return {ShownAt(a_resizing.from, IntermediatesFrom(a_resizing.holding),
                  FailedLevel{a_resizing.to, a_context.wanted}),
          {}};
}

PhaseChange OnQueuedResize(RenderPhase a_phase, const RenderEvent &a_event,
                           const EventContext &a_context) {
  Resizing *resizing = Get<Resizing>(a_phase);
  if (!resizing) {
    return Ignored(std::move(a_phase));
  }

  if (Is<ResizeNotStarted>(a_event)) {
    return FailedResize(*resizing, a_context);
  }

  const ResizeStarted *started = Get<ResizeStarted>(a_event);
  if (!started) {
    return Ignored(std::move(a_phase));
  }
  resizing->stage = Running{
      UnpublishedDraw{started->total, std::min(started->drawn, started->total),
                      a_context.frame, 0}};
  return Unchanged(std::move(a_phase));
}

PhaseChange OnRunningResize(ActorId a_actor, RenderPhase a_phase,
                            const RenderEvent &a_event,
                            const EventContext &a_context) {
  const Resizing *resizing = Get<Resizing>(a_phase);
  if (!resizing) {
    return Ignored(std::move(a_phase));
  }

  if (Is<ResizeNotSwapped>(a_event)) {
    PhaseChange failed = FailedResize(*resizing, a_context);
    failed.commands.push_back(AbandonResize{a_actor});
    return failed;
  }

  if (!Is<ResizeSwapped>(a_event)) {
    return Ignored(std::move(a_phase));
  }
  const ResolutionLevel to = resizing->to;
  return Applied(a_actor, ShownAt(to, Held{}, resizing->failed), to);
}

PhaseChange OnResize(ActorId a_actor, RenderPhase a_phase,
                     const RenderEvent &a_event,
                     const EventContext &a_context) {
  const Resizing *resizing = Get<Resizing>(a_phase);
  if (!resizing) {
    return Ignored(std::move(a_phase));
  }
  if (Is<Queued>(resizing->stage)) {
    return OnQueuedResize(std::move(a_phase), a_event, a_context);
  }
  return OnRunningResize(a_actor, std::move(a_phase), a_event, a_context);
}

PhaseChange OnCreatingEvent(ActorId a_actor, RenderPhase a_phase,
                            const RenderEvent &a_event,
                            const EventContext &a_context) {
  if (!Is<Planning>(a_phase)) {
    return Ignored(std::move(a_phase));
  }
  return Created(a_actor, a_event, a_context.frame);
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
    return OnCreatingEvent(a_actor, std::move(a_phase), a_event, a_context);
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
  return {Empty{}, {FailAwaitedRender{a_actor, a_failed.problem}}};
}

PhaseChange WantLevel(ActorId a_actor, RenderPhase a_phase,
                      ResolutionLevel a_level) {
  if (Drawing *drawing = Get<Drawing>(a_phase)) {
    drawing->next = a_level;
    return Unchanged(std::move(a_phase));
  }
  if (Shown *shown = Get<Shown>(a_phase)) {
    if (shown->level == a_level || Refused(shown->failed, a_level)) {
      return Unchanged(std::move(a_phase));
    }
    return ResizeOrStay(shown->level, a_level, HoldingOf(shown->intermediates),
                        shown->failed);
  }
  if (Resizing *resizing = Get<Resizing>(a_phase)) {
    if (resizing->to == a_level || Refused(resizing->failed, a_level)) {
      return Unchanged(std::move(a_phase));
    }
    const bool running = Is<Running>(resizing->stage);
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
  Shown *shown = Get<Shown>(a_phase);
  if (!shown || !Is<Held>(shown->intermediates)) {
    return Unchanged(std::move(a_phase));
  }
  shown->intermediates = Freed{};
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
  Shown *shown = Get<Shown>(a_phase);
  if (!shown) {
    return Unchanged(std::move(a_phase));
  }
  ForgetStaleFailure(shown->failed, a_seen.wanted);
  shown->smallOnScreen = shown->smallOnScreen.Update(a_seen.small, a_seen.now);
  if (a_seen.inView) {
    shown->intermediates = Held{};
    return Unchanged(std::move(a_phase));
  }
  Held *held = Get<Held>(shown->intermediates);
  if (!held) {
    return Unchanged(std::move(a_phase));
  }
  held->outOfView = held->outOfView.Update(true, a_seen.now);
  if (!held->outOfView.Due(a_seen.now, a_seen.outOfViewReleaseDelay)) {
    return Unchanged(std::move(a_phase));
  }
  shown->intermediates = Freed{};
  return {std::move(a_phase),
          {ReleaseIntermediates{a_actor, ReleaseReason::kOutOfView,
                                a_seen.releasableBytes}}};
}

PhaseChange EndFirstDraw(RenderPhase a_phase) {
  const Drawing *drawing = Get<Drawing>(a_phase);
  if (!drawing) {
    return Unchanged(std::move(a_phase));
  }
  return ResizeOrStay(drawing->level, drawing->next.value_or(drawing->level),
                      IntermediateHolding::kHeld, std::nullopt);
}
}
