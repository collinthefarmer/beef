// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "planners/Hold.h"
#include "planners/RenderBudget.h"
#include "planners/ResolutionLevel.h"
#include "planners/ResolutionResize.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <variant>

namespace BetterEnchantmentEffects::Orchestration {
enum class PlanJobId : std::uint64_t {};

struct IntermediatesHeld {
  Hold outOfView;
  [[nodiscard]] bool operator==(const IntermediatesHeld &) const = default;
};

struct IntermediatesReleased {
  [[nodiscard]] bool operator==(const IntermediatesReleased &) const = default;
};

using Intermediates = std::variant<IntermediatesHeld, IntermediatesReleased>;

struct UnpublishedDraw {
  std::size_t total = 0;
  std::size_t drawn = 0;
  std::uint64_t beganFrame = 0;
  std::uint64_t drawTicks = 0;
  [[nodiscard]] bool operator==(const UnpublishedDraw &) const = default;
};

struct Live {
  ResolutionLevel level = ResolutionLevel::kFull;
  Intermediates intermediates;
  Hold smallOnScreen;
  std::optional<ResolutionLevel> stepUpBlockedFrom;
  std::optional<ResolutionLevel> failedLevel;
};

struct AwaitingPlan {
  PlanJobId job{};
};

struct FirstDrawing {
  ResolutionLevel level = ResolutionLevel::kFull;
  UnpublishedDraw draw;
  std::optional<ResolutionLevel> next;
};

struct ResizeQueued {};

struct ResizeRunning {
  UnpublishedDraw draw;
};

struct Resizing {
  ResolutionLevel from = ResolutionLevel::kFull;
  ResolutionLevel to = ResolutionLevel::kFull;
  Intermediates intermediates;
  std::variant<ResizeQueued, ResizeRunning> stage;
};

using RenderPhase = std::variant<Live, AwaitingPlan, FirstDrawing, Resizing>;

[[nodiscard]] IntermediateHolding
HoldingOf(const Intermediates &a_intermediates) noexcept;
[[nodiscard]] IntermediateHolding
HoldingOf(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::optional<ResolutionLevel>
TargetLevel(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::optional<ResolutionLevel>
AppliedLevel(const RenderPhase &a_phase) noexcept;
[[nodiscard]] bool MayRelease(const RenderPhase &a_phase) noexcept;
[[nodiscard]] bool BudgetBusy(const RenderPhase &a_phase) noexcept;
[[nodiscard]] const UnpublishedDraw *
UnpublishedDrawOf(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::optional<ResizeProgress>
ResizeProgressOf(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::optional<PlanJobId>
AwaitedJob(const RenderPhase &a_phase) noexcept;
[[nodiscard]] bool StepDownDue(const RenderPhase &a_phase, Millis a_now,
                               Millis a_delay) noexcept;
[[nodiscard]] std::string_view PhaseName(const RenderPhase &a_phase) noexcept;
}
