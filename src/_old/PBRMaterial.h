#pragma once

#include "PCH.h"

namespace BetterEnchantmentEffects
{
	struct GlintParameters
	{
		bool  enabled = false;
		float screenSpaceScale = 1.5f;
		float logMicrofacetDensity = 40.f;
		float microfacetRoughness = .015f;
		float densityRandomization = 2.f;
	};

	inline constexpr std::uint32_t kPbrSubsurface = 1u << 0;
	inline constexpr std::uint32_t kPbrTwoLayer = 1u << 1;
	inline constexpr std::uint32_t kPbrColoredCoat = 1u << 2;
	inline constexpr std::uint32_t kPbrFuzz = 1u << 5;
	inline constexpr std::uint32_t kPbrHairMarschner = 1u << 6;

	class PBRMaterialLayout : public RE::BSLightingShaderMaterialBase
	{
	public:
		RE::BSShaderMaterial::Feature      loadedWithFeature;
		std::uint32_t                      pbrFlags;
		float                              coatRoughness;
		float                              coatSpecularLevel;
		RE::NiColor                        fuzzColor;
		float                              fuzzWeight;
		GlintParameters                    glintParameters;
		RE::NiPointer<RE::NiSourceTexture> rmaosTexture;
		RE::NiPointer<RE::NiSourceTexture> emissiveTexture;
		RE::NiPointer<RE::NiSourceTexture> displacementTexture;
		RE::NiPointer<RE::NiSourceTexture> featuresTexture0;
		RE::NiPointer<RE::NiSourceTexture> featuresTexture1;
		std::array<float, 3>               projectedMaterialBaseColorScale;
		float                              projectedMaterialRoughness;
		float                              projectedMaterialSpecularLevel;
		GlintParameters                    projectedMaterialGlintParameters;
		std::string                        inputFilePath;
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

	[[nodiscard]] inline bool IsPBRProperty(const RE::BSLightingShaderProperty* a_property)
	{
		if (!a_property || !a_property->material) {
			return false;
		}
		if (!a_property->flags.any(RE::BSShaderProperty::EShaderPropertyFlag::kVertexLighting)) {
			return false;
		}
		const auto* material = a_property->material;
		if (material->GetType() != RE::BSShaderMaterial::Type::kLighting) {
			return false;
		}
		const auto feature = material->GetFeature();
		if (feature != RE::BSShaderMaterial::Feature::kDefault &&
			feature != RE::BSShaderMaterial::Feature::kMultiTexLandLODBlend) {
			return false;
		}
		const auto vtable = *reinterpret_cast<const std::uintptr_t*>(material);
		return vtable != RE::VTABLE_BSLightingShaderMaterial[0].address();
	}
}
