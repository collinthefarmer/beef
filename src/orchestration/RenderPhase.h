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

struct FailedLevel {
  ResolutionLevel level = ResolutionLevel::kFull;
  std::optional<ResolutionLevel> wantedThen;
  [[nodiscard]] bool operator==(const FailedLevel &) const = default;
};

struct WithoutRender {
  [[nodiscard]] bool operator==(const WithoutRender &) const = default;
};

struct Live {
  ResolutionLevel level = ResolutionLevel::kFull;
  Intermediates intermediates;
  Hold smallOnScreen;
  std::optional<ResolutionLevel> stepUpBlockedFrom;
  std::optional<FailedLevel> failed;
  [[nodiscard]] bool operator==(const Live &) const = default;
};

struct AwaitingPlan {
  PlanJobId job{};
  [[nodiscard]] bool operator==(const AwaitingPlan &) const = default;
};

struct FirstDrawing {
  ResolutionLevel level = ResolutionLevel::kFull;
  UnpublishedDraw draw;
  std::optional<ResolutionLevel> next;
  [[nodiscard]] bool operator==(const FirstDrawing &) const = default;
};

struct ResizeQueued {
  [[nodiscard]] bool operator==(const ResizeQueued &) const = default;
};

struct ResizeRunning {
  UnpublishedDraw draw;
  [[nodiscard]] bool operator==(const ResizeRunning &) const = default;
};

struct Resizing {
  ResolutionLevel from = ResolutionLevel::kFull;
  ResolutionLevel to = ResolutionLevel::kFull;
  IntermediateHolding holding = IntermediateHolding::kHeld;
  std::variant<ResizeQueued, ResizeRunning> stage;
  [[nodiscard]] bool operator==(const Resizing &) const = default;
};

using RenderPhase =
    std::variant<WithoutRender, Live, AwaitingPlan, FirstDrawing, Resizing>;

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
[[nodiscard]] std::optional<ResolutionLevel>
FailedLevelOf(const RenderPhase &a_phase) noexcept;
[[nodiscard]] bool ScreenStepDownDue(const RenderPhase &a_phase, Millis a_now,
                                     Millis a_delay) noexcept;
[[nodiscard]] std::string_view PhaseName(const RenderPhase &a_phase) noexcept;
}
