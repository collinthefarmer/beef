// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>

namespace BetterEnchantmentEffects {
struct TextureExtent {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint32_t mips = 1;
};

struct Tiling {
  float u = 1.0f;
  float v = 1.0f;
};

[[nodiscard]] float FootprintMip(const TextureExtent &a_source, Tiling a_tiling,
                                 std::uint32_t a_targetSide) noexcept;
[[nodiscard]] float SampleMip(const TextureExtent &a_source, Tiling a_tiling,
                              std::uint32_t a_targetSide,
                              float a_floorMip) noexcept;
[[nodiscard]] float MipThatFits(const TextureExtent &a_source,
                                std::uint32_t a_side) noexcept;

inline constexpr std::uint32_t kNormalDetailSide = 256;
inline constexpr std::uint32_t kNormalDetailCoarseSteps = 2;
inline constexpr std::uint32_t kNormalDetailFootprintSide = 64;

struct NormalDetailScale {
  float fineMip = 0.0f;
  float coarseMip = 0.0f;
  float footprint = 0.0f;
};
[[nodiscard]] NormalDetailScale
NormalDetailScaleOf(const TextureExtent &a_normal) noexcept;
}
