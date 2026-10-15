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

float CellularEdge(const Vec3 &a_cell, std::uint32_t a_seed) noexcept {
  if (!std::isfinite(a_cell.x) || !std::isfinite(a_cell.y) ||
      !std::isfinite(a_cell.z)) {
    return 0.0f;
  }
  const auto x0 = static_cast<std::int32_t>(std::floor(a_cell.x));
  const auto y0 = static_cast<std::int32_t>(std::floor(a_cell.y));
  const auto z0 = static_cast<std::int32_t>(std::floor(a_cell.z));
  float nearest = 8.0f;
  float second = 8.0f;
  for (std::int32_t dz = -1; dz <= 1; ++dz) {
    for (std::int32_t dy = -1; dy <= 1; ++dy) {
      for (std::int32_t dx = -1; dx <= 1; ++dx) {
        const std::int32_t x = x0 + dx;
        const std::int32_t y = y0 + dy;
        const std::int32_t z = z0 + dz;
        const float px = static_cast<float>(x) +
                         LatticeValue(x, y, z, a_seed + kCellularSeedStep);
        const float py = static_cast<float>(y) +
                         LatticeValue(x, y, z, a_seed + 2 * kCellularSeedStep);
        const float pz = static_cast<float>(z) +
                         LatticeValue(x, y, z, a_seed + 3 * kCellularSeedStep);
        const float d = std::sqrt((px - a_cell.x) * (px - a_cell.x) +
                                  (py - a_cell.y) * (py - a_cell.y) +
                                  (pz - a_cell.z) * (pz - a_cell.z));
        if (d < nearest) {
          second = nearest;
          nearest = d;
        } else if (d < second) {
          second = d;
        }
      }
    }
  }
  return std::clamp(second - nearest, 0.0f, 1.0f);
}

float StyledNoise(const Vec3 &a_cell, const NoiseShading &a_shading,
                  std::uint32_t a_seed) noexcept {
  switch (a_shading.style) {
  case NoiseStyle::kRidges:
    return std::abs(2.0f * FractalNoise(a_cell, a_shading.octaves, a_seed) -
                    1.0f);
  case NoiseStyle::kCracks:
    return CellularNoise(a_cell, a_shading.octaves, a_seed);
  case NoiseStyle::kBlobs:
    break;
  }
  return FractalNoise(a_cell, a_shading.octaves, a_seed);
}

float CellularNoise(const Vec3 &a_cell, std::uint32_t a_octaves,
                    std::uint32_t a_seed) noexcept {
  const std::uint32_t octaves = std::clamp<std::uint32_t>(a_octaves, 1, 4);
  float sum = 0.0f;
  float total = 0.0f;
  float amplitude = 1.0f;
  float frequency = 1.0f;
  for (std::uint32_t octave = 0; octave < octaves; ++octave) {
    sum += amplitude *
           CellularEdge(Vec3{a_cell.x * frequency, a_cell.y * frequency,
                             a_cell.z * frequency},
                        a_seed + octave * kNoiseOctaveSeedStep);
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
    fields[k] = StyledNoise(
        Vec3{a_cell.x + offset.x, a_cell.y + offset.y, a_cell.z + offset.z},
        a_shading,
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
