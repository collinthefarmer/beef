#include "render/SourceSampling.h"
#include <algorithm>
#include <cctype>

namespace BetterEnchantmentEffects {
std::string ImageCacheKey(std::string_view a_path) {
  std::string out{a_path};
  for (char &c : out) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return out;
}

bool IsNonPlaceholderTexture(const TextureRef &a_texture) {
  const std::optional<TextureLab::Extent> extent =
      TextureLab::ExtentOf(a_texture.get());
  return extent && extent->width > 4 && extent->height > 4;
}

TextureRef MaterialTexture(MaterialMap a_map,
                           const MaterialInputs &a_material) {
  switch (a_map) {
  case MaterialMap::kDiffuse:
    return a_material.diffuse;
  case MaterialMap::kNormal:
    return a_material.normal;
  case MaterialMap::kRmaos:
    return a_material.rmaos;
  case MaterialMap::kDisplacement:
    return a_material.displacement;
  case MaterialMap::kNone:
    return nullptr;
  }
  return nullptr;
}

TextureLab::LayerInput ResolveSampling(const PreparedSource &a_source,
                                       const SignalState &a_signals) {
  TextureLab::LayerInput input = a_source.sampling;
  if (a_source.scroll) {
    const Vec2 scroll = a_signals.Resolve(*a_source.scroll);
    input.transform.uOffset = scroll.x;
    input.transform.vOffset = scroll.y;
  }
  if (a_source.tile) {
    const Vec2 tile = a_signals.Resolve(*a_source.tile);
    input.transform.tileU = std::max(tile.x, 0.01f);
    input.transform.tileV = std::max(tile.y, 0.01f);
  }
  return input;
}
}
