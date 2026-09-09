#pragma once

#include <algorithm>
#include <cstdint>

namespace BetterEnchantmentEffects
{
	class TextureSize
	{
	public:
		static constexpr std::uint32_t kMin = 64;
		static constexpr std::uint32_t kMax = 4096;

		[[nodiscard]] static TextureSize Clamp(std::uint32_t a_pixels) noexcept { return TextureSize{ std::clamp(a_pixels, kMin, kMax) }; }
		[[nodiscard]] std::uint32_t      Pixels() const noexcept { return pixels_; }
		[[nodiscard]] bool               operator==(const TextureSize&) const noexcept = default;

	private:
		explicit TextureSize(std::uint32_t a_pixels) noexcept :
			pixels_(a_pixels)
		{}
		std::uint32_t pixels_;
	};
}
