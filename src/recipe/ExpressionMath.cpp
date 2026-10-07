// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/ExpressionMath.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::uint32_t kMaxPrimeGap = 16;

bool IsPrime(std::uint32_t a_n) noexcept {
  if (a_n < 2) {
    return false;
  }
  for (std::uint32_t divisor = 2; divisor * divisor <= a_n; ++divisor) {
    if (a_n % divisor == 0) {
      return false;
    }
  }
  return true;
}

std::uint32_t PrimeAtLeast(std::uint32_t a_n) noexcept {
  std::uint32_t prime = std::max(a_n, 2u);
  for (std::uint32_t gap = 0; gap < kMaxPrimeGap && !IsPrime(prime); ++gap) {
    ++prime;
  }
  return prime;
}

float Frac(float a_x) noexcept { return a_x - std::floor(a_x); }
}

float ShufflePosition(float a_index, float a_total, float a_seed) noexcept {
  if (!(a_total >= 1.0f) || !(a_index >= 0.0f) || !std::isfinite(a_index)) {
    return kShuffleOutside;
  }
  const float total = std::min(std::floor(a_total), kMaxShuffleTotal);
  const float index = std::floor(a_index);
  if (index >= total) {
    return kShuffleOutside;
  }
  const auto count = static_cast<std::uint32_t>(total);
  const std::uint32_t prime = PrimeAtLeast(count);
  const float seed = std::isfinite(a_seed) ? Frac(a_seed) : 0.0f;
  const std::uint32_t scale =
      1 + std::min(static_cast<std::uint32_t>(std::floor(
                       Frac(seed * 29.41f) * static_cast<float>(prime - 1))),
                   prime - 2);
  const std::uint32_t shift =
      std::min(static_cast<std::uint32_t>(
                   std::floor(Frac(seed * 13.37f) * static_cast<float>(prime))),
               prime - 1);
  auto position = static_cast<std::uint32_t>(index);
  for (std::uint32_t step = 0; step < prime; ++step) {
    position = (scale * position + shift) % prime;
    if (position < count) {
      break;
    }
  }
  return static_cast<float>(position);
}
namespace {
std::uint32_t MixBits(std::uint32_t a_x) noexcept {
  a_x ^= a_x >> 16;
  a_x *= 0x7feb352dU;
  a_x ^= a_x >> 15;
  a_x *= 0x846ca68bU;
  a_x ^= a_x >> 16;
  return a_x;
}

std::uint32_t HashBits(float a_value) noexcept {
  return std::isfinite(a_value) && a_value != 0.0f
             ? std::bit_cast<std::uint32_t>(a_value)
             : 0U;
}
}

float HashUnit(float a_value, float a_salt) noexcept {
  const std::uint32_t mixed =
      MixBits(HashBits(a_value) ^ MixBits(HashBits(a_salt) + 0x9e3779b9U));
  return static_cast<float>(mixed >> 8) * (1.0f / 16777216.0f);
}

float RandomInRange(float a_start, float a_stop, float a_seed) noexcept {
  if (!std::isfinite(a_start)) {
    return 0.0f;
  }
  const float start = std::floor(a_start);
  const float stop = std::isfinite(a_stop) ? std::floor(a_stop) : start;
  if (!(stop > start)) {
    return start;
  }
  const float seed =
      std::isfinite(a_seed) ? std::clamp(a_seed, 0.0f, 1.0f) : 0.0f;
  const float span = stop - start;
  return start + std::min(std::floor(seed * span), span - 1.0f);
}
namespace {
constexpr float kGreyAxis = 0.57735026919f;
constexpr float kTurn = 2.0f * std::numbers::pi_v<float>;

float Saturated(float a_value) noexcept {
  return std::isfinite(a_value) ? std::clamp(a_value, 0.0f, 1.0f) : 0.0f;
}

float WheelChannel(float a_turns, float a_offset) noexcept {
  return Saturated(std::fabs(Frac(a_turns + a_offset) * 6.0f - 3.0f) - 1.0f);
}
}

Vec3 HueColor(float a_turns) noexcept {
  const float turns = std::isfinite(a_turns) ? a_turns : 0.0f;
  return {WheelChannel(turns, 0.0f), WheelChannel(turns, 2.0f / 3.0f),
          WheelChannel(turns, 1.0f / 3.0f)};
}

Vec3 ShiftHue(const Vec3 &a_color, float a_turns) noexcept {
  const float angle = (std::isfinite(a_turns) ? a_turns : 0.0f) * kTurn;
  const float cosine = std::cos(angle);
  const float sine = std::sin(angle);
  const float along =
      kGreyAxis * (a_color.x + a_color.y + a_color.z) * (1.0f - cosine);
  const Vec3 across{kGreyAxis * (a_color.z - a_color.y),
                    kGreyAxis * (a_color.x - a_color.z),
                    kGreyAxis * (a_color.y - a_color.x)};
  return {Saturated(a_color.x * cosine + across.x * sine + kGreyAxis * along),
          Saturated(a_color.y * cosine + across.y * sine + kGreyAxis * along),
          Saturated(a_color.z * cosine + across.z * sine + kGreyAxis * along)};
}
}
