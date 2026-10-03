// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"

#include <cstdint>
#include <string>
#include <vector>

namespace actorfixtures {
using namespace BetterEnchantmentEffects::Actors;
using BetterEnchantmentEffects::FormKey;
using BetterEnchantmentEffects::RecipeKey;

inline PieceRef Piece(std::uint32_t a_item) {
  return PieceRef{FormKey{"Skyrim.esm", a_item}, std::nullopt, false};
}

inline GeometryRef Geo(std::uint32_t a_item, std::string a_name) {
  return GeometryRef{Piece(a_item), std::move(a_name), 0};
}

inline InstanceRef Instance(std::string a_id, std::uint64_t a_fingerprint = 1) {
  return InstanceRef{std::move(a_id), a_fingerprint, std::nullopt,
                     std::nullopt};
}

inline PlannedPlacement Placed(const InstanceRef &a_instance,
                               const GeometryRef &a_geometry,
                               int a_priority = 0) {
  return PlannedPlacement{
      PlacementRef{a_instance, a_geometry}, RecipeKey{}, {}, a_priority};
}

struct PlanBuilder {
  ActorPlan plan;
  PlanBuilder &With(const InstanceRef &a_instance,
                    const GeometryRef &a_geometry, std::uint64_t a_model = 1) {
    if (!Find(plan, a_instance)) {
      plan.instances.push_back({a_instance});
    }
    if (!Find(plan, a_geometry)) {
      plan.geometries.push_back({a_geometry, ModelToken{a_model}});
    }
    plan.placements.push_back(Placed(a_instance, a_geometry));
    return *this;
  }
};
}
