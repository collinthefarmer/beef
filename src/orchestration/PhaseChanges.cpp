// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/PhaseChanges.h"

#include <algorithm>

namespace BetterEnchantmentEffects::Orchestration {
namespace {
PhaseChange Unchanged(const RenderPhase &a_phase) { return {a_phase, {}}; }

PhaseChange Ignored(const RenderPhase &a_phase) { return {a_phase, {}, true}; }

PhaseChange Applied(ActorId a_actor, const RenderPhase &a_phase,
                    Level a_level) {
  return {a_phase, {SetAppliedLevel{a_actor, a_level}}};
}

Intermediates IntermediatesFrom(IntermediateHolding a_holding) {
  if (a_holding == IntermediateHolding::kReleased) {
    return Freed{};
  }

  return Held{};
}

Shown ShownAt(Level a_level, const Intermediates &a_intermediates,
              const std::optional<FailedLevel> &a_failed = std::nullopt) {
  return Shown{a_level, a_intermediates, {}, std::nullopt, a_failed};
}

PhaseChange QueuedOrShown(const Resize &a_resize) {
  if (a_resize.from == a_resize.to) {
    const Intermediates kept = IntermediatesFrom(a_resize.holding);
    return {ShownAt(a_resize.from, kept, a_resize.failed), {}};
  }

  return {Queued{a_resize}, {}};
}

bool Refused(const std::optional<FailedLevel> &a_failed,
             Level a_level) noexcept {
  return a_failed && a_failed->level == a_level;
}

std::optional<FailedLevel>
StillFailed(const std::optional<FailedLevel> &a_failed,
            const std::optional<Level> &a_wanted) {
  if (a_failed && a_failed->wantedThen != a_wanted) {
    return std::nullopt;
  }

  return a_failed;
}

std::optional<Resize> Retargeted(const Resize &a_resize, Level a_level) {
  if (a_resize.to == a_level || Refused(a_resize.failed, a_level)) {
    return std::nullopt;
  }

  Resize next = a_resize;
  next.to = a_level;
  return next;
}

Resize Seen(Resize a_resize, const SeenThisTick &a_seen) {
  a_resize.failed = StillFailed(a_resize.failed, a_seen.wanted);
  a_resize.holding =
      a_seen.shown ? IntermediateHolding::kHeld : a_resize.holding;
  return a_resize;
}

PhaseChange Created(ActorId, const PlanAwaited &a_awaited, std::uint64_t) {
  return {Planning{a_awaited.job}, {}};
}

PhaseChange Created(ActorId a_actor, const Installed &a_installed,
                    std::uint64_t a_frame) {
  const UnpublishedDraw draw{a_installed.total, 0, a_frame, 0};
  const Drawing drawing{a_installed.level, draw, std::nullopt};
  return Applied(a_actor, drawing, a_installed.level);
}

PhaseChange Created(ActorId, const InstalledWithoutRender &, std::uint64_t) {
  return {Empty{}, {}};
}

UnpublishedDraw Counted(UnpublishedDraw a_draw,
                        const UnpublishedDrawn &a_drawn) {
  a_draw.drawn = std::min(a_drawn.drawn, a_draw.total);
  a_draw.drawTicks += a_drawn.drewThisTick ? 1 : 0;
  return a_draw;
}

PhaseChange Drawn(Drawing a_drawing, const UnpublishedDrawn &a_drawn) {
  a_drawing.draw = Counted(a_drawing.draw, a_drawn);
  return {a_drawing, {}};
}

PhaseChange Drawn(Running a_running, const UnpublishedDrawn &a_drawn) {
  a_running.draw = Counted(a_running.draw, a_drawn);
  return {a_running, {}};
}

PhaseChange Started(const Queued &a_queued, const ResizeStarted &a_started,
                    std::uint64_t a_frame) {
  const std::size_t drawn = std::min(a_started.drawn, a_started.total);
  const UnpublishedDraw draw{a_started.total, drawn, a_frame, 0};
  return {Running{a_queued.resize, draw}, {}};
}

PhaseChange FailedResize(const Resize &a_resize,
                         const EventContext &a_context) {
  const Intermediates kept = IntermediatesFrom(a_resize.holding);
  const FailedLevel failed{a_resize.to, a_context.wanted};
  return {ShownAt(a_resize.from, kept, failed), {}};
}

PhaseChange Swapped(ActorId a_actor, const Running &a_running) {
  const Resize &resize = a_running.resize;
  return Applied(a_actor, ShownAt(resize.to, Held{}, resize.failed), resize.to);
}

PhaseChange NotSwapped(ActorId a_actor, const Running &a_running,
                       const EventContext &a_context) {
  PhaseChange failed = FailedResize(a_running.resize, a_context);
  failed.commands.push_back(AbandonResize{a_actor});
  return failed;
}

PhaseChange SeeShown(ActorId a_actor, Shown a_shown,
                     const SeenThisTick &a_seen) {
  a_shown.failed = StillFailed(a_shown.failed, a_seen.wanted);
  a_shown.smallOnScreen =
      a_shown.smallOnScreen.Update(a_seen.small, a_seen.now);
  if (a_seen.shown) {
    a_shown.intermediates = Held{};
    return {a_shown, {}};
  }

  Held *held = Get<Held>(a_shown.intermediates);
  if (!held) {
    return {a_shown, {}};
  }

  held->outOfView = held->outOfView.Update(true, a_seen.now);
  if (!held->outOfView.Due(a_seen.now, a_seen.outOfViewReleaseDelay)) {
    return {a_shown, {}};
  }

  a_shown.intermediates = Freed{};
  return {a_shown,
          {ReleaseIntermediates{a_actor, ReleaseReason::kOutOfView,
                                a_seen.releasableBytes}}};
}
}

std::optional<PhaseChange> PhaseFor(ActorId a_actor, const RenderEvent &a_event,
                                    std::uint64_t a_frame) {
  using Creation = std::optional<PhaseChange>;
  return Match(
      a_event,
      [&](const PlanAwaited &a_awaited) -> Creation {
        return Created(a_actor, a_awaited, a_frame);
      },
      [&](const Installed &a_installed) -> Creation {
        return Created(a_actor, a_installed, a_frame);
      },
      [&](const InstalledWithoutRender &a_bare) -> Creation {
        return Created(a_actor, a_bare, a_frame);
      },
      [](const auto &) -> Creation { return std::nullopt; });
}

PhaseChange OnEvent(ActorId a_actor, RenderPhase a_phase,
                    const RenderEvent &a_event, const EventContext &a_context) {
  const std::uint64_t frame = a_context.frame;
  return Match(
      a_phase, a_event,
      [&](const Planning &, const PlanAwaited &a_awaited) {
        return Created(a_actor, a_awaited, frame);
      },
      [&](const Planning &, const Installed &a_installed) {
        return Created(a_actor, a_installed, frame);
      },
      [&](const Planning &, const InstalledWithoutRender &a_bare) {
        return Created(a_actor, a_bare, frame);
      },
      [](const Drawing &a_drawing, const UnpublishedDrawn &a_drawn) {
        return Drawn(a_drawing, a_drawn);
      },
      [](const Running &a_running, const UnpublishedDrawn &a_drawn) {
        return Drawn(a_running, a_drawn);
      },
      [&](const Queued &a_queued, const ResizeStarted &a_started) {
        return Started(a_queued, a_started, frame);
      },
      [&](const Queued &a_queued, const ResizeNotStarted &) {
        return FailedResize(a_queued.resize, a_context);
      },
      [&](const Running &a_running, const ResizeSwapped &) {
        return Swapped(a_actor, a_running);
      },
      [&](const Running &a_running, const ResizeNotSwapped &) {
        return NotSwapped(a_actor, a_running, a_context);
      },
      [&](const auto &, const auto &) { return Ignored(a_phase); });
}

PhaseChange PlanBuilt(ActorId a_actor, RenderPhase a_phase, PlanJobId a_job,
                      InstallBounds a_bounds) {
  if (AwaitedJob(a_phase) != a_job) {
    return Unchanged(a_phase);
  }

  return {a_phase, {BuildAwaitedRender{a_actor, a_bounds}}};
}

PhaseChange FailPlan(ActorId a_actor, RenderPhase a_phase,
                     const PlanJobFailed &a_failed) {
  if (AwaitedJob(a_phase) != a_failed.job) {
    return Unchanged(a_phase);
  }

  return {Empty{}, {FailAwaitedRender{a_actor, a_failed.problem}}};
}

PhaseChange WantLevel(ActorId a_actor, RenderPhase a_phase, Level a_level) {
  return Match(
      a_phase,
      [&](const Drawing &a_drawing) -> PhaseChange {
        Drawing next = a_drawing;
        next.next = a_level;
        return {next, {}};
      },
      [&](const Shown &a_shown) -> PhaseChange {
        if (a_shown.level == a_level || Refused(a_shown.failed, a_level)) {
          return Unchanged(a_phase);
        }

        const IntermediateHolding holding = HoldingOf(a_shown.intermediates);
        return QueuedOrShown(
            Resize{a_shown.level, a_level, holding, a_shown.failed});
      },
      [&](const Queued &a_queued) -> PhaseChange {
        const std::optional<Resize> next = Retargeted(a_queued.resize, a_level);
        if (!next) {
          return Unchanged(a_phase);
        }

        return QueuedOrShown(*next);
      },
      [&](const Running &a_running) -> PhaseChange {
        const std::optional<Resize> next =
            Retargeted(a_running.resize, a_level);
        if (!next) {
          return Unchanged(a_phase);
        }

        PhaseChange change = QueuedOrShown(*next);
        change.commands.push_back(AbandonResize{a_actor});
        return change;
      },
      [&](const auto &) { return Unchanged(a_phase); });
}

PhaseChange ReleaseForBudget(ActorId a_actor, RenderPhase a_phase,
                             std::uint64_t a_bytes) {
  const Shown *shown = Get<Shown>(a_phase);
  if (!shown || !Is<Held>(shown->intermediates)) {
    return Unchanged(a_phase);
  }

  Shown released = *shown;
  released.intermediates = Freed{};
  return {released,
          {ReleaseIntermediates{a_actor, ReleaseReason::kBudget, a_bytes}}};
}

PhaseChange SeeActor(ActorId a_actor, RenderPhase a_phase,
                     const SeenThisTick &a_seen) {
  return Match(
      a_phase,
      [&](const Shown &a_shown) { return SeeShown(a_actor, a_shown, a_seen); },
      [&](const Queued &a_queued) -> PhaseChange {
        return {Queued{Seen(a_queued.resize, a_seen)}, {}};
      },
      [&](const Running &a_running) -> PhaseChange {
        return {Running{Seen(a_running.resize, a_seen), a_running.draw}, {}};
      },
      [&](const auto &) { return Unchanged(a_phase); });
}

PhaseChange EndFirstDraw(RenderPhase a_phase) {
  const Drawing *drawing = Get<Drawing>(a_phase);
  if (!drawing) {
    return Unchanged(a_phase);
  }

  const Level to = drawing->next.value_or(drawing->level);
  return QueuedOrShown(
      Resize{drawing->level, to, IntermediateHolding::kHeld, std::nullopt});
}
}
