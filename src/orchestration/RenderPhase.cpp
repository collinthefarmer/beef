// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/RenderPhase.h"

namespace BetterEnchantmentEffects::Orchestration {
IntermediateHolding HoldingOf(const Intermediates &a_intermediates) noexcept {
  return Is<Freed>(a_intermediates) ? IntermediateHolding::kReleased
                                    : IntermediateHolding::kHeld;
}

IntermediateHolding HoldingOf(const RenderPhase &a_phase) noexcept {
  if (const Shown *shown = Get<Shown>(a_phase)) {
    return HoldingOf(shown->intermediates);
  }
  if (const Resizing *resizing = Get<Resizing>(a_phase)) {
    return resizing->holding;
  }
  return IntermediateHolding::kHeld;
}

std::optional<ResolutionLevel>
TargetLevel(const RenderPhase &a_phase) noexcept {
  return Match(
      a_phase,
      [](const Shown &a_shown) -> std::optional<ResolutionLevel> {
        return a_shown.level;
      },
      [](const Empty &) -> std::optional<ResolutionLevel> {
        return std::nullopt;
      },
      [](const Planning &) -> std::optional<ResolutionLevel> {
        return std::nullopt;
      },
      [](const Drawing &a_drawing) -> std::optional<ResolutionLevel> {
        return a_drawing.next.value_or(a_drawing.level);
      },
      [](const Resizing &a_resizing) -> std::optional<ResolutionLevel> {
        return a_resizing.to;
      });
}

std::optional<ResolutionLevel>
AppliedLevel(const RenderPhase &a_phase) noexcept {
  return Match(
      a_phase,
      [](const Shown &a_shown) -> std::optional<ResolutionLevel> {
        return a_shown.level;
      },
      [](const Empty &) -> std::optional<ResolutionLevel> {
        return std::nullopt;
      },
      [](const Planning &) -> std::optional<ResolutionLevel> {
        return std::nullopt;
      },
      [](const Drawing &a_drawing) -> std::optional<ResolutionLevel> {
        return a_drawing.level;
      },
      [](const Resizing &a_resizing) -> std::optional<ResolutionLevel> {
        return a_resizing.from;
      });
}

bool MayRelease(const RenderPhase &a_phase) noexcept {
  const Shown *shown = Get<Shown>(a_phase);
  return shown && Is<Held>(shown->intermediates);
}

bool MayStepDown(const RenderPhase &a_phase) noexcept {
  return Is<Shown>(a_phase);
}

bool MayEvict(const RenderPhase &a_phase) noexcept {
  return !Is<Resizing>(a_phase);
}

const UnpublishedDraw *UnpublishedDrawOf(const RenderPhase &a_phase) noexcept {
  if (const Drawing *drawing = Get<Drawing>(a_phase)) {
    return &drawing->draw;
  }
  if (const Resizing *resizing = Get<Resizing>(a_phase)) {
    if (const Running *running = Get<Running>(resizing->stage)) {
      return &running->draw;
    }
  }
  return nullptr;
}

std::optional<ResizeProgress>
ResizeProgressOf(const RenderPhase &a_phase) noexcept {
  const Resizing *resizing = Get<Resizing>(a_phase);
  if (!resizing) {
    return std::nullopt;
  }
  const Running *running = Get<Running>(resizing->stage);
  if (!running) {
    return std::nullopt;
  }
  return ResizeProgress{resizing->from, resizing->to, running->draw.beganFrame};
}

std::optional<PlanJobId> AwaitedJob(const RenderPhase &a_phase) noexcept {
  const Planning *awaiting = Get<Planning>(a_phase);
  return awaiting ? std::optional<PlanJobId>{awaiting->job} : std::nullopt;
}

std::optional<ResolutionLevel>
FailedLevelOf(const RenderPhase &a_phase) noexcept {
  const std::optional<FailedLevel> *failed = nullptr;
  if (const Shown *shown = Get<Shown>(a_phase)) {
    failed = &shown->failed;
  } else if (const Resizing *resizing = Get<Resizing>(a_phase)) {
    failed = &resizing->failed;
  }
  if (!failed || !*failed) {
    return std::nullopt;
  }
  return (*failed)->level;
}

bool ScreenStepDownDue(const RenderPhase &a_phase, Millis a_now,
                       Millis a_delay) noexcept {
  const Shown *shown = Get<Shown>(a_phase);
  return shown && shown->smallOnScreen.Due(a_now, a_delay);
}

std::string_view PhaseName(const RenderPhase &a_phase) noexcept {
  return Match(
      a_phase, [](const Shown &) { return std::string_view{"shown"}; },
      [](const Empty &) { return std::string_view{"empty"}; },
      [](const Planning &) { return std::string_view{"planning"}; },
      [](const Drawing &) { return std::string_view{"drawing"}; },
      [](const Resizing &a_resizing) {
        return Is<Queued>(a_resizing.stage)
                   ? std::string_view{"resize_queued"}
                   : std::string_view{"resize_running"};
      });
}
}
