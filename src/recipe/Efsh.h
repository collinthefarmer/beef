// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"

#include <array>

namespace BetterEnchantmentEffects::Efsh {
struct AlphaParams {
  float fullAlphaRatio = 1.0f;
  float persistentAlphaRatio = 1.0f;
  float pulseAmplitude = 0.0f;
  float pulseFrequency = 0.0f;
  float fadeInTime = 0.0f;
};

struct EffectParams {
  std::array<Vec3, 3> colorKeys{};
  std::array<float, 3> colorKeyTimes{};
  std::array<float, 3> colorKeyScales{1.0f, 1.0f, 1.0f};
  float colorScale = 1.0f;
  AlphaParams fill{};
  float animationSpeedU = 0.0f;
  float animationSpeedV = 0.0f;
  Vec3 edgeColor{};
  AlphaParams edge{};
};

struct FillState {
  Vec3 color{};
  float scale = 1.0f;
  float alpha = 1.0f;
  float uOffset = 0.0f;
  float vOffset = 0.0f;
  Vec3 edgeColor{};
  float edgeAlpha = 0.0f;
};

inline constexpr float kMinAnimationSpeed = 0.05f;
inline constexpr float kMaxAnimationSpeed = 4.0f;

[[nodiscard]] float BaselineAlpha(const AlphaParams &a_params) noexcept;

[[nodiscard]] FillState Evaluate(const EffectParams &a_params,
                                 float a_elapsedSeconds, float a_speed,
                                 float a_intensity) noexcept;
}
