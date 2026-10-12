// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/ClusterReadout.h"

#include <cstdint>
#include <optional>

namespace BetterEnchantmentEffects::Menu {
enum class ChartAction : std::uint8_t { kPick, kCompare };

struct ChartClick {
  std::uint8_t cluster = 0;
  ChartAction action = ChartAction::kPick;
};

struct ChartFocus {
  std::uint8_t picked = 0;
  std::optional<std::uint8_t> compared;
};

[[nodiscard]] std::uint32_t ClusterInk(std::uint8_t a_cluster,
                                       std::uint8_t a_alpha) noexcept;
[[nodiscard]] std::optional<ChartClick>
DrawClusterChart(const Studio::ClusterChart &a_chart, ChartFocus a_focus,
                 float a_scale);
}
