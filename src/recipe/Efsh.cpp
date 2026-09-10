#include "recipe/Efsh.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace BetterEnchantmentEffects::Efsh {
namespace {
constexpr float kEpsilon = 1e-4f;

float Lerp(float a_from, float a_to, float a_amount) noexcept {
  return a_from + (a_to - a_from) * a_amount;
}

float Wrap(float a_value, float a_period) noexcept {
  float r = std::fmod(a_value, a_period);
  if (r < 0.0f) {
    r += a_period;
  }
  return r;
}

float SegmentAmount(float a_t, float a_start, float a_end) noexcept {
  const float span = a_end - a_start;
  if (span <= kEpsilon) {
    return 1.0f;
  }
  return Clamp01((a_t - a_start) / span);
}

Vec3 LerpColor(const Vec3 &a_from, const Vec3 &a_to, float a_amount) noexcept {
  const float amount = Clamp01(a_amount);
  return Vec3{Lerp(a_from.x, a_to.x, amount), Lerp(a_from.y, a_to.y, amount),
              Lerp(a_from.z, a_to.z, amount)};
}

float EvaluateAlpha(const AlphaParams &a_params, float a_t,
                    float a_intensity) noexcept {
  const float full = a_params.fullAlphaRatio;
  float persistent = a_params.persistentAlphaRatio;
  if (persistent <= kEpsilon) {
    persistent = full;
  }

  float alpha = persistent;

  const float amplitude = Clamp01(a_params.pulseAmplitude);
  if (amplitude > kEpsilon && a_params.pulseFrequency > kEpsilon) {
    const float s = std::sin(a_params.pulseFrequency * 2.0f *
                                 std::numbers::pi_v<float> * a_t -
                             std::numbers::pi_v<float> / 2.0f);
    const float peak = std::max(full, persistent);
    alpha = Lerp(persistent, peak, (s * 0.5f + 0.5f) * amplitude);
  }

  if (a_params.fadeInTime > kEpsilon && a_t < a_params.fadeInTime) {
    alpha *= Clamp01(a_t / a_params.fadeInTime);
  }

  return Clamp01(alpha * a_intensity);
}
}

float BaselineAlpha(const AlphaParams &a_params) noexcept {
  const float persistent = a_params.persistentAlphaRatio > kEpsilon
                               ? a_params.persistentAlphaRatio
                               : a_params.fullAlphaRatio;
  return std::max(persistent, kEpsilon);
}

FillState Evaluate(const EffectParams &a_params, float a_elapsedSeconds,
                   float a_speed, float a_intensity) noexcept {
  FillState out{};
  const float t = a_elapsedSeconds * a_speed;

  const float k1 = std::max(0.0f, a_params.colorKeyTimes[0]);
  const float k2 = std::max(k1, a_params.colorKeyTimes[1]);
  const float k3 = std::max(k2, a_params.colorKeyTimes[2]);

  out.color = a_params.colorKeys[0];
  float scale = a_params.colorKeyScales[0];
  if (k3 > kEpsilon) {
    const float phase = Wrap(t, k3);
    if (phase > k1) {
      if (phase > k2) {
        const float amount = SegmentAmount(phase, k2, k3);
        out.color =
            LerpColor(a_params.colorKeys[1], a_params.colorKeys[2], amount);
        scale = Lerp(a_params.colorKeyScales[1], a_params.colorKeyScales[2],
                     amount);
      } else {
        const float amount = SegmentAmount(phase, k1, k2);
        out.color =
            LerpColor(a_params.colorKeys[0], a_params.colorKeys[1], amount);
        scale = Lerp(a_params.colorKeyScales[0], a_params.colorKeyScales[1],
                     amount);
      }
    }
  }

  out.scale = std::max(0.0f, std::max(0.0f, a_params.colorScale) * scale);
  out.alpha = EvaluateAlpha(a_params.fill, t, a_intensity);
  out.uOffset = Wrap(t * a_params.animationSpeedU, 1.0f);
  out.vOffset = Wrap(t * a_params.animationSpeedV, 1.0f);
  out.edgeColor = a_params.edgeColor;
  out.edgeAlpha = EvaluateAlpha(a_params.edge, t, a_intensity);
  return out;
}
}
