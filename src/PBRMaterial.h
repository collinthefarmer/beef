#pragma once

// Layout mirror of Community Shaders' BSLightingShaderMaterialPBR (see
// cs/BSLightingShaderMaterialPBR.h and cs/SOURCE.txt). CS adds no virtual
// functions, only overrides, so the vtable slot layout is the base class's and
// only the data members below extend it. Field order and types are copied
// exactly; GlintParameters comes from CS src/TruePBR.h at the same commit.

#include "PCH.h"

namespace WornEnchantmentPBR
{
	struct GlintParameters
	{
		bool  enabled = false;
		float screenSpaceScale = 1.5f;
		float logMicrofacetDensity = 40.f;
		float microfacetRoughness = .015f;
		float densityRandomization = 2.f;
	};

	// PBRFlags from cs/BSLightingShaderMaterialPBR.h.
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

	// CS's own test in BSLightingShaderProperty_GetRenderPasses
	// (reference/community-shaders/TruePBR-GetRenderPasses-excerpt.cpp):
	// kVertexLighting on the property plus a kDefault/kMultiTexLandLODBlend
	// material feature means TruePBR draws it.
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
		// CS's material reports kDefault too, so a vanilla BSLightingShaderMaterial
		// (0xA0 bytes, no PBR fields) is only told apart by its vtable. Reading
		// PBR fields off a vanilla material would run past its allocation.
		const auto vtable = *reinterpret_cast<const std::uintptr_t*>(material);
		return vtable != RE::VTABLE_BSLightingShaderMaterial[0].address();
	}
}
