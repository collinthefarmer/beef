#include "planners/TextureIdentity.h"

#include <cctype>

namespace BetterEnchantmentEffects {
std::string ImageCacheKey(std::string_view a_path) {
  std::string out{a_path};
  for (char &c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool IsPlaceholderExtent(std::uint32_t a_width,
                         std::uint32_t a_height) noexcept {
  return a_width <= kPlaceholderTextureExtent ||
         a_height <= kPlaceholderTextureExtent;
}
}
