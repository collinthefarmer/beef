// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/MipLevel.h"
#include "test_support.h"

#include <cmath>
#include <limits>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
constexpr TextureExtent kChained2048{2048, 2048, 12};
constexpr TextureExtent kSingle2048{2048, 2048, 1};

bool Near(float a_value, float a_expected) {
  return std::abs(a_value - a_expected) < 1e-4f;
}

void ASmallerTargetReadsTheMatchingMip() {
  Check(Near(FootprintMip(kChained2048, {}, 2048), 0.0f),
        "a full-size target reads mip 0");
  Check(Near(FootprintMip(kChained2048, {}, 1024), 1.0f),
        "a half-size target reads mip 1");
  Check(Near(FootprintMip(kChained2048, {}, 256), 3.0f),
        "an eighth-size target reads mip 3");
  Check(Near(FootprintMip(kChained2048, {}, 4096), 0.0f),
        "a larger target never reads below mip 0");
}

void TilingRaisesTheFootprint() {
  Check(Near(FootprintMip(kChained2048, {4.0f, 4.0f}, 2048), 2.0f),
        "tiling four times reads two mips down");
  Check(Near(FootprintMip(kChained2048, {1.0f, 8.0f}, 2048), 3.0f),
        "the more tiled axis sets the footprint");
  Check(Near(FootprintMip(TextureExtent{2048, 512, 12}, {1.0f, 4.0f}, 1024),
             1.0f),
        "a rectangular source uses its larger tiled side");
}

void TheMipCountLimitsTheLevel() {
  Check(Near(FootprintMip(kSingle2048, {}, 256), 0.0f),
        "a texture without a mip chain reads mip 0");
  Check(Near(FootprintMip(TextureExtent{2048, 2048, 3}, {}, 64), 2.0f),
        "the level stops at the last mip");
  Check(Near(FootprintMip(TextureExtent{2048, 2048, 0}, {}, 64), 0.0f),
        "a zero mip count reads mip 0");
}

void TheRecipeMipIsAFloor() {
  Check(Near(SampleMip(kChained2048, {}, 2048, 2.0f), 2.0f),
        "the recipe mip blurs a full-size read");
  Check(Near(SampleMip(kChained2048, {}, 256, 1.0f), 3.0f),
        "the footprint wins over a lower recipe mip");
  Check(Near(SampleMip(kSingle2048, {}, 256, 1.5f), 1.5f),
        "the recipe mip passes through when the source has no chain");
}

void GarbageReadsMipZero() {
  const float nan = std::numeric_limits<float>::quiet_NaN();
  const float inf = std::numeric_limits<float>::infinity();
  Check(Near(FootprintMip(kChained2048, {}, 0), 0.0f),
        "a zero target side reads mip 0");
  Check(Near(FootprintMip(TextureExtent{0, 0, 12}, {}, 256), 0.0f),
        "an empty source reads mip 0");
  Check(Near(FootprintMip(kChained2048, {nan, inf}, 2048), 0.0f),
        "non-finite tiling counts as no tiling");
  Check(Near(FootprintMip(kChained2048, {-4.0f, 1.0f}, 2048), 2.0f),
        "a negative tile counts by its size");
  Check(Near(SampleMip(kChained2048, {}, 2048, nan), 0.0f),
        "a non-finite recipe mip reads mip 0");
  Check(Near(SampleMip(kChained2048, {}, 2048, -3.0f), 0.0f),
        "a negative recipe mip reads mip 0");
}

void MipThatFitsKeepsTheSideAtLeastTheTarget() {
  Check(Near(MipThatFits(kChained2048, 64), 5.0f),
        "a 2048 source fits 64 at mip 5");
  Check(Near(MipThatFits(TextureExtent{1000, 600, 10}, 64), 3.0f),
        "a 1000 source fits 64 at mip 3, the last level of at least 64");
  Check(Near(MipThatFits(TextureExtent{32, 32, 6}, 64), 0.0f),
        "a source smaller than the side reads mip 0");
}
}

int main() {
  ASmallerTargetReadsTheMatchingMip();
  TilingRaisesTheFootprint();
  TheMipCountLimitsTheLevel();
  TheRecipeMipIsAFloor();
  GarbageReadsMipZero();
  MipThatFitsKeepsTheSideAtLeastTheTarget();
  return test::Finish("planners_miplevel");
}
