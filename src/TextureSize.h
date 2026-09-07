#pragma once

// The one size type for render targets and the bakes and masks cached by
// size. Engine-free, so the key functions in Mesh.h and the lab in
// RuntimeTextures.h name the same type.

#include <algorithm>
#include <cstdint>

namespace WornEnchantmentPBR
{
	// The side of a square render target in pixels, 64 to 4096. Clamp is
	// the only way to make one, so no pass can be asked for a 0 px target
	// and no caller re-clamps a size it was handed.
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
