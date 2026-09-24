// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
[[nodiscard]] bool IsWholeReference(std::string_view a_text) noexcept;
[[nodiscard]] std::optional<std::uint32_t> WholeNumber(std::string_view a_text,
                                                       std::uint32_t a_max);
[[nodiscard]] std::optional<std::array<float, 5>>
FiveNumbers(std::string_view a_text);
[[nodiscard]] std::vector<std::string> SplitNames(std::string_view a_text);
[[nodiscard]] std::optional<Vec3> LiteralColor(std::string_view a_text);
[[nodiscard]] std::string LiteralColorText(const Vec3 &a_color);
}
