// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
inline constexpr std::uint32_t kPlaceholderTextureExtent = 4;

struct NamedTexture {
  std::string name;
  std::uint32_t width = 0;
  std::uint32_t height = 0;

  [[nodiscard]] bool operator==(const NamedTexture &) const = default;
};

[[nodiscard]] std::string ImageCacheKey(std::string_view a_path);
[[nodiscard]] std::optional<NamedTexture>
NamedTextureOf(std::string_view a_name, std::uint32_t a_width,
               std::uint32_t a_height);
[[nodiscard]] bool IsPlaceholderExtent(std::uint32_t a_width,
                                       std::uint32_t a_height) noexcept;
}
