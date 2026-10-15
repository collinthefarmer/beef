// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <span>
#include <string_view>

namespace BetterEnchantmentEffects {
struct ShaderFile {
  std::string_view name;
  std::string_view text;
};

[[nodiscard]] std::span<const ShaderFile> ShaderFiles() noexcept;
}
