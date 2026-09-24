// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"

#include <array>

namespace BetterEnchantmentEffects {
struct RestSkinToBone {
  std::array<std::array<float, 3>, 3> rotate{
      {{1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}}};
  Vec3 translate;
  float scale = 1.0f;
  [[nodiscard]] bool operator==(const RestSkinToBone &) const = default;
};

struct ShellPoseValues {
  Vec3 inflate;
  Vec3 offset;
  float scale = 1.0f;
  Vec3 scalePoint;
  float spin = 0.0f;
  Vec3 spinAxis{0.0f, 0.0f, 1.0f};
  [[nodiscard]] bool operator==(const ShellPoseValues &) const = default;
};

[[nodiscard]] RestSkinToBone PosedTransform(const RestSkinToBone &a_rest,
                                            const ShellPoseValues &a_pose);
}
