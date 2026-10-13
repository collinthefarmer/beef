// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/MipLevel.h"

#include <algorithm>
#include <cmath>

namespace BetterEnchantmentEffects {
namespace {
float TileScale(float a_tile) noexcept {
  return std::isfinite(a_tile) ? std::abs(a_tile) : 1.0f;
}

float LastMip(const TextureExtent &a_source) noexcept {
  return static_cast<float>(std::max<std::uint32_t>(a_source.mips, 1) - 1);
}

float ReadableFloor(float a_floorMip) noexcept {
  return std::isfinite(a_floorMip) && a_floorMip > 0.0f ? a_floorMip : 0.0f;
}
}

float FootprintMip(const TextureExtent &a_source, Tiling a_tiling,
                   std::uint32_t a_targetSide) noexcept {
  if (a_targetSide == 0)
    return 0.0f;
  const float texels =
      (std::max)(static_cast<float>(a_source.width) * TileScale(a_tiling.u),
                 static_cast<float>(a_source.height) * TileScale(a_tiling.v));
  const float level = std::log2(texels / static_cast<float>(a_targetSide));
  if (!std::isfinite(level))
    return 0.0f;
  return std::clamp(level, 0.0f, LastMip(a_source));
}

float SampleMip(const TextureExtent &a_source, Tiling a_tiling,
                std::uint32_t a_targetSide, float a_floorMip) noexcept {
  return (std::max)(ReadableFloor(a_floorMip),
                    FootprintMip(a_source, a_tiling, a_targetSide));
}

float MipThatFits(const TextureExtent &a_source,
                  std::uint32_t a_side) noexcept {
  const std::uint32_t side = (std::max)(a_source.width, a_source.height);
  std::uint32_t mip = 0;
  while (mip < 16 && (side >> (mip + 1)) >= a_side)
    ++mip;
  return static_cast<float>(mip);
}
}
