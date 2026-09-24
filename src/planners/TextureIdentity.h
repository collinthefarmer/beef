// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
inline constexpr std::uint32_t kPlaceholderTextureExtent = 4;

[[nodiscard]] std::string ImageCacheKey(std::string_view a_path);
[[nodiscard]] bool IsPlaceholderExtent(std::uint32_t a_width,
                                       std::uint32_t a_height) noexcept;
}
