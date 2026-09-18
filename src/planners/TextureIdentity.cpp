#include "planners/TextureIdentity.h"

#include "Core.h"

namespace BetterEnchantmentEffects {
std::string ImageCacheKey(std::string_view a_path) { return Lower(a_path); }

bool IsPlaceholderExtent(std::uint32_t a_width,
                         std::uint32_t a_height) noexcept {
  return a_width <= kPlaceholderTextureExtent ||
         a_height <= kPlaceholderTextureExtent;
}
}
