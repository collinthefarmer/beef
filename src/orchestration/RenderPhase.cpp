// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/RenderPhase.h"

namespace BetterEnchantmentEffects::Orchestration {
const Resize *FindResize(const RenderPhase &a_phase) noexcept {
  if (const Queued *queued = Get<Queued>(a_phase)) {
    return &queued->resize;
  }
  const Running *running = Get<Running>(a_phase);
  return running ? &running->resize : nullptr;
}

Resize *FindResize(RenderPhase &a_phase) noexcept {
  if (Queued *queued = Get<Queued>(a_phase)) {
    return &queued->resize;
  }
  Running *running = Get<Running>(a_phase);
  return running ? &running->resize : nullptr;
}

IntermediateHolding HoldingOf(const Intermediates &a_intermediates) noexcept {
  return Is<Freed>(a_intermediates) ? IntermediateHolding::kReleased
                                    : IntermediateHolding::kHeld;
}

IntermediateHolding HoldingOf(const RenderPhase &a_phase) noexcept {
  if (const Shown *shown = Get<Shown>(a_phase)) {
    return HoldingOf(shown->intermediates);
  }
  const Resize *resize = FindResize(a_phase);
  return resize ? resize->holding : IntermediateHolding::kHeld;
}

std::optional<Level> TargetLevel(const RenderPhase &a_phase) noexcept {
  using Target = std::optional<Level>;
  return Match(
      a_phase, [](const Empty &) -> Target { return std::nullopt; },
      [](const Shown &a_shown) -> Target { return a_shown.level; },
      [](const Planning &) -> Target { return std::nullopt; },
      [](const Drawing &a_drawing) -> Target {
        return a_drawing.next.value_or(a_drawing.level);
      },
      [](const Queued &a_queued) -> Target { return a_queued.resize.to; },
      [](const Running &a_running) -> Target { return a_running.resize.to; });
}

std::optional<Level> AppliedLevel(const RenderPhase &a_phase) noexcept {
  using Applied = std::optional<Level>;
  return Match(
      a_phase, [](const Empty &) -> Applied { return std::nullopt; },
      [](const Shown &a_shown) -> Applied { return a_shown.level; },
      [](const Planning &) -> Applied { return std::nullopt; },
      [](const Drawing &a_drawing) -> Applied { return a_drawing.level; },
      [](const Queued &a_queued) -> Applied { return a_queued.resize.from; },
      [](const Running &a_running) -> Applied {
        return a_running.resize.from;
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
  return !FindResize(a_phase);
}

const UnpublishedDraw *UnpublishedDrawOf(const RenderPhase &a_phase) noexcept {
  if (const Drawing *drawing = Get<Drawing>(a_phase)) {
    return &drawing->draw;
  }
  const Running *running = Get<Running>(a_phase);
  return running ? &running->draw : nullptr;
}

std::optional<ResizeProgress>
ResizeProgressOf(const RenderPhase &a_phase) noexcept {
  const Running *running = Get<Running>(a_phase);
  if (!running) {
    return std::nullopt;
  }

  const Resize &resize = running->resize;
  return ResizeProgress{resize.from, resize.to, running->draw.beganFrame};
}

std::optional<PlanJobId> AwaitedJob(const RenderPhase &a_phase) noexcept {
  const Planning *planning = Get<Planning>(a_phase);
  return planning ? std::optional<PlanJobId>{planning->job} : std::nullopt;
}

std::optional<Level> FailedLevelOf(const RenderPhase &a_phase) noexcept {
  if (const Shown *shown = Get<Shown>(a_phase); shown && shown->failed) {
    return shown->failed->level;
  }

  const Resize *resize = FindResize(a_phase);
  if (!resize || !resize->failed) {
    return std::nullopt;
  }

  return resize->failed->level;
}

bool ScreenStepDownDue(const RenderPhase &a_phase, Millis a_now,
                       Millis a_delay) noexcept {
  const Shown *shown = Get<Shown>(a_phase);
  return shown && shown->smallOnScreen.Due(a_now, a_delay);
}

std::string_view PhaseName(const RenderPhase &a_phase) noexcept {
  using Name = std::string_view;
  return Match(
      a_phase, [](const Empty &) { return Name{"empty"}; },
      [](const Shown &) { return Name{"shown"}; },
      [](const Planning &) { return Name{"planning"}; },
      [](const Drawing &) { return Name{"drawing"}; },
      [](const Queued &) { return Name{"resize_queued"}; },
      [](const Running &) { return Name{"resize_running"}; });
}
}
