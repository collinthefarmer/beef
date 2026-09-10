#pragma once

#include "planners/ActorState.h"
#include "recipe/Merge.h"
#include "recipe/Recipe.h"

#include <functional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
struct GeometryPlacement {
  std::vector<PlacedRecipe> placed;
  std::vector<PlacementId> sources;
  GeometryPlan plan;
};

struct ActorLightPlan {
  std::vector<PlacedRecipe> placed;
  std::vector<InstanceId> sources;
  LightPlan plan;
};

using RecipeResolver =
    std::function<std::vector<ResolvedRecipe>(const Piece &, std::size_t)>;

[[nodiscard]] ActorState MatchActor(std::span<const Piece> a_pieces,
                                    std::span<const Recipe> a_store);
[[nodiscard]] ActorState MatchActor(std::span<const Piece> a_pieces,
                                    std::span<const Recipe> a_store,
                                    const RecipeResolver &a_resolver);
[[nodiscard]] GeometryPlacement PlaceGeometry(const ActorState &a_state,
                                              std::span<const Recipe> a_store,
                                              PieceId a_piece);
[[nodiscard]] ActorLightPlan PlaceLights(const ActorState &a_state,
                                         std::span<const Recipe> a_store);
[[nodiscard]] std::vector<RecipeId> RetirePlan(const ActorState &a_state);
}
