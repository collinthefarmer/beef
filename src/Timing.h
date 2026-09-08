#pragma once

#include <array>
#include <cstdint>
#include <optional>

namespace WornEnchantmentPBR::Timing
{
	struct Rgb
	{
		float r = 0.0f;
		float g = 0.0f;
		float b = 0.0f;
		[[nodiscard]] bool operator==(const Rgb&) const = default;
	};

	struct AlphaParams
	{
		float fullAlphaRatio = 1.0f;
		float persistentAlphaRatio = 1.0f;
		float pulseAmplitude = 0.0f;
		float pulseFrequency = 0.0f;
		float fadeInTime = 0.0f;
	};

	struct EffectParams
	{
		std::array<Rgb, 3>   colorKeys{};
		std::array<float, 3> colorKeyTimes{};
		std::array<float, 3> colorKeyScales{ 1.0f, 1.0f, 1.0f };
		float                colorScale = 1.0f;
		AlphaParams          fill{};
		float                animationSpeedU = 0.0f;
		float                animationSpeedV = 0.0f;
		Rgb                  edgeColor{};
		AlphaParams          edge{};
		float                edgeFalloff = 1.0f;
	};

	struct FillState
	{
		Rgb   color{};
		float scale = 1.0f;
		float alpha = 1.0f;
		float uOffset = 0.0f;
		float vOffset = 0.0f;
		Rgb   edgeColor{};
		float edgeAlpha = 0.0f;
	};

	struct ColorPolicy
	{
		bool               tintWithEdge = true;
		bool               blackFillAsWhite = true;
		std::optional<Rgb> override;
	};

	[[nodiscard]] Rgb NormalizeHue(const Rgb& a_color) noexcept;

	[[nodiscard]] float Chroma(const Rgb& a_color) noexcept;

	[[nodiscard]] Rgb ResolveEmissiveColor(const Rgb& a_fill, const Rgb& a_edge, const ColorPolicy& a_policy) noexcept;

	inline constexpr float kMinAnimationSpeed = 0.05f;
	inline constexpr float kMaxAnimationSpeed = 4.0f;
	inline constexpr float kMinIntensity = 0.1f;
	inline constexpr float kMaxIntensity = 2.0f;

	[[nodiscard]] float ClampAnimationSpeed(float a_speed) noexcept;
	[[nodiscard]] float ClampIntensity(float a_intensity) noexcept;

	[[nodiscard]] float SegmentAmount(float a_t, float a_start, float a_end) noexcept;

	[[nodiscard]] Rgb LerpColor(const Rgb& a_from, const Rgb& a_to, float a_amount) noexcept;

	[[nodiscard]] float BaselineAlpha(const AlphaParams& a_params) noexcept;

	[[nodiscard]] float EvaluateAlpha(const AlphaParams& a_params, float a_t, float a_intensity) noexcept;

	[[nodiscard]] FillState Evaluate(const EffectParams& a_params, float a_elapsedSeconds, float a_speed, float a_intensity) noexcept;

	[[nodiscard]] std::uint32_t FrameIndex(float a_uOffset, float a_vOffset, bool a_scrollsU, bool a_scrollsV, std::uint32_t a_frameCount) noexcept;
}
