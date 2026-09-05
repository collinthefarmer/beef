#pragma once

// Engine-free settings model: the value struct, the declarative table that
// describes every scalar setting once, and pure parse/serialise. Settings.h
// adds the disk, logging and form-key wrappers on top.

#include "Timing.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace WornEnchantmentPBR
{
	struct Settings
	{
		// [General]
		bool          playerOnly = false;
		bool          enableShaders = true;
		bool          thirdPerson = true;
		bool          firstPerson = true;
		bool          uniqueMaterial = true;
		bool          verboseLogging = true;
		bool          debugSolidGlow = false;
		std::uint32_t maxEffectsPerActor = 32;
		std::uint32_t animationFPS = 60;
		float         animationSpeed = 1.0f;
		float         intensity = 1.0f;
		float         emissiveScale = 20.0f;
		bool          normalizeBrightness = true;
		float         emissiveStrength = 1.0f;

		// [Colors]
		bool                                         tintWithEdge = true;
		bool                                         blackFillAsWhite = true;
		std::unordered_map<std::string, Timing::Rgb> colorOverrides;  // key: FormKeyOf() of the EFSH

		// [Layers] glow mask
		bool          glowMask = true;
		std::uint32_t glowMaskChannel = 1;  // RMAOS 0 r roughness, 1 g metallic, 2 b occlusion, 3 a reflectance
		float         glowMaskThreshold = 0.0f;
		float         glowMaskSoftness = 0.1f;
		bool          glowMaskInvert = false;
		float         glowMaskStrength = 1.0f;

		// [Runtime]
		bool          runtimeTextures = true;
		std::uint32_t runtimeTextureSize = 512;

		// [Layers]
		bool          sheen = true;
		float         sheenScale = 0.5f;
		bool          sheenMap = true;
		bool          sheenMapMirror = true;
		float         sheenMapPhase = 0.5f;
		bool          glint = false;
		float         glintScreenSpaceScale = 1.5f;
		float         glintLogMicrofacetDensity = 20.0f;
		float         glintMicrofacetRoughness = 0.015f;
		float         glintDensityRandomization = 2.0f;
		float         glossBoost = 0.25f;
		bool          glossMap = true;
		bool          glossMapMirror = false;
		bool          glossMapTranspose = false;
		float         glossMapPhase = 0.0f;
		float         glossContrast = 2.0f;
		std::uint32_t glossMapSize = 1024;
		bool          shimmer = true;
		float         shimmerScale = 0.1f;
		bool          shimmerTranspose = true;
		float         shimmerPhase = 0.25f;
		std::uint32_t shimmerDepthSource = 0;  // 0 auto, 1 displacement, 2 occlusion, 3 normal slope, 4 diffuse luminance
		float         shimmerArmorWeight = 1.0f;
		float         shimmerReliefContrast = 3.0f;
		float         shimmerNoiseWeight = 0.35f;

		// [Outputs]
		bool          light = true;
		float         lightIntensity = 1.0f;
		std::uint32_t lightRadius = 300;
		std::uint32_t lightBone = 0;         // 0 = the bones the item is skinned to; 1.. = fixed list in Outputs.cpp
		std::uint32_t lightMaxPerEffect = 2;
		bool          lightUseBound = true;  // place each light at its bone's skinned-vertex centre
		bool          shell = true;
		std::uint32_t shellMode = 0;       // 0 rim shell over the driven armor, 1 the layers write the shell's PBR copy
		bool          shellDepthBias = true;
		std::uint32_t shellBlend = 0;    // 0 additive, 1 alpha blend
		std::uint32_t shellDiffuse = 0;  // 0 white, 1 armor diffuse
		float         shellAlpha = 0.5f;
		float         shellRimPower = 4.0f;
		float         shellEmissive = 0.5f;
		float         shellScale = 1.0f;       // inflation, percent, at the pulse's steady state
		float         shellScalePulse = 1.0f;  // extra percent per unit of pulse level
		float         shellScaleAlong = 0.0f;  // weight of the inflation along each bone (its X axis)
		float         shellScaleAcrossY = 1.0f;  // weight across the bone, Y
		float         shellScaleAcrossZ = 1.0f;  // weight across the bone, Z

		[[nodiscard]] std::uint32_t       TickIntervalMS() const noexcept { return 1000u / animationFPS; }
		[[nodiscard]] Timing::ColorPolicy ColorPolicyFor(const std::string& a_formKey) const;

		// INI text in, settings out (unknown keys ignored, values clamped).
		[[nodiscard]] static Settings Parse(std::string_view a_text);
		// Settings out as INI text with the table's help comments.
		[[nodiscard]] std::string Serialize() const;
	};

	// One row per scalar setting. Everything that touches a setting by name
	// (parser, serialiser, menu, change detection) derives from this table.
	struct SettingDesc
	{
		using Member = std::variant<bool Settings::*, float Settings::*, std::uint32_t Settings::*>;

		enum class Widget
		{
			kCheckbox,
			kSlider,
			kLogSlider,
			kIntSlider,
			kSizeCombo,  // powers of two between min and max
			kEnum,       // combo over `items` (zero-separated labels); the value is the index
		};

		const char* section;  // INI section
		const char* key;      // INI key
		const char* page;     // menu page or group
		const char* label;    // menu label
		const char* help;     // tooltip and INI comment (may be empty)
		Member      member;
		float       min;
		float       max;
		Widget      widget;
		bool        reapply;  // consumed at apply time; a change needs a re-apply
		const char* items = nullptr;  // kEnum labels, "a\0b\0c\0"
	};

	[[nodiscard]] std::span<const SettingDesc> SettingTable();

	// True when any table setting or the override map differs.
	[[nodiscard]] bool SettingsDiffer(const Settings& a_lhs, const Settings& a_rhs);
}
