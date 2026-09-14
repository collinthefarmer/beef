#include "render/PBRMaterial.h"

namespace BetterEnchantmentEffects {
namespace {
bool CommunityShadersMaterial(const RE::BSShaderMaterial *a_material) noexcept {
  const auto module = REX::W32::GetModuleHandleW(L"CommunityShaders.dll");
  if (!module) {
    return false;
  }
  const void *vtable = *reinterpret_cast<const void *const *>(a_material);
  REX::W32::MEMORY_BASIC_INFORMATION region{};
  return REX::W32::VirtualQuery(vtable, &region, sizeof(region)) ==
             sizeof(region) &&
         region.allocationBase == module;
}
}

bool IsPBRProperty(const RE::BSLightingShaderProperty *a_property) noexcept {
  if (!a_property || !a_property->material) {
    return false;
  }
  if (!a_property->flags.any(
          RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
    return false;
  }
  const RE::BSShaderMaterial *material = a_property->material;
  if (!CommunityShadersMaterial(material) ||
      material->GetType() != RE::BSShaderMaterial::Type::kLighting) {
    return false;
  }
  const RE::BSShaderMaterial::Feature feature = material->GetFeature();
  return feature == RE::BSShaderMaterial::Feature::kDefault ||
         feature == RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend;
}

std::optional<PbrMaterial>
PbrMaterial::Bind(RE::BSLightingShaderProperty *a_property) {
  if (!IsPBRProperty(a_property)) {
    return std::nullopt;
  }
  return PbrMaterial{a_property};
}

PbrMaterial::PbrMaterial(RE::BSLightingShaderProperty *a_property)
    : material_(static_cast<PBRMaterialLayout *>(a_property->material)),
      property_(a_property) {}

bool PbrMaterial::Attached() const noexcept {
  return material_ && property_ && property_->material == material_.get();
}

bool PbrMaterial::TextureSlotsValid() const {
  if (!Attached()) {
    return false;
  }
  const std::array textures{
      material_->rmaosTexture.get(), material_->emissiveTexture.get(),
      material_->displacementTexture.get(), material_->featuresTexture0.get(),
      material_->featuresTexture1.get()};
  for (auto *texture : textures) {
    if (!texture || !netimmerse_cast<RE::NiSourceTexture *>(
                        static_cast<RE::NiTexture *>(texture))) {
      return false;
    }
  }
  return true;
}
}
