#pragma once

#include "Recipe.h"
#include "Timing.h"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace WornEnchantmentPBR
{
	struct EffectShaderRecord
	{
		FormKey              key;
		std::string          editorId;
		std::string          fillTexture;
		Timing::EffectParams params;
		float                tileU = 1.0f;
		float                tileV = 1.0f;
		std::uint32_t        flags = 0;

		static constexpr std::uint32_t kGreyscaleToColor = 1 << 1;
		static constexpr std::uint32_t kGreyscaleToAlpha = 1 << 2;

		[[nodiscard]] FormRef Reference() const;
	};

	[[nodiscard]] std::expected<EffectShaderRecord, std::string> ParseEffectShaderRecord(std::string_view a_json);

	struct ImportDefaults
	{
		Timing::ColorPolicy colorPolicy{};
		float               emissiveStrength = 1.0f;
		float               sheenScale = 0.5f;
		float               sheenPhase = 0.5f;
		bool                sheenMirrorV = true;
		float               shimmerScale = 0.1f;
		float               shimmerPhase = 0.25f;
		bool                shimmerTranspose = true;
		float               shimmerNoiseWeight = 0.35f;
		float               shimmerReliefContrast = 3.0f;
		float               glossBoost = 0.25f;
		float               glossContrast = 2.0f;
		float               lightSize = 1.4f;
		float               lightCutoff = 0.05f;
		std::uint32_t       lightMaxPerEffect = 2;
		float               shellAlpha = 0.5f;
		float               shellInflatePercent = 1.0f;
		float               shellInflatePulsePercent = 1.0f;
		MaterialChannel     glowMaskChannel = MaterialChannel::kMetallic;
		std::string         importer = "WornEnchantmentPBR 0.1.0";
	};

	[[nodiscard]] std::string RecipeIdFor(const EffectShaderRecord& a_record);

	[[nodiscard]] Recipe ImportEffectShader(const EffectShaderRecord& a_record, const ImportDefaults& a_defaults = {});
}
