#include "mesh/ShellPose.h"

#include <cmath>
#include <numbers>

namespace BetterEnchantmentEffects {
namespace {
using Mat3 = std::array<std::array<float, 3>, 3>;

inline constexpr float kRadiansPerTurn = 2.0f * std::numbers::pi_v<float>;
inline constexpr float kMinAxisLength = 1.0e-6f;

Mat3 MatMul(const Mat3 &a_lhs, const Mat3 &a_rhs) noexcept {
  Mat3 out{};
  for (std::size_t row = 0; row < 3; ++row) {
    for (std::size_t col = 0; col < 3; ++col) {
      float sum = 0.0f;
      for (std::size_t k = 0; k < 3; ++k) {
        sum += a_lhs[row][k] * a_rhs[k][col];
      }
      out[row][col] = sum;
    }
  }
  return out;
}

Vec3 MatVec(const Mat3 &a_matrix, const Vec3 &a_vector) noexcept {
  return Vec3{a_matrix[0][0] * a_vector.x + a_matrix[0][1] * a_vector.y +
                  a_matrix[0][2] * a_vector.z,
              a_matrix[1][0] * a_vector.x + a_matrix[1][1] * a_vector.y +
                  a_matrix[1][2] * a_vector.z,
              a_matrix[2][0] * a_vector.x + a_matrix[2][1] * a_vector.y +
                  a_matrix[2][2] * a_vector.z};
}

Vec3 Add(const Vec3 &a_lhs, const Vec3 &a_rhs) noexcept {
  return Vec3{a_lhs.x + a_rhs.x, a_lhs.y + a_rhs.y, a_lhs.z + a_rhs.z};
}

Vec3 Sub(const Vec3 &a_lhs, const Vec3 &a_rhs) noexcept {
  return Vec3{a_lhs.x - a_rhs.x, a_lhs.y - a_rhs.y, a_lhs.z - a_rhs.z};
}

Vec3 ScaleVec(const Vec3 &a_v, float a_s) noexcept {
  return Vec3{a_v.x * a_s, a_v.y * a_s, a_v.z * a_s};
}

Vec3 NormalizedOrFallbackZ(const Vec3 &a_axis) noexcept {
  const float length = std::sqrt(a_axis.x * a_axis.x + a_axis.y * a_axis.y +
                                 a_axis.z * a_axis.z);
  if (length < kMinAxisLength) {
    return Vec3{0.0f, 0.0f, 1.0f};
  }
  return Vec3{a_axis.x / length, a_axis.y / length, a_axis.z / length};
}

Mat3 AxisAngle(const Vec3 &a_axis, float a_radians) noexcept {
  const Vec3 axis = NormalizedOrFallbackZ(a_axis);
  const float c = std::cos(a_radians);
  const float s = std::sin(a_radians);
  const float t = 1.0f - c;
  return Mat3{{{t * axis.x * axis.x + c, t * axis.x * axis.y - s * axis.z,
                t * axis.x * axis.z + s * axis.y},
               {t * axis.x * axis.y + s * axis.z, t * axis.y * axis.y + c,
                t * axis.y * axis.z - s * axis.x},
               {t * axis.x * axis.z - s * axis.y,
                t * axis.y * axis.z + s * axis.x, t * axis.z * axis.z + c}}};
}
}

RestSkinToBone PosedTransform(const RestSkinToBone &a_rest,
                              const ShellPoseValues &a_pose) {
  const Vec3 inflateAxis{1.0f + a_pose.inflate.x, 1.0f + a_pose.inflate.y,
                         1.0f + a_pose.inflate.z};
  Mat3 rotate{{{a_rest.rotate[0][0] * inflateAxis.x,
                a_rest.rotate[0][1] * inflateAxis.x,
                a_rest.rotate[0][2] * inflateAxis.x},
               {a_rest.rotate[1][0] * inflateAxis.y,
                a_rest.rotate[1][1] * inflateAxis.y,
                a_rest.rotate[1][2] * inflateAxis.y},
               {a_rest.rotate[2][0] * inflateAxis.z,
                a_rest.rotate[2][1] * inflateAxis.z,
                a_rest.rotate[2][2] * inflateAxis.z}}};
  Vec3 translate{a_rest.translate.x * inflateAxis.x,
                 a_rest.translate.y * inflateAxis.y,
                 a_rest.translate.z * inflateAxis.z};

  for (auto &row : rotate) {
    for (float &value : row) {
      value *= a_pose.scale;
    }
  }
  translate = Add(ScaleVec(translate, a_pose.scale),
                  ScaleVec(a_pose.scalePoint, 1.0f - a_pose.scale));

  const Mat3 spin = AxisAngle(a_pose.spinAxis, a_pose.spin * kRadiansPerTurn);
  rotate = MatMul(spin, rotate);
  translate =
      Add(MatVec(spin, Sub(translate, a_pose.scalePoint)), a_pose.scalePoint);

  translate = Add(translate, a_pose.offset);

  RestSkinToBone out;
  out.rotate = rotate;
  out.translate = translate;
  out.scale = a_rest.scale;
  return out;
}
}
