// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "mesh/Mesh.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kNoiseFields = 3;
inline constexpr std::array<Vec3, kNoiseFields> kNoiseFieldOffsets{
    Vec3{0.0f, 0.0f, 0.0f}, Vec3{17.3f, 5.9f, 31.1f},
    Vec3{41.7f, 23.3f, 11.9f}};
inline constexpr std::uint32_t kNoiseFieldSeedStep = 101;
inline constexpr std::uint32_t kNoiseOctaveSeedStep = 7919;

[[nodiscard]] float ValueNoise(const Vec3 &a_cell,
                               std::uint32_t a_seed) noexcept;
[[nodiscard]] float FractalNoise(const Vec3 &a_cell, std::uint32_t a_octaves,
                                 std::uint32_t a_seed) noexcept;
[[nodiscard]] Vec3 NoiseFieldsAt(const Vec3 &a_cell,
                                 const NoiseShading &a_shading) noexcept;
}
