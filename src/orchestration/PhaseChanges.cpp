// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/PhaseChanges.h"

#include <utility>

namespace BetterEnchantmentEffects::Orchestration {
namespace {
PhaseChange Unchanged(RenderPhase a_phase) { return {std::move(a_phase), {}}; }

Live LiveAt(ResolutionLevel a_level, Intermediates a_intermediates,
            std::optional<ResolutionLevel> a_failed = std::nullopt) {
  return Live{a_level, std::move(a_intermediates), {}, std::nullopt, a_failed};
}

PhaseChange Applied(ActorId a_actor, RenderPhase a_phase,
                    ResolutionLevel a_level) {
  return {std::move(a_phase), {SetAppliedLevel{a_actor, a_level}}};
}

PhaseChange ResizeOrStay(ResolutionLevel a_from, ResolutionLevel a_to,
                         Intermediates a_intermediates) {
  if (a_from == a_to) {
    return {LiveAt(a_from, std::move(a_intermediates)), {}};
  }
  return {Resizing{a_from, a_to, std::move(a_intermediates), ResizeQueued{}},
          {}};
}

PhaseChange Drawn(RenderPhase a_phase, const UnpublishedDrawn &a_drawn) {
  UnpublishedDraw *draw = nullptr;
  if (FirstDrawing *drawing = Get<FirstDrawing>(a_phase)) {
    draw = &drawing->draw;
  } else if (Resizing *resizing = Get<Resizing>(a_phase)) {
    if (ResizeRunning *running = Get<ResizeRunning>(resizing->stage)) {
      draw = &running->draw;
    }
  }
  if (draw) {
    draw->drawn = a_drawn.drawn;
    draw->drawTicks += a_drawn.drewThisTick ? 1 : 0;
  }
  return Unchanged(std::move(a_phase));
}

PhaseChange OnResize(ActorId a_actor, RenderPhase a_phase,
                     const RenderEvent &a_event, std::uint64_t a_frame) {
  Resizing *resizing = Get<Resizing>(a_phase);
  if (!resizing) {
    return Unchanged(std::move(a_phase));
  }
  const bool queued = Is<ResizeQueued>(resizing->stage);
  if (const ResizeStarted *started = Get<ResizeStarted>(a_event);
      started && queued) {
    resizing->stage =
        ResizeRunning{UnpublishedDraw{started->units, 0, a_frame, 0}};
    return Unchanged(std::move(a_phase));
  }
  if (Is<ResizeNotStarted>(a_event) && queued) {
    return {LiveAt(resizing->from, std::move(resizing->intermediates),
                   resizing->to),
            {}};
  }
  if (Is<ResizeSwapped>(a_event) && !queued) {
    const ResolutionLevel to = resizing->to;
    return Applied(a_actor, LiveAt(to, IntermediatesHeld{}), to);
  }
  if (Is<ResizeNotSwapped>(a_event) && !queued) {
    resizing->stage = ResizeQueued{};
  }
  return Unchanged(std::move(a_phase));
}
}

PhaseChange OnEvent(ActorId a_actor, RenderPhase a_phase,
                    const RenderEvent &a_event, std::uint64_t a_frame) {
  return Match(
      a_event,
      [&](const PlanAwaited &a_awaited) -> PhaseChange {
        return {AwaitingPlan{a_awaited.job}, {}};
      },
      [&](const Installed &a_installed) -> PhaseChange {
        return Applied(
            a_actor,
            FirstDrawing{a_installed.level,
                         UnpublishedDraw{a_installed.units, 0, a_frame, 0},
                         std::nullopt},
            a_installed.level);
      },
      [&](const InstalledWithoutRender &) -> PhaseChange {
        return {WithoutRender{}, {}};
      },
      [&](const UnpublishedDrawn &a_drawn) -> PhaseChange {
        return Drawn(std::move(a_phase), a_drawn);
      },
      [&](const ResizeStarted &) -> PhaseChange {
        return OnResize(a_actor, std::move(a_phase), a_event, a_frame);
      },
      [&](const ResizeNotStarted &) -> PhaseChange {
        return OnResize(a_actor, std::move(a_phase), a_event, a_frame);
      },
      [&](const ResizeSwapped &) -> PhaseChange {
        return OnResize(a_actor, std::move(a_phase), a_event, a_frame);
      },
      [&](const ResizeNotSwapped &) -> PhaseChange {
        return OnResize(a_actor, std::move(a_phase), a_event, a_frame);
      });
}

PhaseChange FailPlan(RenderPhase a_phase, PlanJobId a_job) {
  if (AwaitedJob(a_phase) != a_job) {
    return Unchanged(std::move(a_phase));
  }
  return {WithoutRender{}, {}};
}

PhaseChange WantLevel(ActorId a_actor, RenderPhase a_phase,
                      ResolutionLevel a_level) {
  if (FirstDrawing *drawing = Get<FirstDrawing>(a_phase)) {
    drawing->next = a_level;
    return Unchanged(std::move(a_phase));
  }
  if (Live *live = Get<Live>(a_phase)) {
    if (live->failedLevel == a_level) {
      return Unchanged(std::move(a_phase));
    }
    return ResizeOrStay(live->level, a_level, std::move(live->intermediates));
  }
  if (Resizing *resizing = Get<Resizing>(a_phase)) {
    if (resizing->to == a_level) {
      return Unchanged(std::move(a_phase));
    }
    const bool running = Is<ResizeRunning>(resizing->stage);
    PhaseChange next = ResizeOrStay(resizing->from, a_level,
                                    std::move(resizing->intermediates));
    if (running) {
      next.commands.push_back(AbandonResize{a_actor});
    }
    return next;
  }
  return Unchanged(std::move(a_phase));
}

PhaseChange ReleaseForBudget(ActorId a_actor, RenderPhase a_phase,
                             std::uint64_t a_bytes) {
  if (!MayRelease(a_phase)) {
    return Unchanged(std::move(a_phase));
  }
  Get<Live>(a_phase)->intermediates = IntermediatesReleased{};
  return {std::move(a_phase),
          {ReleaseIntermediates{a_actor, ReleaseReason::kBudget, a_bytes}}};
}

PhaseChange SeeActor(ActorId a_actor, RenderPhase a_phase,
                     const SeenActor &a_seen) {
  Live *live = Get<Live>(a_phase);
  if (!live) {
    return Unchanged(std::move(a_phase));
  }
  if (live->failedLevel != a_seen.wanted) {
    live->failedLevel.reset();
  }
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
  FirstDrawing *drawing = Get<FirstDrawing>(a_phase);
  if (!drawing) {
    return Unchanged(std::move(a_phase));
  }
  return ResizeOrStay(drawing->level, drawing->next.value_or(drawing->level),
                      IntermediatesHeld{});
}
}
