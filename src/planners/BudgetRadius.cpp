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

bool CanEvict(const BudgetActor &a_actor) noexcept { return !a_actor.resizing; }

std::optional<float> InsideOf(const BudgetActor &a_actor,
                              float a_bound) noexcept {
  const std::optional<float> distance = UsableDistance(a_actor.distance);
  if (a_actor.visibility.role == ActorRole::kPlayer || !distance ||
      *distance > a_bound) {
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
    if (distance && CanEvict(a_actors[i]) &&
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
  const BudgetActor &evictee = a_actors[*chosen];
  return RadiusShrink{evictee.actor, BudgetRadius{radius, a_nowMs},
                      evictee.costs.planned};
}

std::vector<RingActor> RingOf(std::span<const EvictedActor> a_evicted,
                              float a_evictNow, float a_evictAfter) {
  std::vector<RingActor> ring;
  for (const EvictedActor &actor : a_evicted) {
    if (!actor.distance || !std::isfinite(*actor.distance)) {
      continue;
    }
    const float distance = *actor.distance;
    if (!Returns(distance, a_evictNow) && Returns(distance, a_evictAfter)) {
      ring.push_back({distance, actor.returnBytes});
    }
  }
  std::ranges::stable_sort(ring, {}, &RingActor::distance);
  return ring;
}

std::uint64_t PriceOf(const RingActor &a_actor,
                      std::uint64_t a_unpricedBytes) noexcept {
  return a_actor.returnBytes.value_or(a_unpricedBytes);
}

std::uint64_t RingBytes(std::span<const RingActor> a_ring,
                        std::uint64_t a_unpricedBytes) noexcept {
  std::uint64_t bytes = 0;
  for (const RingActor &actor : a_ring) {
    bytes = Sum(bytes, PriceOf(actor, a_unpricedBytes));
  }
  return bytes;
}

std::optional<RingRoom> RingRoomOf(const BudgetState &a_budget) noexcept {
  const std::optional<std::uint64_t> headroom = HeadroomOf(a_budget, 0);
  if (!headroom || !a_budget.allowance) {
    return std::nullopt;
  }
  return RingRoom{*headroom, BandOf(*a_budget.allowance) / kRingMarginDivisor};
}

RingFit FitRing(std::span<const RingActor> a_ring, std::uint64_t a_room,
                std::uint64_t a_unpricedBytes) noexcept {
  RingFit fit;
  std::size_t unpriced = 0;
  while (fit.returning < a_ring.size()) {
    const float distance = a_ring[fit.returning].distance;
    std::size_t end = fit.returning;
    std::uint64_t bytes = fit.bytes;
    std::size_t groupUnpriced = unpriced;
    for (; end < a_ring.size() && a_ring[end].distance == distance; ++end) {
      bytes = Sum(bytes, PriceOf(a_ring[end], a_unpricedBytes));
      groupUnpriced += a_ring[end].returnBytes ? 0 : 1;
    }
    if (bytes > a_room || groupUnpriced > kUnpricedPerGrowth) {
      break;
    }
    fit = {end, bytes};
    unpriced = groupUnpriced;
  }
  return fit;
}

float RadiusStoppingAt(float a_distance) noexcept {
  float radius = a_distance / kRestoreFraction;
  for (int step = 0;
       step < kRadiusRoundingSteps && radius * kRestoreFraction > a_distance;
       ++step) {
    radius = std::nextafter(radius, 0.0f);
  }
  return radius;
}

RadiusGrowth GrowRadius(const RadiusGrowthRequest &a_request) {
  const BudgetRadius &radius = a_request.radius;
  if (IsOver(a_request.budget.pressure)) {
    return RadiusUnchanged{
        BudgetRadius{radius.distance, a_request.nowMs, radius.blockReported}};
  }
  if (a_request.nowMs - radius.sinceMs < kRadiusDwellMs) {
    return RadiusUnchanged{radius};
  }
  const float grown = radius.distance / kRestoreFraction;
  const std::optional<float> limit =
      LimitsDistance(a_request.evictDistance)
          ? std::optional<float>{a_request.evictDistance}
          : UsableDistance(a_request.farthest);
  const bool clears = !limit || !(grown < *limit);
  const float evictAfter =
      clears ? a_request.evictDistance
             : EffectiveEvictDistance(a_request.evictDistance,
                                      BudgetRadius{grown, a_request.nowMs});
  const std::vector<RingActor> ring = RingOf(
      a_request.evicted,
      EffectiveEvictDistance(a_request.evictDistance, radius), evictAfter);
  const std::optional<RingRoom> room = RingRoomOf(a_request.budget);
  const RingFit fit =
      room ? FitRing(ring, Less(room->headroom, room->margin),
                     a_request.unpricedBytes)
           : RingFit{ring.size(), RingBytes(ring, a_request.unpricedBytes)};
  if (fit.returning == ring.size()) {
    if (clears) {
      return RadiusCleared{fit.bytes};
    }
    return RadiusGrown{BudgetRadius{grown, a_request.nowMs}, fit.bytes};
  }
  const float stop = RadiusStoppingAt(ring[fit.returning].distance);
  if (fit.returning == 0 || !(stop > radius.distance)) {
    return RadiusBlocked{BudgetRadius{radius.distance, a_request.nowMs, true},
                         RingBytes(ring, a_request.unpricedBytes),
                         room.value_or(RingRoom{}), !radius.blockReported};
  }
  return RadiusGrown{BudgetRadius{stop, a_request.nowMs}, fit.bytes};
}

std::string RadiusShrinkLine(float a_radius, std::uint32_t a_actor,
                             std::string_view a_name, std::uint64_t a_freed) {
  return std::format("render target budget: effects beyond {:.0f} units are "
                     "off: {:08X} ({}) is evicted, frees ~{}",
                     a_radius, a_actor, a_name.empty() ? "unnamed" : a_name,
                     Mebibytes(a_freed));
}

std::string RadiusGrowLine(float a_radius) {
  return std::format("render target budget: effects return up to {:.0f} units",
                     a_radius);
}

std::string RadiusBlockedLine(const RadiusBlocked &a_blocked) {
  return std::format(
      "render target budget: effects stay off beyond {:.0f} units: the "
      "returning NPCs plan ~{}, the headroom is ~{} less a margin of ~{}",
      a_blocked.radius.distance, Mebibytes(a_blocked.ringBytes),
      Mebibytes(a_blocked.room.headroom), Mebibytes(a_blocked.room.margin));
}

std::string RadiusClearLine() { return "render target budget: radius cleared"; }
}
