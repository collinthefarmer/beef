// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/Noise.h"
#include "test_support.h"

#include <algorithm>
#include <cmath>
#include <limits>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
bool InUnit(float a_value) { return a_value >= 0.0f && a_value < 1.0f; }
}

int main() {
  bool ranged = true;
  bool smooth = true;
  for (int i = 0; i < 200; ++i) {
    const Vec3 at{static_cast<float>(i) * 0.37f - 30.0f,
                  static_cast<float>(i) * 0.11f,
                  -static_cast<float>(i) * 0.23f};
    const float value = FractalNoise(at, 3, 5);
    ranged = ranged && InUnit(value);
    const float step =
        FractalNoise(Vec3{at.x + 0.001f, at.y, at.z}, 3, 5) - value;
    smooth = smooth && std::abs(step) < 0.02f;
  }
  Check(ranged, "fractal noise stays in 0..1");
  Check(smooth, "noise changes little over a small step, so it is smooth");

  const Vec3 lattice{3.0f, -2.0f, 7.0f};
  Check(ValueNoise(lattice, 1) == ValueNoise(lattice, 1) &&
            ValueNoise(lattice, 1) != ValueNoise(lattice, 2),
        "a seed fixes the noise and another seed changes it");
  Check(std::abs(ValueNoise(Vec3{3.0f, -2.0f, 6.999f}, 1) -
                 ValueNoise(lattice, 1)) < 0.01f,
        "noise is continuous across a cell boundary");

  const NoiseShading shading{2, 9};
  const Vec3 fields = NoiseFieldsAt(Vec3{1.3f, 4.7f, -2.2f}, shading);
  Check(InUnit(fields.x) && InUnit(fields.y) && InUnit(fields.z) &&
            fields.x != fields.y && fields.y != fields.z,
        "the three fields are independent noise");
  Check(FractalNoise(Vec3{0.5f, 0.5f, 0.5f}, 0, 3) ==
                FractalNoise(Vec3{0.5f, 0.5f, 0.5f}, 1, 3) &&
            FractalNoise(Vec3{0.5f, 0.5f, 0.5f}, 9, 3) ==
                FractalNoise(Vec3{0.5f, 0.5f, 0.5f}, 4, 3),
        "octaves clamp to 1..4");
  bool styled = true;
  bool varied = true;
  for (int i = 0; i < 100; ++i) {
    const Vec3 at{static_cast<float>(i) * 0.53f, static_cast<float>(i) * 0.29f,
                  -static_cast<float>(i) * 0.17f};
    const float blobs =
        StyledNoise(at, NoiseShading{2, 4, NoiseStyle::kBlobs}, 4);
    const float ridges =
        StyledNoise(at, NoiseShading{2, 4, NoiseStyle::kRidges}, 4);
    const float cracks =
        StyledNoise(at, NoiseShading{2, 4, NoiseStyle::kCracks}, 4);
    styled = styled && InUnit(blobs) && ridges >= 0.0f && ridges <= 1.0f &&
             cracks >= 0.0f && cracks <= 1.0f;
    varied = varied && std::abs(ridges - std::abs(2.0f * blobs - 1.0f)) < 1e-5f;
  }
  Check(styled, "every style stays in 0..1");
  Check(varied, "ridges fold blobs at their middle, so veins sit where blobs "
                "cross 0.5");
  Check(CellularEdge(Vec3{0.5f, 0.5f, 0.5f}, 3) >= 0.0f &&
            CellularEdge(Vec3{0.5f, 0.5f, 0.5f}, 3) ==
                CellularEdge(Vec3{0.5f, 0.5f, 0.5f}, 3),
        "cellular noise is deterministic");
  float smallest = 1.0f;
  for (int i = 0; i < 400; ++i) {
    smallest = std::min(
        smallest,
        CellularEdge(Vec3{static_cast<float>(i) * 0.0125f, 0.37f, 0.81f}, 3));
  }
  Check(smallest < 0.05f,
        "cellular noise reaches near zero on a border between cells");

  const float nan = std::numeric_limits<float>::quiet_NaN();
  Check(ValueNoise(Vec3{nan, 0.0f, 0.0f}, 1) == 0.0f,
        "a non-finite position reads zero");
  return test::Finish("noise");
}
