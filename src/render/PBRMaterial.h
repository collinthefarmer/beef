#pragma once

#include "PCH.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace BetterEnchantmentEffects {
struct GlintParameters {
  bool enabled = false;
  float screenSpaceScale = 1.5f;
  float logMicrofacetDensity = 40.0f;
  float microfacetRoughness = 0.015f;
  float densityRandomization = 2.0f;
};

inline constexpr std::uint32_t kPbrSubsurface = 1u << 0;
inline constexpr std::uint32_t kPbrTwoLayer = 1u << 1;
inline constexpr std::uint32_t kPbrColoredCoat = 1u << 2;
inline constexpr std::uint32_t kPbrFuzz = 1u << 5;
inline constexpr std::uint32_t kPbrHairMarschner = 1u << 6;

class PBRMaterialLayout : public RE::BSLightingShaderMaterialBase {
public:
  RE::BSShaderMaterial::Feature loadedWithFeature;
  std::uint32_t pbrFlags;
  float coatRoughness;
  float coatSpecularLevel;
  RE::NiColor fuzzColor;
  float fuzzWeight;
  GlintParameters glintParameters;
  RE::NiPointer<RE::NiSourceTexture> rmaosTexture;
  RE::NiPointer<RE::NiSourceTexture> emissiveTexture;
  RE::NiPointer<RE::NiSourceTexture> displacementTexture;
  RE::NiPointer<RE::NiSourceTexture> featuresTexture0;
  RE::NiPointer<RE::NiSourceTexture> featuresTexture1;
  std::array<float, 3> projectedMaterialBaseColorScale;
  float projectedMaterialRoughness;
  float projectedMaterialSpecularLevel;
  GlintParameters projectedMaterialGlintParameters;
  std::string inputFilePath;
};

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(PBRMaterialLayout, loadedWithFeature) == 0xA0);
static_assert(offsetof(PBRMaterialLayout, glintParameters) == 0xC0);
static_assert(offsetof(PBRMaterialLayout, rmaosTexture) == 0xD8);
static_assert(offsetof(PBRMaterialLayout, emissiveTexture) == 0xE0);
static_assert(offsetof(PBRMaterialLayout, featuresTexture1) == 0xF8);
static_assert(offsetof(PBRMaterialLayout, inputFilePath) == 0x128);
static_assert(sizeof(PBRMaterialLayout) == 0x148);
#pragma clang diagnostic pop

[[nodiscard]] bool
IsPBRProperty(const RE::BSLightingShaderProperty *a_property) noexcept;

class PbrMaterial {
public:
  [[nodiscard]] static std::optional<PbrMaterial>
  Bind(RE::BSLightingShaderProperty *a_property);
  [[nodiscard]] bool Attached() const noexcept;
  [[nodiscard]] bool TextureSlotsValid() const;

private:
  friend class SlotWriter;
  friend struct MaterialInputs;
  explicit PbrMaterial(RE::BSLightingShaderProperty *a_property);

  RE::BSTSmartPointer<PBRMaterialLayout> material_;
  RE::NiPointer<RE::BSLightingShaderProperty> property_;
};
}
