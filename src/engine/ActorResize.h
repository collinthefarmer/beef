// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "diagnostics/WarningHistory.h"
#include "engine/LiveActor.h"
#include "planners/DrawPacing.h"
#include "planners/InstallProgram.h"
#include "planners/ResolutionLevel.h"
#include "planners/TargetCost.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <span>
#include <string>

namespace BetterEnchantmentEffects {
struct ResizeSwap {
  ResolutionLevel from = ResolutionLevel::kFull;
  ResolutionLevel to = ResolutionLevel::kFull;
  std::size_t stacks = 0;
  std::size_t drawn = 0;
  TargetCost targets;
};

[[nodiscard]] std::shared_ptr<RenderInstance>
AttachedRender(const LiveActor &a_state);
[[nodiscard]] std::expected<std::size_t, std::string>
StartResize(LiveActor &a_state, ResolutionLevel a_to);
[[nodiscard]] DrawPace DrawResize(LiveActor &a_state, std::uint64_t a_frame,
                                  std::uint64_t a_nowMS,
                                  WarningHistory &a_warnings, DrawPace a_pace,
                                  std::uint64_t a_budgetUs);
[[nodiscard]] std::expected<ResizeSwap, std::string>
FinishResize(LiveActor &a_state);
void DropResize(LiveActor &a_state);
[[nodiscard]] const ResizeInFlight *
InFlightOf(const LiveActor &a_state) noexcept;
[[nodiscard]] std::size_t
DrawnStacks(const ResizeInFlight &a_inFlight) noexcept;
[[nodiscard]] const RenderInstance *
ResizingInstance(const LiveActor &a_state) noexcept;
void StartFirstDraw(LiveActor &a_state, std::span<const GeometrySlot> a_slots);
[[nodiscard]] std::size_t DrawnSlots(const FirstDraw &a_draw) noexcept;
[[nodiscard]] LiveGeometry *GeometryOf(LiveActor &a_state,
                                       const HiddenSlot &a_slot) noexcept;
}
