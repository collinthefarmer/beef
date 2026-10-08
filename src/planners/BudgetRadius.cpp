// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/BudgetRadius.h"

#include "planners/Eviction.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>

namespace BetterEnchantmentEffects {
namespace {
std::optional<float> UsableDistance(std::optional<float> a_distance) noexcept {
  if (!a_distance || !std::isfinite(*a_distance) || *a_distance <= 0.0f) {
    return std::nullopt;
  }
  return a_distance;
}

bool CanLoseEffect(const BudgetActor &a_actor) noexcept {
  return !a_actor.resizing;
}

std::optional<float> InsideOf(const BudgetActor &a_actor,
                              float a_bound) noexcept {
  const std::optional<float> distance = UsableDistance(a_actor.distance);
  if (a_actor.role == ActorRole::kPlayer || !distance || *distance > a_bound) {
    return std::nullopt;
  }
  return distance;
}

std::optional<std::size_t>
FarthestCandidate(std::span<const BudgetActor> a_actors,
                  float a_bound) noexcept {
  std::optional<std::size_t> chosen;
  float farthest = 0.0f;
  for (std::size_t i = 0; i < a_actors.size(); ++i) {
    const std::optional<float> distance = InsideOf(a_actors[i], a_bound);
    if (distance && CanLoseEffect(a_actors[i]) &&
        (!chosen || *distance > farthest)) {
      chosen = i;
      farthest = *distance;
    }
  }
  return chosen;
}
}

bool LimitsDistance(float a_evictDistance) noexcept {
  return a_evictDistance > 0.0f;
}

float EffectiveEvictDistance(
    float a_evictDistance,
    const std::optional<BudgetRadius> &a_radius) noexcept {
  if (!a_radius) {
    return a_evictDistance;
  }
  return LimitsDistance(a_evictDistance)
             ? std::min(a_evictDistance, a_radius->distance)
             : a_radius->distance;
}

std::optional<RadiusShrink>
ShrinkRadius(std::span<const BudgetActor> a_actors,
             const std::optional<BudgetRadius> &a_current,
             std::uint32_t a_nowMs) noexcept {
  const float bound =
      a_current ? a_current->distance : std::numeric_limits<float>::infinity();
  const std::optional<std::size_t> chosen = FarthestCandidate(a_actors, bound);
  if (!chosen) {
    return std::nullopt;
  }
  const float evicted = a_actors[*chosen].distance.value_or(0.0f);
  float kept = 0.0f;
  for (std::size_t i = 0; i < a_actors.size(); ++i) {
    const std::optional<float> distance = InsideOf(a_actors[i], bound);
    if (i == *chosen || !distance) {
      continue;
    }
    if (*distance > evicted) {
      return std::nullopt;
    }
    if (*distance < evicted) {
      kept = std::max(kept, *distance);
    }
  }
  const float radius =
      std::min(std::max(evicted * kRadiusShrinkFactor, kept), bound);
  return RadiusShrink{*chosen, BudgetRadius{radius, a_nowMs},
                      a_actors[*chosen].held};
}

RadiusGrowth GrowRadius(const BudgetRadius &a_radius, Pressure a_pressure,
                        std::uint32_t a_nowMs, float a_evictDistance,
                        std::optional<float> a_farthest) noexcept {
  if (IsOver(a_pressure)) {
    return {BudgetRadius{a_radius.distance, a_nowMs}, RadiusChange::kHeld};
  }
  if (a_nowMs - a_radius.sinceMs < kRadiusDwellMs) {
    return {a_radius, RadiusChange::kHeld};
  }
  const float grown = a_radius.distance / kRestoreFraction;
  const std::optional<float> limit = LimitsDistance(a_evictDistance)
                                         ? std::optional<float>{a_evictDistance}
                                         : UsableDistance(a_farthest);
  if (!limit || !(grown < *limit)) {
    return {std::nullopt, RadiusChange::kCleared};
  }
  return {BudgetRadius{grown, a_nowMs}, RadiusChange::kGrown};
}

std::string RadiusShrinkLine(float a_radius, std::uint32_t a_actor,
                             std::string_view a_name, std::uint64_t a_freed) {
  return std::format("render target budget: effects beyond {:.0f} units are "
                     "off: {:08X} ({}) loses its effect, frees ~{}",
                     a_radius, a_actor, a_name.empty() ? "unnamed" : a_name,
                     Mebibytes(a_freed));
}

std::string RadiusGrowLine(float a_radius) {
  return std::format("render target budget: effects return up to {:.0f} units",
                     a_radius);
}

std::string RadiusClearLine() { return "render target budget: radius cleared"; }
}
