// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "actors/Actor.h"
#include "planners/ActorMatches.h"
#include "recipe/Recipe.h"
#include "studio/View.h"

#include <cstdint>
#include <optional>
#include <span>

namespace BetterEnchantmentEffects::Actors {
class RecipeCatalog {
public:
  [[nodiscard]] static std::optional<RecipeCatalog>
  Of(std::span<const Recipe> a_recipes,
     std::span<const std::uint64_t> a_fingerprints) noexcept;
  [[nodiscard]] std::span<const Recipe> Recipes() const noexcept;
  [[nodiscard]] std::uint64_t FingerprintOf(RecipeId a_recipe) const noexcept;

private:
  RecipeCatalog(std::span<const Recipe> a_recipes,
                std::span<const std::uint64_t> a_fingerprints) noexcept;
  std::span<const Recipe> recipes_;
  std::span<const std::uint64_t> fingerprints_;
};

struct PlanRequest {
  const ActorFacts &facts;
  const RecipeCatalog &recipes;
  const Studio::View &view;
};

[[nodiscard]] ActorPlan PlanActor(const PlanRequest &a_request);
}
