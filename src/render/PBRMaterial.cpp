#include "PBRMaterial.h"

namespace BetterEnchantmentEffects {
bool IsPBRProperty(const RE::BSLightingShaderProperty *a_property) noexcept {
  if (!a_property || !a_property->material) {
    return false;
  }
  if (!a_property->flags.any(
          RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
    return false;
  }
  const RE::BSShaderMaterial *material = a_property->material;
  if (material->GetType() != RE::BSShaderMaterial::Type::kLighting) {
    return false;
  }
  const RE::BSShaderMaterial::Feature feature = material->GetFeature();
  if (feature != RE::BSShaderMaterial::Feature::kDefault &&
      feature != RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend) {
    return false;
  }
  const std::uintptr_t vtable =
      *reinterpret_cast<const std::uintptr_t *>(material);
  return vtable != RE::VTABLE_BSLightingShaderMaterial[0].address();
}
}
