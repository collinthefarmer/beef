// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"
#include "recipe/Signals.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
[[nodiscard]] bool WritesCell(const OutputRow &a_output, Surface a_surface,
                              Slot a_slot) noexcept;

[[nodiscard]] LightRow LightRowOf(const Recipe &a_recipe);
[[nodiscard]] LightRow LightRowOf(const Recipe &a_recipe, std::size_t a_output);
[[nodiscard]] ShellRow ShellRowOf(const Recipe &a_recipe);
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
[[nodiscard]] SourceRow SourceRowOf(const Source &a_source,
                                    std::size_t a_references);
[[nodiscard]] std::optional<SourceKind> SourceKindOf(const SourceRow &a_row);
}
