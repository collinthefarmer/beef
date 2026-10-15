// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/Noise.h"

#include <algorithm>
#include <cmath>

namespace BetterEnchantmentEffects {
namespace {
std::uint32_t LatticeHash(std::int32_t a_x, std::int32_t a_y, std::int32_t a_z,
                          std::uint32_t a_seed) noexcept;
float LatticeValue(std::int32_t a_x, std::int32_t a_y, std::int32_t a_z,
                   std::uint32_t a_seed) noexcept;
float Fade(float a_t) noexcept;
float Lerp(float a_from, float a_to, float a_t) noexcept;
}

float ValueNoise(const Vec3 &a_cell, std::uint32_t a_seed) noexcept {
  if (!std::isfinite(a_cell.x) || !std::isfinite(a_cell.y) ||
      !std::isfinite(a_cell.z)) {
    return 0.0f;
  }
  const float fx = std::floor(a_cell.x);
  const float fy = std::floor(a_cell.y);
  const float fz = std::floor(a_cell.z);
  const auto x = static_cast<std::int32_t>(fx);
  const auto y = static_cast<std::int32_t>(fy);
  const auto z = static_cast<std::int32_t>(fz);
  const float u = Fade(a_cell.x - fx);
  const float v = Fade(a_cell.y - fy);
  const float w = Fade(a_cell.z - fz);
  const auto corner = [&](std::int32_t a_dx, std::int32_t a_dy,
                          std::int32_t a_dz) {
    return LatticeValue(x + a_dx, y + a_dy, z + a_dz, a_seed);
  };
  const float y0z0 = Lerp(corner(0, 0, 0), corner(1, 0, 0), u);
  const float y1z0 = Lerp(corner(0, 1, 0), corner(1, 1, 0), u);
  const float y0z1 = Lerp(corner(0, 0, 1), corner(1, 0, 1), u);
  const float y1z1 = Lerp(corner(0, 1, 1), corner(1, 1, 1), u);
  return Lerp(Lerp(y0z0, y1z0, v), Lerp(y0z1, y1z1, v), w);
}

float FractalNoise(const Vec3 &a_cell, std::uint32_t a_octaves,
                   std::uint32_t a_seed) noexcept {
  const std::uint32_t octaves = std::clamp<std::uint32_t>(a_octaves, 1, 4);
  float sum = 0.0f;
  float total = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  for (std::uint32_t octave = 0; octave < octaves; ++octave) {
    const Vec3 at{a_cell.x * frequency, a_cell.y * frequency,
                  a_cell.z * frequency};
    sum += amplitude * ValueNoise(at, a_seed + octave * kNoiseOctaveSeedStep);
    total += amplitude;
    amplitude *= 0.5f;
    frequency *= 2.0f;
  }
  return sum / total;
}

Vec3 NoiseFieldsAt(const Vec3 &a_cell, const NoiseShading &a_shading) noexcept {
  std::array<float, kNoiseFields> fields{};
  for (std::size_t k = 0; k < kNoiseFields; ++k) {
    const Vec3 &offset = kNoiseFieldOffsets[k];
    fields[k] = FractalNoise(
        Vec3{a_cell.x + offset.x, a_cell.y + offset.y, a_cell.z + offset.z},
        a_shading.octaves,
        a_shading.seed + static_cast<std::uint32_t>(k) * kNoiseFieldSeedStep);
  }
  return Vec3{fields[0], fields[1], fields[2]};
}

namespace {
std::uint32_t LatticeHash(std::int32_t a_x, std::int32_t a_y, std::int32_t a_z,
                          std::uint32_t a_seed) noexcept {
  std::uint32_t h = static_cast<std::uint32_t>(a_x) * 73856093u ^
                    static_cast<std::uint32_t>(a_y) * 19349663u ^
                    static_cast<std::uint32_t>(a_z) * 83492791u ^
                    a_seed * 2654435761u;
  h ^= h >> 16;
  h *= 0x7feb352du;
  h ^= h >> 15;
  h *= 0x846ca68bu;
  h ^= h >> 16;
  return h;
}

float LatticeValue(std::int32_t a_x, std::int32_t a_y, std::int32_t a_z,
                   std::uint32_t a_seed) noexcept {
  return static_cast<float>(LatticeHash(a_x, a_y, a_z, a_seed) >> 8) *
         (1.0f / 16777216.0f);
}

float Fade(float a_t) noexcept { return a_t * a_t * (3.0f - 2.0f * a_t); }

float Lerp(float a_from, float a_to, float a_t) noexcept {
  return a_from + (a_to - a_from) * a_t;
}
}
}
