// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Planning.h"

#include "planners/ActorPlanning.h"
#include "studio/Selection.h"

#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Actors {
namespace {
struct MatchInput {
  std::vector<Geometry> geometries;
  std::vector<GeometryRef> refs;
  std::vector<Studio::PieceRef> owners;
};

WornPiece KeysWithDiffusePaths(const PieceFacts &a_piece) {
  WornPiece keys = a_piece.keys;
  for (const GeometryFacts &geometry : a_piece.geometries) {
    keys.diffusePaths.push_back(geometry.diffusePath);
  }
  return keys;
}

GeometryRef RefOf(const PieceFacts &a_piece, std::size_t a_index) {
  const std::string &name = a_piece.geometries[a_index].name;
  std::uint32_t ordinal = 0;
  for (std::size_t i = 0; i < a_index; ++i) {
    if (a_piece.geometries[i].name == name) {
      ++ordinal;
    }
  }
  return GeometryRef{a_piece.ref, name, ordinal};
}

MatchInput MatchInputOf(const ActorFacts &a_facts) {
  MatchInput input;
  const auto actorID = static_cast<std::uint32_t>(a_facts.actor);
  for (const PieceFacts &piece : a_facts.pieces) {
    if (!piece.model) {
      continue;
    }
    const WornPiece keys = KeysWithDiffusePaths(piece);
    const Studio::PieceRef owner{actorID, piece.itemID, piece.ref.firstPerson};
    for (std::size_t i = 0; i < piece.geometries.size(); ++i) {
      const GeometryFacts &geometry = piece.geometries[i];
      Geometry planned;
      planned.identity = GeometryIdentity{piece.ref.addon, geometry.name,
                                          geometry.diffusePath};
      planned.keys = keys;
      planned.firstPerson = piece.ref.firstPerson;
      input.geometries.push_back(std::move(planned));
      input.refs.push_back(RefOf(piece, i));
      input.owners.push_back(owner);
    }
  }
  return input;
}

std::optional<ModelToken> ModelOf(const ActorFacts &a_facts,
                                  const PieceRef &a_piece) {
  for (const PieceFacts &piece : a_facts.pieces) {
    if (piece.ref == a_piece) {
      return piece.model;
    }
  }
  return std::nullopt;
}

InstanceRef InstanceRefOf(const Instance &a_instance,
                          const RecipeCatalog &a_recipes) {
  const std::size_t index = IndexOf(a_instance.recipe);
  const std::span<const Recipe> recipes = a_recipes.Recipes();
  return InstanceRef{index < recipes.size() ? recipes[index].id : std::string{},
                     a_recipes.FingerprintOf(a_instance.recipe),
                     a_instance.enchantment, a_instance.effectKey};
}

ActorPlan KeyedPlanOf(const ActorMatches &a_matches, const MatchInput &a_input,
                      const PlanRequest &a_request) {
  ActorPlan plan;
  for (const Instance &instance : a_matches.instances) {
    plan.instances.push_back({InstanceRefOf(instance, a_request.recipes)});
  }
  for (const Placement &placement : a_matches.placements) {
    const std::size_t instance = IndexOf(placement.instance);
    const std::size_t geometry = IndexOf(placement.geometry);
    if (instance >= plan.instances.size() || geometry >= a_input.refs.size()) {
      continue;
    }
    const GeometryRef &ref = a_input.refs[geometry];
    const std::optional<ModelToken> model = ModelOf(a_request.facts, ref.piece);
    if (!model) {
      continue;
    }
    if (!Find(plan, ref)) {
      plan.geometries.push_back({ref, *model});
    }
    plan.placements.push_back({PlacementRef{plan.instances[instance].ref, ref},
                               placement.key, placement.outputs,
                               placement.priority});
  }
  return plan;
}
}

std::optional<RecipeCatalog>
RecipeCatalog::Of(std::span<const Recipe> a_recipes,
                  std::span<const std::uint64_t> a_fingerprints) noexcept {
  if (a_recipes.size() != a_fingerprints.size()) {
    return std::nullopt;
  }
  return RecipeCatalog{a_recipes, a_fingerprints};
}

RecipeCatalog::RecipeCatalog(
    std::span<const Recipe> a_recipes,
    std::span<const std::uint64_t> a_fingerprints) noexcept
    : recipes_(a_recipes), fingerprints_(a_fingerprints) {}

std::span<const Recipe> RecipeCatalog::Recipes() const noexcept {
  return recipes_;
}

std::uint64_t RecipeCatalog::FingerprintOf(RecipeId a_recipe) const noexcept {
  const std::size_t index = IndexOf(a_recipe);
  return index < fingerprints_.size() ? fingerprints_[index] : 0;
}

ActorPlan PlanActor(const PlanRequest &a_request) {
  const MatchInput input = MatchInputOf(a_request.facts);
  const std::span<const Recipe> recipes = a_request.recipes.Recipes();
  const auto actorID = static_cast<std::uint32_t>(a_request.facts.actor);
  const ActorMatches matches =
      MatchActor(input.geometries, recipes,
                 [&](const Geometry &a_geometry, GeometryId a_id) {
                   const std::size_t index = IndexOf(a_id);
                   const Studio::PieceRef owner = index < input.owners.size()
                                                      ? input.owners[index]
                                                      : Studio::PieceRef{};
                   return Studio::ViewedRecipes(
                       {Resolve(a_geometry.keys, recipes, actorID),
                        a_geometry.keys, owner, a_request.view, recipes});
                 });
  return KeyedPlanOf(matches, input, a_request);
}
}
