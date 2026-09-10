#include "planners/ManagerDecisions.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
[[nodiscard]] std::optional<RecipeId>
RecipeIdOf(std::span<const Recipe> a_store, const Recipe *a_recipe) noexcept {
  if (!a_recipe || a_store.empty()) {
    return std::nullopt;
  }
  const Recipe *first = a_store.data();
  if (a_recipe < first || a_recipe >= first + a_store.size()) {
    return std::nullopt;
  }
  return RecipeId{static_cast<std::size_t>(a_recipe - first)};
}

[[nodiscard]] const Recipe *RecipeAt(std::span<const Recipe> a_store,
                                     RecipeId a_recipe) noexcept {
  const std::size_t index = static_cast<std::size_t>(a_recipe);
  if (index >= a_store.size()) {
    return nullptr;
  }
  return &a_store[index];
}

[[nodiscard]] std::vector<OutputPlacement>
SurfacePlacements(const Recipe &a_recipe, const GeometryIdentity &a_identity) {
  std::vector<OutputPlacement> out;
  for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
    const SurfaceOutput *output = Get<SurfaceOutput>(a_recipe.outputs[i]);
    if (!output) {
      continue;
    }
    const bool selected = Matches(output->selector, a_identity);
    out.push_back(OutputPlacement{OutputIndex{i}, selected,
                                  selected ? std::string{}
                                           : std::string{"selector did not "
                                                         "match"}});
  }
  return out;
}

[[nodiscard]] InstanceId
InstanceFor(ActorState &a_state, RecipeId a_recipe,
            const std::optional<FormKey> &a_enchantment, int a_priority) {
  if (const std::optional<InstanceId> existing =
          FindInstance(a_state, a_recipe, a_enchantment)) {
    Instance &instance = a_state.instances[static_cast<std::size_t>(*existing)];
    instance.priority = std::max(instance.priority, a_priority);
    return *existing;
  }
  a_state.instances.push_back(Instance{a_recipe, a_enchantment, a_priority});
  return InstanceId{a_state.instances.size() - 1};
}
}

ActorState MatchActor(std::span<const Piece> a_pieces,
                      std::span<const Recipe> a_store,
                      const RecipeResolver &a_resolver) {
  ActorState state;
  state.pieces.assign(a_pieces.begin(), a_pieces.end());
  for (std::size_t p = 0; p < state.pieces.size(); ++p) {
    const Piece &piece = state.pieces[p];
    for (const ResolvedRecipe &resolved : a_resolver(piece, p)) {
      const std::optional<RecipeId> recipe =
          RecipeIdOf(a_store, resolved.recipe);
      if (!recipe) {
        continue;
      }
      const InstanceId instance = InstanceFor(
          state, *recipe, piece.keys.enchantment, resolved.priority);
      Placement placement;
      placement.instance = instance;
      placement.piece = PieceId{p};
      placement.key = resolved.key;
      placement.outputs = SurfacePlacements(*resolved.recipe, piece.identity);
      state.placements.push_back(std::move(placement));
    }
  }
  return state;
}

ActorState MatchActor(std::span<const Piece> a_pieces,
                      std::span<const Recipe> a_store) {
  return MatchActor(a_pieces, a_store,
                    [a_store](const Piece &a_piece, std::size_t) {
                      return Resolve(a_piece.keys, a_store);
                    });
}

GeometryPlacement PlaceGeometry(const ActorState &a_state,
                                std::span<const Recipe> a_store,
                                PieceId a_piece) {
  GeometryPlacement out;
  for (const PlacementId placementId : PlacementsOfPiece(a_state, a_piece)) {
    const Placement *placement = PlacementAt(a_state, placementId);
    if (!placement) {
      continue;
    }
    const Instance *instance = InstanceAt(a_state, placement->instance);
    if (!instance) {
      continue;
    }
    const Recipe *recipe = RecipeAt(a_store, instance->recipe);
    if (!recipe) {
      continue;
    }
    PlacedRecipe row;
    row.recipe = recipe;
    row.priority = instance->priority;
    for (const OutputPlacement &output : placement->outputs) {
      if (output.selected) {
        row.outputs.push_back(static_cast<std::size_t>(output.output));
      }
    }
    out.placed.push_back(std::move(row));
    out.sources.push_back(placementId);
  }
  out.plan = PlanGeometry(out.placed);
  return out;
}

ActorLightPlan PlaceLights(const ActorState &a_state,
                           std::span<const Recipe> a_store) {
  ActorLightPlan out;
  for (std::size_t i = 0; i < a_state.instances.size(); ++i) {
    const Instance &instance = a_state.instances[i];
    out.placed.push_back(PlacedRecipe{
        RecipeAt(a_store, instance.recipe), instance.priority, {}});
    out.sources.push_back(InstanceId{i});
  }
  out.plan = PlanLights(out.placed);
  return out;
}

std::vector<RecipeId> RetirePlan(const ActorState &a_state) {
  std::vector<RecipeId> out;
  for (std::size_t i = 0; i < a_state.instances.size(); ++i) {
    bool live = false;
    for (const PlacementId placementId :
         PlacementsOfInstance(a_state, InstanceId{i})) {
      const Placement *placement = PlacementAt(a_state, placementId);
      if (!placement) {
        continue;
      }
      const Piece *piece = PieceAt(a_state, placement->piece);
      if (piece && !piece->lost) {
        live = true;
        break;
      }
    }
    if (!live) {
      out.push_back(a_state.instances[i].recipe);
    }
  }
  return out;
}
}
