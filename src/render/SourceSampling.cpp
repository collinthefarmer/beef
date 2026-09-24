// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/SourceSampling.h"
#include <algorithm>

namespace BetterEnchantmentEffects {
bool IsNonPlaceholderTexture(const TextureRef &a_texture) {
  const std::optional<TextureLab::Extent> extent =
      TextureLab::ExtentOf(a_texture.get());
  return extent && !IsPlaceholderExtent(extent->width, extent->height);
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
