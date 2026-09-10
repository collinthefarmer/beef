#include "planners/ManagerDecisions.h"
#include "test_support.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
Recipe GlowRecipe() {
  Recipe recipe;
  recipe.id = "glow";
  recipe.keys.push_back(RecipeKey{});
  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kEmissive;
  recipe.outputs.push_back(surface);
  return recipe;
}

Recipe RingRecipe() {
  Recipe recipe;
  recipe.id = "ring";
  recipe.keys.push_back(
      RecipeKey{KeyKind::kMaterial, KeyOperandValue{std::string{"*"}}});
  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kEmissive;
  surface.selector.anyOf.push_back(
      SelectorClause{SelectorKind::kGeometry, std::string{"*ring*"}});
  recipe.outputs.push_back(surface);
  recipe.outputs.push_back(LightOutput{});
  return recipe;
}

Piece MakePiece(std::string a_name, std::string a_diffuse,
                std::optional<FormKey> a_enchantment) {
  Piece piece;
  piece.identity.name = std::move(a_name);
  piece.identity.diffusePath = a_diffuse;
  piece.keys.diffusePaths.push_back(std::move(a_diffuse));
  piece.keys.enchantment = std::move(a_enchantment);
  return piece;
}

std::size_t PlacementsForInstance(const ActorState &a_state,
                                  InstanceId a_instance) {
  std::size_t count = 0;
  for (const Placement &placement : a_state.placements) {
    if (placement.instance == a_instance) {
      ++count;
    }
  }
  return count;
}
}

int main() {
  const std::vector<Recipe> store{GlowRecipe(), RingRecipe()};
  const FormKey enchantment{"Test.esp", 0x123};
  const std::vector<Piece> pieces{
      MakePiece("ring01", "ring.dds", std::nullopt),
      MakePiece("body01", "body.dds", std::nullopt),
      MakePiece("amulet01", "amulet.dds", enchantment)};

  ActorState state = MatchActor(pieces, store);

  Check(state.pieces.size() == 3, "MatchActor keeps the actor's pieces");
  Check(state.instances.size() == 4,
        "MatchActor instances at per-recipe-per-enchantment grain");
  Check(state.placements.size() == 6,
        "MatchActor makes one placement per piece per matched recipe");

  const auto unenchantedGlow = FindInstance(state, RecipeId{0}, std::nullopt);
  const auto enchantedGlow = FindInstance(state, RecipeId{0}, enchantment);
  Check(unenchantedGlow.has_value() && enchantedGlow.has_value() &&
            *unenchantedGlow != *enchantedGlow,
        "the same recipe splits into distinct instances by enchantment");
  Check(unenchantedGlow && PlacementsForInstance(state, *unenchantedGlow) == 2,
        "unenchanted pieces share one instance per recipe");

  GeometryPlacement ring = PlaceGeometry(state, store, PieceId{0});
  Check(ring.placed.size() == 2 && ring.sources.size() == 2,
        "PlaceGeometry builds a placed row per placement of the piece");
  Check(ring.plan.slots.size() == 1 && ring.plan.slots[0].chain.size() == 2,
        "PlaceGeometry merges both selected outputs into the emissive slot");

  GeometryPlacement body = PlaceGeometry(state, store, PieceId{1});
  const bool ringOutputDropped =
      std::ranges::any_of(body.placed, [](const PlacedRecipe &a_row) {
        return a_row.outputs.empty();
      });
  Check(ringOutputDropped,
        "PlaceGeometry drops an output whose selector does not match");
  Check(body.plan.slots.size() == 1 && body.plan.slots[0].chain.size() == 1,
        "the unselected ring output does not reach the body's merge");

  bool sawSelectorProblem = false;
  for (const Placement &placement : state.placements) {
    if (placement.piece != PieceId{1}) {
      continue;
    }
    for (const OutputPlacement &output : placement.outputs) {
      if (!output.selected && output.problem == "selector did not match") {
        sawSelectorProblem = true;
      }
    }
  }
  Check(sawSelectorProblem,
        "MatchActor records why an output's selector did not match");

  ActorLightPlan lights = PlaceLights(state, store);
  Check(lights.placed.size() == 4 && lights.sources.size() == 4,
        "PlaceLights offers one light source per instance");
  Check(lights.plan.shown.size() == 2,
        "PlaceLights shows a light only for instances of a light recipe");
  bool lightsMapToRingInstances = true;
  for (const LightContribution &shown : lights.plan.shown) {
    const std::size_t at = static_cast<std::size_t>(shown.placed);
    if (at >= lights.sources.size()) {
      lightsMapToRingInstances = false;
      continue;
    }
    const Instance *instance = InstanceAt(state, lights.sources[at]);
    if (!instance || instance->recipe != RecipeId{1}) {
      lightsMapToRingInstances = false;
    }
  }
  Check(lightsMapToRingInstances,
        "each shown light maps back to a ring-recipe instance");

  Check(RetirePlan(state).empty(),
        "RetirePlan retires nothing while every piece is live");

  ActorState onePieceLost = state;
  onePieceLost.pieces[0].lost = true;
  Check(RetirePlan(onePieceLost).empty(),
        "an instance stays while another live piece still backs it");

  ActorState enchantedLost = state;
  enchantedLost.pieces[2].lost = true;
  const std::vector<RecipeId> retired = RetirePlan(enchantedLost);
  Check(retired.size() == 2,
        "RetirePlan retires only the instances the lost piece backed alone");
  const bool retiredBoth =
      std::ranges::find(retired, RecipeId{0}) != retired.end() &&
      std::ranges::find(retired, RecipeId{1}) != retired.end();
  Check(retiredBoth, "both enchanted instances are retired by recipe");

  return test::Finish("planners managerdecisions");
}
