// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/PBRMaterial.h"

#include "Core.h"
#include "Identity.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

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
    : layout_(static_cast<PBRMaterialLayout *>(a_property->material)),
      property_(a_property) {}

bool PbrMaterial::Attached() const noexcept {
  return layout_ && property_ && property_->material == layout_.get();
}

RE::BSLightingShaderProperty *LightingPropertyOf(RE::BSGeometry *a_geometry) {
  if (!a_geometry) {
    return nullptr;
  }
  RE::NiProperty *property = a_geometry->GetGeometryRuntimeData()
                                 .properties[RE::BSGeometry::States::kEffect]
                                 .get();
  return property ? netimmerse_cast<RE::BSLightingShaderProperty *>(property)
                  : nullptr;
}

std::size_t PbrMaterial::PresenterTextures() const {
  if (!Attached()) {
    return 0;
  }
  const std::string folder = Identity::PresenterTextureFolder();
  const std::array<const RE::NiSourceTexture *, 7> textures{
      layout_->diffuseTexture.get(),      layout_->normalTexture.get(),
      layout_->rmaosTexture.get(),        layout_->emissiveTexture.get(),
      layout_->displacementTexture.get(), layout_->featuresTexture0.get(),
      layout_->featuresTexture1.get()};
  return static_cast<std::size_t>(std::ranges::count_if(
      textures, [&](const RE::NiSourceTexture *a_texture) {
        return a_texture && a_texture->name.c_str() &&
               EqualsIgnoreCase(
                   std::string_view{a_texture->name.c_str()}.substr(
                       0, folder.size()),
                   folder);
      }));
}

bool PbrMaterial::TextureSlotsValid() const {
  if (!Attached()) {
    return false;
  }
  const std::array textures{
      layout_->rmaosTexture.get(), layout_->emissiveTexture.get(),
      layout_->displacementTexture.get(), layout_->featuresTexture0.get(),
      layout_->featuresTexture1.get()};
  for (auto *texture : textures) {
    if (!texture || !netimmerse_cast<RE::NiSourceTexture *>(
                        static_cast<RE::NiTexture *>(texture))) {
      return false;
    }
  }
  return true;
}
}
