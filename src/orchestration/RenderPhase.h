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

struct Held {
  Hold outOfView;
  [[nodiscard]] bool operator==(const Held &) const = default;
};

struct Freed {
  [[nodiscard]] bool operator==(const Freed &) const = default;
};

using Intermediates = std::variant<Held, Freed>;

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

struct Empty {
  [[nodiscard]] bool operator==(const Empty &) const = default;
};

struct Shown {
  ResolutionLevel level = ResolutionLevel::kFull;
  Intermediates intermediates;
  Hold smallOnScreen;
  std::optional<ResolutionLevel> stepUpBlockedFrom;
  std::optional<FailedLevel> failed;
  [[nodiscard]] bool operator==(const Shown &) const = default;
};

struct Planning {
  PlanJobId job{};
  [[nodiscard]] bool operator==(const Planning &) const = default;
};

struct Drawing {
  ResolutionLevel level = ResolutionLevel::kFull;
  UnpublishedDraw draw;
  std::optional<ResolutionLevel> next;
  [[nodiscard]] bool operator==(const Drawing &) const = default;
};

struct Queued {
  [[nodiscard]] bool operator==(const Queued &) const = default;
};

struct Running {
  UnpublishedDraw draw;
  [[nodiscard]] bool operator==(const Running &) const = default;
};

struct Resizing {
  ResolutionLevel from = ResolutionLevel::kFull;
  ResolutionLevel to = ResolutionLevel::kFull;
  IntermediateHolding holding = IntermediateHolding::kHeld;
  std::variant<Queued, Running> stage;
  std::optional<FailedLevel> failed;
  [[nodiscard]] bool operator==(const Resizing &) const = default;
};

using RenderPhase = std::variant<Empty, Shown, Planning, Drawing, Resizing>;

[[nodiscard]] std::optional<ResolutionLevel>
TargetLevel(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::optional<ResolutionLevel>
AppliedLevel(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::optional<ResolutionLevel>
FailedLevelOf(const RenderPhase &a_phase) noexcept;

[[nodiscard]] IntermediateHolding
HoldingOf(const Intermediates &a_intermediates) noexcept;
[[nodiscard]] IntermediateHolding
HoldingOf(const RenderPhase &a_phase) noexcept;

[[nodiscard]] bool MayRelease(const RenderPhase &a_phase) noexcept;
[[nodiscard]] bool MayStepDown(const RenderPhase &a_phase) noexcept;
[[nodiscard]] bool MayEvict(const RenderPhase &a_phase) noexcept;
[[nodiscard]] bool ScreenStepDownDue(const RenderPhase &a_phase, Millis a_now,
                                     Millis a_delay) noexcept;

[[nodiscard]] const UnpublishedDraw *
UnpublishedDrawOf(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::optional<ResizeProgress>
ResizeProgressOf(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::optional<PlanJobId>
AwaitedJob(const RenderPhase &a_phase) noexcept;

[[nodiscard]] std::string_view PhaseName(const RenderPhase &a_phase) noexcept;
}
