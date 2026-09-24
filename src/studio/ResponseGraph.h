// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/Snapshot.h"

#include <array>
#include <expected>
#include <optional>
#include <string>

namespace BetterEnchantmentEffects::Studio {
struct ResponseGraph {
  std::array<float, 65> values{};
  float seconds = 1.0f;
  std::optional<float> live;
  std::string caption;
};

[[nodiscard]] std::expected<ResponseGraph, std::string>
BuildResponseGraph(const SignalRow &a_signal, const RecipeRow &a_recipe);
}
