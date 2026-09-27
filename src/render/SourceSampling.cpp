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

}
