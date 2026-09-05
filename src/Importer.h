#pragma once

// EFSH record to recipe. The record arrives as plain data (from the engine in
// game, from tools/efsh_dump.py fixtures in tests), so the import itself is
// engine-free. The shipped defaults reproduce the proof of concept's look;
// the output is the canonical file shape (schema/example-magicka.json).

#include "Recipe.h"
#include "Timing.h"

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>

namespace WornEnchantmentPBR
{
	// The fields of an effect shader the importer reads.
	struct EffectShaderRecord
	{
		FormKey              key;
		std::string          editorId;     // empty when the runtime has none (needs po3's Tweaks for EFSH)
		std::string          fillTexture;  // ICON path as stored, e.g. "Effects\\DarkSwirls.dds"
		Timing::EffectParams params;
		float                tileU = 1.0f;
		float                tileV = 1.0f;
		std::uint32_t        flags = 0;  // EffectShaderData::Flags

		static constexpr std::uint32_t kGreyscaleToColor = 1 << 1;
		static constexpr std::uint32_t kGreyscaleToAlpha = 1 << 2;

		// How the recipe names this record: its editor ID when known, else its form key.
		[[nodiscard]] FormRef Reference() const;
	};

	// Reads the JSON that tools/efsh_dump.py writes.
	[[nodiscard]] std::expected<EffectShaderRecord, std::string> ParseEffectShaderRecord(std::string_view a_json);

	// The proof of concept's INI defaults, expressed as the constants the
	// imported recipe declares.
	struct ImportDefaults
	{
		Timing::ColorPolicy colorPolicy{};  // tintWithEdge, blackFillAsWhite
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
		float               shellInflatePercent = 1.0f;       // at the pulse's steady state
		float               shellInflatePulsePercent = 1.0f;  // per unit of pulse level
		MaterialChannel     glowMaskChannel = MaterialChannel::kMetallic;
		std::string         importer = "WornEnchantmentPBR 0.1.0";  // written to `imported`
	};

	// Recipe id (file stem) for a record: its editor ID, else "<plugin stem>-<id hex>".
	[[nodiscard]] std::string RecipeIdFor(const EffectShaderRecord& a_record);

	[[nodiscard]] Recipe ImportEffectShader(const EffectShaderRecord& a_record, const ImportDefaults& a_defaults = {});
}
