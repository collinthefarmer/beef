// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/ClusterReadout.h"
#include "studio/Snapshot.h"

#include <cstdint>
#include <optional>

namespace BetterEnchantmentEffects::Menu {
enum class ChartAction : std::uint8_t { kPick, kCompare };

struct ChartClick {
  std::uint8_t cluster = 0;
  ChartAction action = ChartAction::kPick;
};

struct ChartFocus {
  std::optional<std::uint8_t> picked;
  std::optional<std::uint8_t> compared;
};

struct ClusterSubject {
  const Studio::GeometryRow &geometry;
  ClusterSettings settings;
  std::optional<std::uint8_t> picked;
};

[[nodiscard]] std::uint32_t ClusterInk(std::uint8_t a_cluster,
                                       std::uint8_t a_alpha) noexcept;
[[nodiscard]] bool GuessButton(bool a_matchesGuess);
[[nodiscard]] std::optional<std::uint8_t>
DrawClusterExplorer(const ClusterSubject &a_subject,
                    Studio::ClusterMemo &a_memo, float a_scale);
[[nodiscard]] std::optional<ChartClick>
DrawClusterChart(const Studio::ClusterChart &a_chart, ChartFocus a_focus,
                 float a_scale);
}
