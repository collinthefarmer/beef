// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/RenderPhase.h"

namespace BetterEnchantmentEffects::Orchestration {
IntermediateHolding HoldingOf(const Intermediates &a_intermediates) noexcept {
  return Is<IntermediatesReleased>(a_intermediates)
             ? IntermediateHolding::kReleased
             : IntermediateHolding::kHeld;
}

IntermediateHolding HoldingOf(const RenderPhase &a_phase) noexcept {
  if (const Live *live = Get<Live>(a_phase)) {
    return HoldingOf(live->intermediates);
  }
  if (const Resizing *resizing = Get<Resizing>(a_phase)) {
    return HoldingOf(resizing->intermediates);
  }
  return IntermediateHolding::kHeld;
}

std::optional<ResolutionLevel>
TargetLevel(const RenderPhase &a_phase) noexcept {
  return Match(
      a_phase,
      [](const Live &a_live) -> std::optional<ResolutionLevel> {
        return a_live.level;
      },
      [](const AwaitingPlan &) -> std::optional<ResolutionLevel> {
        return std::nullopt;
      },
      [](const FirstDrawing &a_drawing) -> std::optional<ResolutionLevel> {
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
      [](const Live &a_live) -> std::optional<ResolutionLevel> {
        return a_live.level;
      },
      [](const AwaitingPlan &) -> std::optional<ResolutionLevel> {
        return std::nullopt;
      },
      [](const FirstDrawing &a_drawing) -> std::optional<ResolutionLevel> {
        return a_drawing.level;
      },
      [](const Resizing &a_resizing) -> std::optional<ResolutionLevel> {
        return a_resizing.from;
      });
}

bool MayRelease(const RenderPhase &a_phase) noexcept {
  const Live *live = Get<Live>(a_phase);
  return live && Is<IntermediatesHeld>(live->intermediates);
}

bool BudgetBusy(const RenderPhase &a_phase) noexcept {
  return !Is<Live>(a_phase);
}

const UnpublishedDraw *UnpublishedDrawOf(const RenderPhase &a_phase) noexcept {
  if (const FirstDrawing *drawing = Get<FirstDrawing>(a_phase)) {
    return &drawing->draw;
  }
  if (const Resizing *resizing = Get<Resizing>(a_phase)) {
    if (const ResizeRunning *running = Get<ResizeRunning>(resizing->stage)) {
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
  const ResizeRunning *running = Get<ResizeRunning>(resizing->stage);
  return ResizeProgress{resizing->from, resizing->to,
                        running ? running->draw.beganFrame : 0};
}

std::optional<PlanJobId> AwaitedJob(const RenderPhase &a_phase) noexcept {
  const AwaitingPlan *awaiting = Get<AwaitingPlan>(a_phase);
  return awaiting ? std::optional<PlanJobId>{awaiting->job} : std::nullopt;
}

bool StepDownDue(const RenderPhase &a_phase, Millis a_now,
                 Millis a_delay) noexcept {
  const Live *live = Get<Live>(a_phase);
  return live && live->smallOnScreen.Due(a_now, a_delay);
}

std::string_view PhaseName(const RenderPhase &a_phase) noexcept {
  return Match(
      a_phase, [](const Live &) { return std::string_view{"live"}; },
      [](const AwaitingPlan &) { return std::string_view{"awaiting_plan"}; },
      [](const FirstDrawing &) { return std::string_view{"first_drawing"}; },
      [](const Resizing &a_resizing) {
        return Is<ResizeQueued>(a_resizing.stage)
                   ? std::string_view{"resize_queued"}
                   : std::string_view{"resizing"};
      });
}
}
