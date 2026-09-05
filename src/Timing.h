#pragma once

// Pure port of WornEnchantmentFX's WornShaderData::Update, EvaluateAlpha,
// LerpColor and SegmentAmount (decompiled/WornEnchantmentFX/plugin.c
// 4199-4573). Nothing here touches the engine so it can be unit-tested on any
// host.

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

	// Everything the animation needs from an EFSH record, converted to floats
	// once at apply time.
	struct EffectParams
	{
		std::array<Rgb, 3>   colorKeys{};
		std::array<float, 3> colorKeyTimes{};
		std::array<float, 3> colorKeyScales{ 1.0f, 1.0f, 1.0f };
		float                colorScale = 1.0f;
		AlphaParams          fill{};
		float                animationSpeedU = 0.0f;
		float                animationSpeedV = 0.0f;
		// Edge (rim) effect: the colour lives here on the vanilla armor shaders.
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
		Rgb   edgeColor{};  // plugin.c 4544-4552
		float edgeAlpha = 0.0f;  // plugin.c 4553-4560
	};

	// How the emissive colour is derived from an EFSH.
	struct ColorPolicy
	{
		bool               tintWithEdge = true;      // hue from the edge colour, brightness from the fill keys
		bool               blackFillAsWhite = true;  // vanilla stamina/frost shaders have black fill keys
		std::optional<Rgb> override;                 // from the INI [Colors] section
	};

	// Colour scaled so its largest component is 1 (black stays black).
	[[nodiscard]] Rgb NormalizeHue(const Rgb& a_color) noexcept;

	// Chroma of a colour: 0 for greys, 1 for a pure hue.
	[[nodiscard]] float Chroma(const Rgb& a_color) noexcept;

	// Emissive colour for one tick: a_fill is the animated fill colour (keys
	// already interpolated and scaled), a_edge the EFSH edge colour.
	[[nodiscard]] Rgb ResolveEmissiveColor(const Rgb& a_fill, const Rgb& a_edge, const ColorPolicy& a_policy) noexcept;

	inline constexpr float kMinAnimationSpeed = 0.05f;
	inline constexpr float kMaxAnimationSpeed = 4.0f;
	inline constexpr float kMinIntensity = 0.1f;
	inline constexpr float kMaxIntensity = 2.0f;

	[[nodiscard]] float ClampAnimationSpeed(float a_speed) noexcept;
	[[nodiscard]] float ClampIntensity(float a_intensity) noexcept;

	// Position of a_t inside [a_start, a_end] as 0..1; 1 when the segment is
	// degenerate (plugin.c 4385-4408).
	[[nodiscard]] float SegmentAmount(float a_t, float a_start, float a_end) noexcept;

	[[nodiscard]] Rgb LerpColor(const Rgb& a_from, const Rgb& a_to, float a_amount) noexcept;

	// The steady-state alpha a shader settles at (its persistent ratio, or the
	// full ratio when persistent is unset); dividing EvaluateAlpha by it leaves
	// only the pulse and fade-in shape.
	[[nodiscard]] float BaselineAlpha(const AlphaParams& a_params) noexcept;

	// plugin.c 4199-4264. a_t is already scaled by the animation speed.
	[[nodiscard]] float EvaluateAlpha(const AlphaParams& a_params, float a_t, float a_intensity) noexcept;

	// plugin.c 4414-4573. a_elapsedSeconds is wall time since the effect was
	// applied; a_speed and a_intensity are the clamped INI values.
	[[nodiscard]] FillState Evaluate(const EffectParams& a_params, float a_elapsedSeconds, float a_speed, float a_intensity) noexcept;

	// Flipbook frame for a scroll offset in 0..1. Both axes scrolling use U;
	// only-V scrolling uses V.
	[[nodiscard]] std::uint32_t FrameIndex(float a_uOffset, float a_vOffset, bool a_scrollsU, bool a_scrollsV, std::uint32_t a_frameCount) noexcept;
}
