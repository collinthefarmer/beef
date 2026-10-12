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
#include <type_traits>
#include <variant>

namespace BetterEnchantmentEffects::Orchestration {
using Level = ResolutionLevel;

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
  Level level = Level::kFull;
  std::optional<Level> wantedThen;

  [[nodiscard]] bool operator==(const FailedLevel &) const = default;
};

struct Empty {
  [[nodiscard]] bool operator==(const Empty &) const = default;
};

struct Shown {
  Level level = Level::kFull;
  Intermediates intermediates;
  Hold smallOnScreen;
  std::optional<Level> waitingForRoomFrom;
  std::optional<FailedLevel> failed;

  [[nodiscard]] bool operator==(const Shown &) const = default;
};

struct Planning {
  PlanJobId job{};

  [[nodiscard]] bool operator==(const Planning &) const = default;
};

struct Drawing {
  Level level = Level::kFull;
  UnpublishedDraw draw;
  std::optional<Level> next;

  [[nodiscard]] bool operator==(const Drawing &) const = default;
};

struct Resize {
  Level from = Level::kFull;
  Level to = Level::kFull;
  IntermediateHolding holding = IntermediateHolding::kHeld;
  std::optional<FailedLevel> failed;

  [[nodiscard]] bool operator==(const Resize &) const = default;
};

struct Queued {
  Resize resize;

  [[nodiscard]] bool operator==(const Queued &) const = default;
};

struct Running {
  Resize resize;
  UnpublishedDraw draw;

  [[nodiscard]] bool operator==(const Running &) const = default;
};

struct RestageRequested {
  [[nodiscard]] bool operator==(const RestageRequested &) const = default;
};

struct RestagePlanning {
  PlanJobId job{};

  [[nodiscard]] bool operator==(const RestagePlanning &) const = default;
};

struct RestageDrawing {
  Level level = Level::kFull;
  UnpublishedDraw draw;
  std::uint64_t chargedBytes = 0;

  [[nodiscard]] bool operator==(const RestageDrawing &) const = default;
};

using RestageStep =
    std::variant<RestageRequested, RestagePlanning, RestageDrawing>;

struct Restaging {
  Shown shown;
  std::uint64_t shownBytes = 0;
  RestageStep step;

  [[nodiscard]] bool operator==(const Restaging &) const = default;
};

using RenderPhase =
    std::variant<Empty, Shown, Planning, Drawing, Queued, Running, Restaging>;

static_assert(std::is_trivially_copyable_v<RenderPhase>);

[[nodiscard]] const Resize *FindResize(const RenderPhase &a_phase) noexcept;
[[nodiscard]] Resize *FindResize(RenderPhase &a_phase) noexcept;

[[nodiscard]] std::optional<Level>
TargetLevel(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::optional<Level>
AppliedLevel(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::optional<Level>
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
[[nodiscard]] const RestageDrawing *
RestageDrawingOf(const RenderPhase &a_phase) noexcept;
[[nodiscard]] std::uint64_t
StagedBytesOf(const Restaging &a_restaging) noexcept;

[[nodiscard]] std::string_view PhaseName(const RenderPhase &a_phase) noexcept;
}
