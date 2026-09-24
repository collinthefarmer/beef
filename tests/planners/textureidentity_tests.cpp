// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/TextureIdentity.h"
#include "test_support.h"

#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
void CacheKeyIgnoresCase() {
  Equal(ImageCacheKey("Textures\\Effects\\Glow.DDS"),
        std::string{"textures\\effects\\glow.dds"},
        "the cache key lowercases the path");
  Equal(ImageCacheKey("textures\\effects\\glow.dds"),
        ImageCacheKey("TEXTURES\\EFFECTS\\GLOW.DDS"),
        "two spellings of one path share a key");
  Equal(ImageCacheKey(""), std::string{}, "an empty path keys as empty");
  Check(ImageCacheKey("a/b.dds") != ImageCacheKey("a\\b.dds"),
        "separators are part of the identity");
}

void PlaceholderExtentIsTiny() {
  Check(IsPlaceholderExtent(0, 0), "a zero extent is a placeholder");
  Check(IsPlaceholderExtent(4, 4), "the four-texel extent is a placeholder");
  Check(IsPlaceholderExtent(4, 1024),
        "a placeholder on either axis makes the texture a placeholder");
  Check(!IsPlaceholderExtent(5, 5), "five texels each way is a real texture");
  Check(!IsPlaceholderExtent(1024, 1024), "a full map is a real texture");
}
}

int main() {
  CacheKeyIgnoresCase();
  PlaceholderExtentIsTiny();
  return test::Finish("planners textureidentity");
}
