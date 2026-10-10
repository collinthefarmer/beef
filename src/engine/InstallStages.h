// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Settings.h"
#include "engine/LiveActor.h"
#include "planners/InstallProgram.h"
#include "recipe/Recipe.h"

#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
struct LocatedGeometry {
  LivePiece &piece;
  LiveGeometry &bound;
  GeometryId id;
};

[[nodiscard]] std::optional<LocatedGeometry>
LocateGeometry(LiveActor &a_state, GeometryId a_geometry) noexcept;
[[nodiscard]] SurfacePlan SurfacePlanOf(const LiveActor &a_state);
void KeepSurfacePlan(LiveActor &a_state, const SurfacePlan &a_plan);
void WriteOutputs(LiveActor &a_state,
                  std::span<const PlacementOutputs> a_outputs);
[[nodiscard]] std::vector<InstanceId>
CompiledInstancesOf(const LiveActor &a_state);
[[nodiscard]] BoundSurfaces BoundSurfacesOf(LiveGeometry &a_bound,
                                            GeometryId a_geometry);
[[nodiscard]] std::vector<BoundSurfaces> BoundSurfacesOf(LiveActor &a_state);
[[nodiscard]] Resolution ScaleResolutionOf(TextureScale a_scale) noexcept;
}
