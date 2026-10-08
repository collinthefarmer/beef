// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "diagnostics/WarningHistory.h"
#include "engine/LiveActor.h"
#include "planners/ResolutionLevel.h"
#include "planners/TargetCost.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>

namespace BetterEnchantmentEffects {
struct ResizeSwap {
  ResolutionLevel from = ResolutionLevel::kFull;
  ResolutionLevel to = ResolutionLevel::kFull;
  std::uint64_t frames = 0;
  std::size_t stacks = 0;
  std::size_t drawn = 0;
  TargetCost targets;
};

[[nodiscard]] std::shared_ptr<RenderInstance>
AttachedRender(const LiveActor &a_state);
void RequestResize(LiveActor &a_state, ResolutionLevel a_level);
[[nodiscard]] std::expected<void, std::string>
StartResize(LiveActor &a_state, std::uint64_t a_frame);
void DrawResize(LiveActor &a_state, std::uint64_t a_frame,
                std::uint64_t a_nowMS, WarningHistory &a_warnings);
[[nodiscard]] std::optional<ResizeSwap> FinishResize(LiveActor &a_state,
                                                     std::uint64_t a_frame);
[[nodiscard]] const RenderInstance *
ResizingInstance(const LiveActor &a_state) noexcept;
}
