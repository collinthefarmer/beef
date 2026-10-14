// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/MaterialRegions.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace BetterEnchantmentEffects {
struct Grid {
  std::uint32_t side = 0;

  [[nodiscard]] std::size_t At(std::uint32_t a_y,
                               std::uint32_t a_x) const noexcept {
    return static_cast<std::size_t>(a_y) * side + a_x;
  }
  [[nodiscard]] std::size_t Size() const noexcept {
    return static_cast<std::size_t>(side) * side;
  }
};

template <typename Visit>
void ForEachNeighbour(const Grid &a_grid, std::size_t a_index, Visit a_visit) {
  const auto y = static_cast<std::uint32_t>(a_index / a_grid.side);
  const auto x = static_cast<std::uint32_t>(a_index % a_grid.side);
  if (y > 0) {
    a_visit(a_grid.At(y - 1, x));
  }
  if (y + 1 < a_grid.side) {
    a_visit(a_grid.At(y + 1, x));
  }
  if (x > 0) {
    a_visit(a_grid.At(y, x - 1));
  }
  if (x + 1 < a_grid.side) {
    a_visit(a_grid.At(y, x + 1));
  }
}

[[nodiscard]] inline bool IsCovered(const MaterialRaster &a_raster,
                                    std::size_t a_index) noexcept {
  return a_raster.charts[a_index] != kNoChart;
}

[[nodiscard]] inline std::size_t CoveredCount(const MaterialRaster &a_raster) {
  return static_cast<std::size_t>(std::ranges::count_if(
      a_raster.charts, [](std::uint16_t a_c) { return a_c != kNoChart; }));
}
}
