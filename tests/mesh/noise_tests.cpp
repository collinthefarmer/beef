// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/Noise.h"
#include "test_support.h"

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
  const float nan = std::numeric_limits<float>::quiet_NaN();
  Check(ValueNoise(Vec3{nan, 0.0f, 0.0f}, 1) == 0.0f,
        "a non-finite position reads zero");
  return test::Finish("noise");
}
