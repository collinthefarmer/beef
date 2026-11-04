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

void NamedTextureIgnoresCaseAndKeepsExtent() {
  Check(!NamedTextureOf("", 512, 512), "a texture without a name has none");
  Check(NamedTextureOf("Armor\\Iron_M.dds", 512, 512) ==
            NamedTextureOf("armor\\iron_m.DDS", 512, 512),
        "two spellings of one name are one texture");
  Check(NamedTextureOf("armor\\iron_m.dds", 512, 512) !=
            NamedTextureOf("armor\\iron_m.dds", 1024, 1024),
        "a reload at another extent is another texture");
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
  NamedTextureIgnoresCaseAndKeepsExtent();
  PlaceholderExtentIsTiny();
  return test::Finish("planners textureidentity");
}
