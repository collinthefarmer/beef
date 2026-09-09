#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace BetterEnchantmentEffects
{
	inline constexpr float kMinAnimationSpeed = 0.05f;
	inline constexpr float kMaxAnimationSpeed = 4.0f;

	enum class TextureScale
	{
		kQuarter,
		kHalf,
		kFull,
	};

	[[nodiscard]] std::string_view              TextureScaleName(TextureScale a_scale) noexcept;
	[[nodiscard]] std::optional<TextureScale>   TextureScaleFromString(std::string_view a_text) noexcept;
	[[nodiscard]] std::span<const std::string_view> TextureScaleNames() noexcept;

	struct Settings
	{
		bool          playerOnly = false;
		bool          enableShaders = true;
		bool          thirdPerson = true;
		bool          firstPerson = true;
		bool          uniqueMaterial = true;
		bool          verboseLogging = true;
		std::uint32_t animationFPS = 60;
		float         animationSpeed = 1.0f;
		TextureScale  textureScale = TextureScale::kFull;

		[[nodiscard]] std::uint32_t TickIntervalMS() const noexcept { return 1000u / animationFPS; }

		[[nodiscard]] static Settings Parse(std::string_view a_text);
		[[nodiscard]] std::string     Serialize() const;
	};

	struct SettingDesc
	{
		using Member = std::variant<bool Settings::*, float Settings::*, std::uint32_t Settings::*, TextureScale Settings::*>;

		enum class Widget
		{
			kCheckbox,
			kSlider,
			kIntSlider,
			kCombo,
		};

		const char*                     section;
		const char*                     key;
		const char*                     label;
		const char*                     help;
		Member                          member;
		float                           min;
		float                           max;
		Widget                          widget;
		bool                            reapply;
		std::span<const std::string_view> items = {};
	};

	[[nodiscard]] std::span<const SettingDesc> SettingTable();

	[[nodiscard]] bool SettingsDiffer(const Settings& a_lhs, const Settings& a_rhs);
}
