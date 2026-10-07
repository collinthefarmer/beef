// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Planning.h"

#include "planners/ActorPlanning.h"
#include "studio/Selection.h"

#include <algorithm>
#include <optional>
#include <span>
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

MatchInput MatchInputOf(const ActorFacts &a_facts) {
  MatchInput input;
  const auto actorID = static_cast<std::uint32_t>(a_facts.actor);
  for (const PieceFacts *planned : PlannedPieces(a_facts)) {
    const PieceFacts &piece = *planned;
    const WornPiece keys = KeysWithDiffusePaths(piece);
    const Studio::PieceRef owner{actorID, piece.itemID, piece.ref.firstPerson};
    for (std::size_t i = 0; i < piece.geometries.size(); ++i) {
      const std::optional<GeometryRef> ref = GeometryRefOf(piece, i);
      if (!ref) {
        continue;
      }
      const GeometryFacts &geometry = piece.geometries[i];
      Geometry planned;
      planned.identity = GeometryIdentity{piece.ref.addon, geometry.name,
                                          geometry.diffusePath};
      planned.keys = keys;
      planned.firstPerson = piece.ref.firstPerson;
      input.geometries.push_back(std::move(planned));
      input.refs.push_back(*ref);
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
  const Recipe *recipe = a_recipes.At(a_instance.recipe);
  return InstanceRef{recipe ? recipe->id : std::string{},
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

bool Claimed(const PlanRequest &a_request, const GeometryRef &a_geometry) {
  const std::optional<ModelToken> model =
      ModelOf(a_request.facts, a_geometry.piece);
  return model &&
         std::ranges::contains(a_request.claims, Claim{a_geometry, *model});
}
}

std::optional<GeometryRef> GeometryRefOf(const PieceFacts &a_piece,
                                         std::size_t a_index) {
  if (a_index >= a_piece.geometries.size()) {
    return std::nullopt;
  }
  const std::string &name = a_piece.geometries[a_index].name;
  std::uint32_t ordinal = 0;
  for (std::size_t i = 0; i < a_index; ++i) {
    if (a_piece.geometries[i].name == name) {
      ++ordinal;
    }
  }
  return GeometryRef{a_piece.ref, name, ordinal};
}

std::vector<const PieceFacts *> PlannedPieces(const ActorFacts &a_facts) {
  std::vector<const PieceFacts *> planned;
  for (const PieceFacts &piece : a_facts.pieces) {
    if (piece.model) {
      planned.push_back(&piece);
    }
  }
  return planned;
}

PlannedActor PlanActor(const PlanRequest &a_request) {
  const MatchInput input = MatchInputOf(a_request.facts);
  const std::span<const Recipe> recipes = a_request.recipes.Recipes();
  const auto actorID = static_cast<std::uint32_t>(a_request.facts.actor);
  ActorMatches matches =
      MatchActor(input.geometries, recipes,
                 [&](const Geometry &a_geometry, GeometryId a_id) {
                   const std::size_t index = IndexOf(a_id);
                   if (index >= input.refs.size() ||
                       Claimed(a_request, input.refs[index])) {
                     return std::vector<ResolvedRecipe>{};
                   }
                   const Studio::PieceRef owner = index < input.owners.size()
                                                      ? input.owners[index]
                                                      : Studio::PieceRef{};
                   return Studio::ViewedRecipes(
                       {Resolve(a_geometry.keys, recipes, actorID),
                        a_geometry.keys, owner, a_request.view, recipes});
                 });
  ActorPlan plan = KeyedPlanOf(matches, input, a_request);
  return {std::move(plan), std::move(matches)};
}
}
