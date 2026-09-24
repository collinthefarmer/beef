// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/ShellPose.h"
#include "test_support.h"

#include <numbers>

using namespace BetterEnchantmentEffects;
using test::Near;

namespace {
Vec3 Apply(const RestSkinToBone &a_transform, const Vec3 &a_point) {
  const auto &r = a_transform.rotate;
  return Vec3{r[0][0] * a_point.x + r[0][1] * a_point.y + r[0][2] * a_point.z +
                  a_transform.translate.x,
              r[1][0] * a_point.x + r[1][1] * a_point.y + r[1][2] * a_point.z +
                  a_transform.translate.y,
              r[2][0] * a_point.x + r[2][1] * a_point.y + r[2][2] * a_point.z +
                  a_transform.translate.z};
}
}

int main() {
  const RestSkinToBone identity;

  {
    const ShellPoseValues noPose{};
    const RestSkinToBone posed = PosedTransform(identity, noPose);
    Near(posed.rotate[0][0], 1.0f, "identity pose keeps rotate[0][0]");
    Near(posed.rotate[1][1], 1.0f, "identity pose keeps rotate[1][1]");
    Near(posed.rotate[2][2], 1.0f, "identity pose keeps rotate[2][2]");
    Near(posed.rotate[0][1], 0.0f, "identity pose keeps rotate[0][1]");
    Near(posed.translate.x, 0.0f, "identity pose keeps translate.x");
    Near(posed.translate.y, 0.0f, "identity pose keeps translate.y");
    Near(posed.translate.z, 0.0f, "identity pose keeps translate.z");
  }

  {
    ShellPoseValues pose{};
    pose.scale = 2.0f;
    pose.scalePoint = Vec3{1.0f, 2.0f, 3.0f};
    const RestSkinToBone posed = PosedTransform(identity, pose);
    const Vec3 fixed = Apply(posed, pose.scalePoint);
    Near(fixed.x, pose.scalePoint.x, "scale about a point leaves it fixed (x)");
    Near(fixed.y, pose.scalePoint.y, "scale about a point leaves it fixed (y)");
    Near(fixed.z, pose.scalePoint.z, "scale about a point leaves it fixed (z)");
  }

  {
    ShellPoseValues pose{};
    pose.spin = 0.25f;
    pose.spinAxis = Vec3{0.0f, 0.0f, 1.0f};
    const RestSkinToBone posed = PosedTransform(identity, pose);
    const Vec3 mapped = Apply(posed, Vec3{1.0f, 0.0f, 0.0f});
    Near(mapped.x, 0.0f, "quarter turn about +Z maps +X to +Y (x)");
    Near(mapped.y, 1.0f, "quarter turn about +Z maps +X to +Y (y)");
    Near(mapped.z, 0.0f, "quarter turn about +Z maps +X to +Y (z)");
  }

  {
    ShellPoseValues pose{};
    pose.offset = Vec3{5.0f, -2.0f, 3.0f};
    const RestSkinToBone posed = PosedTransform(identity, pose);
    const Vec3 moved = Apply(posed, Vec3{1.0f, 1.0f, 1.0f});
    Near(moved.x, 1.0f + pose.offset.x, "offset adds (x)");
    Near(moved.y, 1.0f + pose.offset.y, "offset adds (y)");
    Near(moved.z, 1.0f + pose.offset.z, "offset adds (z)");
  }

  {
    ShellPoseValues pose{};
    pose.spinAxis = Vec3{0.0f, 0.0f, 0.0f};
    pose.spin = 0.25f;
    const RestSkinToBone posed = PosedTransform(identity, pose);
    const Vec3 mapped = Apply(posed, Vec3{1.0f, 0.0f, 0.0f});
    Near(mapped.x, 0.0f, "a zero spinAxis falls back to +Z (x)");
    Near(mapped.y, 1.0f, "a zero spinAxis falls back to +Z (y)");
  }

  {
    ShellPoseValues pose{};
    pose.inflate = Vec3{1.0f, 0.0f, 0.0f};
    const RestSkinToBone posed = PosedTransform(identity, pose);
    const Vec3 mapped = Apply(posed, Vec3{1.0f, 1.0f, 1.0f});
    Near(mapped.x, 2.0f,
         "inflate doubles along its own axis before scale/spin/offset");
    Near(mapped.y, 1.0f, "inflate leaves the other axes alone");
  }

  return test::Finish("shellpose");
}
