// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/sim/Driver.h"

#include <cstdint>
#include <optional>
#include <utility>

namespace sim {
inline constexpr std::uint64_t kFullBytes = 64 * kMebibyte;

[[nodiscard]] inline Sighting InView(float a_screenFraction, float a_distance) {
  return Sighting{ActorRole::kOther, true, true, a_screenFraction, a_distance};
}

[[nodiscard]] inline Sighting OutOfView(float a_distance) {
  return Sighting{ActorRole::kOther, true, false, std::nullopt, a_distance};
}

[[nodiscard]] inline InstalledRender RenderIn(
    RenderPhase a_phase,
    PerLevel<std::optional<LevelCost>> a_costs = HalvingCosts(kFullBytes)) {
  return InstalledRender{std::move(a_phase), std::move(a_costs), std::nullopt};
}

[[nodiscard]] inline Shown ShownAt(ResolutionLevel a_level,
                                   Intermediates a_intermediates = Held{}) {
  return Shown{
      a_level, std::move(a_intermediates), {}, std::nullopt, std::nullopt};
}

[[nodiscard]] inline ActorRecord RecordOf(InstalledRender a_render,
                                          Sighting a_sight = {}) {
  ActorRecord record;
  record.sight = a_sight;
  record.render = std::move(a_render);
  return record;
}

[[nodiscard]] inline World WithEstimate(World a_world) {
  a_world.budget.planned = EstimateOf(a_world, {}).most;
  return a_world;
}

[[nodiscard]] inline ActorScript ScriptedActor(Sighting a_sight) {
  ActorScript actor;
  actor.sightings.push_back({0, a_sight});
  actor.levelCosts = HalvingCosts(kFullBytes);
  return actor;
}
}
