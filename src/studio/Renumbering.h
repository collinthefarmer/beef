// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"
#include "studio/View.h"

#include <compare>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
inline constexpr std::size_t kMaxRenumberedEntries = 256;

struct LayerPlace {
  std::size_t output = 0;
  std::size_t layer = 0;
  [[nodiscard]] auto operator<=>(const LayerPlace &) const = default;
};

struct OutputRenumbering {
  std::optional<std::size_t> to;
  std::vector<std::optional<std::size_t>> layers;
  [[nodiscard]] bool operator==(const OutputRenumbering &) const = default;
};

struct Renumbering {
  std::vector<OutputRenumbering> outputs;
  [[nodiscard]] bool operator==(const Renumbering &) const = default;
};

[[nodiscard]] std::optional<Renumbering> RenumberingOf(const Recipe &a_before,
                                                       const Recipe &a_after);
[[nodiscard]] std::optional<std::size_t>
OutputAfter(const Renumbering &a_renumbering, std::size_t a_output) noexcept;
[[nodiscard]] std::optional<LayerPlace>
LayerAfter(const Renumbering &a_renumbering, LayerPlace a_place) noexcept;
[[nodiscard]] Isolation RenumberedIsolation(Isolation a_isolation,
                                            std::string_view a_recipe,
                                            const Renumbering &a_renumbering);
[[nodiscard]] View RenumberedView(View a_view, std::string_view a_recipe,
                                  const Renumbering &a_renumbering);
}
