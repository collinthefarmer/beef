// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"
#include "planners/ActorMatches.h"
#include "recipe/Recipe.h"
#include "recipe/RecipeCatalog.h"
#include "studio/View.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects::Actors {
struct PlanRequest {
  const ActorFacts &facts;
  const RecipeCatalog &recipes;
  const Studio::View &view;
  std::span<const Claim> claims;
};

struct PlannedActor {
  ActorPlan plan;
  ActorMatches matches;
};

[[nodiscard]] std::optional<GeometryRef>
GeometryRefOf(const PieceFacts &a_piece, std::size_t a_index);
[[nodiscard]] std::vector<const PieceFacts *>
PlannedPieces(const ActorFacts &a_facts);
[[nodiscard]] PlannedActor PlanActor(const PlanRequest &a_request);
}
