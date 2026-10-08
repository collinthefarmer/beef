// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ResolutionLevel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace BetterEnchantmentEffects {
struct ResizeIdle {
  [[nodiscard]] bool operator==(const ResizeIdle &) const = default;
};

struct ResizePending {
  ResolutionLevel level = ResolutionLevel::kFull;
  [[nodiscard]] bool operator==(const ResizePending &) const = default;
};

struct ResizeProgress {
  ResolutionLevel from = ResolutionLevel::kFull;
  ResolutionLevel to = ResolutionLevel::kFull;
  std::uint64_t beganFrame = 0;
  [[nodiscard]] bool operator==(const ResizeProgress &) const = default;
};

enum class ResizeDraw { kOutOfView, kDrawing, kDrawn };

struct ResizeCandidate {
  bool pending = false;
  float screenFraction = 0.0f;
};

inline constexpr std::size_t kResizesInFlight = 1;
inline constexpr std::uint64_t kResizeSwapAfterFrames = 60;

[[nodiscard]] ResizeDraw DrawOf(bool a_inView, std::size_t a_drawn,
                                std::size_t a_stacks) noexcept;
[[nodiscard]] bool SwapDue(const ResizeProgress &a_progress, ResizeDraw a_draw,
                           std::uint64_t a_frame) noexcept;
[[nodiscard]] std::optional<std::size_t>
NextBuild(std::span<const ResizeCandidate> a_candidates,
          std::size_t a_inFlight) noexcept;
}
