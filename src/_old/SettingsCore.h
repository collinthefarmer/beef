#pragma once

#include "Timing.h"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace BetterEnchantmentEffects
{
	struct Settings
	{
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

		bool                                         tintWithEdge = true;
		bool                                         blackFillAsWhite = true;
		std::unordered_map<std::string, Timing::Rgb> colorOverrides;

		bool          glowMask = true;
		std::uint32_t glowMaskChannel = 1;
		float         glowMaskThreshold = 0.0f;
		float         glowMaskSoftness = 0.1f;
		bool          glowMaskInvert = false;
		float         glowMaskStrength = 1.0f;

		bool          runtimeTextures = true;
		std::uint32_t runtimeTextureSize = 512;

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
		std::uint32_t shimmerDepthSource = 0;
		float         shimmerArmorWeight = 1.0f;
		float         shimmerReliefContrast = 3.0f;
		float         shimmerNoiseWeight = 0.35f;

		bool          light = true;
		float         lightIntensity = 1.0f;
		std::uint32_t lightRadius = 300;
		std::uint32_t lightBone = 0;
		std::uint32_t lightMaxPerEffect = 2;
		bool          lightUseBound = true;
		bool          shell = true;
		std::uint32_t shellMode = 0;
		bool          shellDepthBias = true;
		std::uint32_t shellBlend = 0;
		std::uint32_t shellDiffuse = 0;
		float         shellAlpha = 0.5f;
		float         shellRimPower = 4.0f;
		float         shellEmissive = 0.5f;
		float         shellScale = 1.0f;
		float         shellScalePulse = 1.0f;
		float         shellScaleAlong = 0.0f;
		float         shellScaleAcrossY = 1.0f;
		float         shellScaleAcrossZ = 1.0f;

		[[nodiscard]] std::uint32_t       TickIntervalMS() const noexcept { return 1000u / animationFPS; }
		[[nodiscard]] Timing::ColorPolicy ColorPolicyFor(const std::string& a_formKey) const;

		[[nodiscard]] static Settings Parse(std::string_view a_text);
		[[nodiscard]] std::string Serialize() const;
	};

	struct SettingDesc
	{
		using Member = std::variant<bool Settings::*, float Settings::*, std::uint32_t Settings::*>;

		enum class Widget
		{
			kCheckbox,
			kSlider,
			kLogSlider,
			kIntSlider,
			kSizeCombo,
			kEnum,
		};

		const char* section;
		const char* key;
		const char* page;
		const char* label;
		const char* help;
		Member      member;
		float       min;
		float       max;
		Widget      widget;
		bool        reapply;
		const char* items = nullptr;
	};

	[[nodiscard]] std::span<const SettingDesc> SettingTable();

	[[nodiscard]] bool SettingsDiffer(const Settings& a_lhs, const Settings& a_rhs);
}
