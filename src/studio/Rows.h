#pragma once

#include "recipe/Recipe.h"
#include "recipe/Signals.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
[[nodiscard]] SignalRow SignalRowOf(const Signal &a_signal,
                                    const RowTypes &a_rows,
                                    std::size_t a_references);
[[nodiscard]] TextRow CurveRowOf(const Curve &a_curve,
                                 std::size_t a_references);
[[nodiscard]] TextRow MaskRowOf(const Mask &a_mask, std::size_t a_references);
[[nodiscard]] LayerRow LayerRowOf(const Recipe &a_recipe, const Layer &a_layer,
                                  Slot a_slot);
[[nodiscard]] std::vector<ScalarRow>
ScalarRowsOf(const Recipe &a_recipe, const SurfaceOutput &a_output);
[[nodiscard]] OutputRow OutputRowOf(const Recipe &a_recipe,
                                    std::size_t a_index);
}
