// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Planning.h"
#include "test_support.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects::Actors;
using BetterEnchantmentEffects::FormKey;
using BetterEnchantmentEffects::GeometryIdentity;
using BetterEnchantmentEffects::KeyKind;
using BetterEnchantmentEffects::KeyOperandValue;
using BetterEnchantmentEffects::Recipe;
using BetterEnchantmentEffects::RecipeKey;
using BetterEnchantmentEffects::Slot;
using BetterEnchantmentEffects::Surface;
using BetterEnchantmentEffects::SurfaceOutput;
namespace Studio = BetterEnchantmentEffects::Studio;

namespace {
Recipe EmissiveRecipe(std::string a_id) {
  Recipe recipe;
  recipe.id = std::move(a_id);
  recipe.keys.push_back(
      RecipeKey{KeyKind::kMaterial, KeyOperandValue{std::string{"*"}}});
  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kEmissive;
  recipe.outputs.push_back(surface);
  return recipe;
}

PieceRef Cuirass(bool a_firstPerson) {
  return PieceRef{FormKey{"Skyrim.esm", 0x12E49}, std::nullopt, a_firstPerson};
}

PieceFacts CuirassFacts(bool a_firstPerson, bool a_modelAttached) {
  PieceFacts piece;
  piece.ref = Cuirass(a_firstPerson);
  piece.itemID = 0x12E49;
  piece.keys.armor = piece.ref.item;
  if (a_modelAttached) {
    piece.model = ModelToken{a_firstPerson ? 2u : 1u};
    piece.geometries.push_back(
        {GeometryRef{piece.ref, "Body", 0},
         GeometryIdentity{std::nullopt, "Body", "body.dds"}});
    piece.geometries.push_back(
        {GeometryRef{piece.ref, "Body", 1},
         GeometryIdentity{std::nullopt, "Body", "body_trim.dds"}});
  }
  return piece;
}

ActorFacts LoadedActor(std::vector<PieceFacts> a_pieces) {
  ActorFacts facts;
  facts.actor = ActorId{0x14};
  facts.loaded = true;
  facts.inRange = true;
  facts.pieces = std::move(a_pieces);
  return facts;
}

struct Fixture {
  std::vector<Recipe> recipes{EmissiveRecipe("glow"), EmissiveRecipe("ring")};
  std::array<std::uint64_t, 2> fingerprints{11, 22};
  RecipeCatalog catalog = *RecipeCatalog::Of(recipes, fingerprints);
  Studio::View view;

  ActorPlan Plan(const ActorFacts &a_facts) const {
    return PlanActor({a_facts, catalog, view});
  }
};

void CatalogRejectsMismatchedFingerprints() {
  const std::vector<Recipe> recipes{EmissiveRecipe("glow")};
  const std::array<std::uint64_t, 2> two{1, 2};
  test::Check(!RecipeCatalog::Of(recipes, two).has_value(),
              "a catalog needs one fingerprint per recipe");
}

void PlansEveryGeometryOfAnAttachedPiece() {
  const Fixture fixture;
  const ActorPlan plan = fixture.Plan(LoadedActor({CuirassFacts(false, true)}));
  test::Equal(plan.instances.size(), std::size_t{2},
              "each matching recipe is one instance");
  test::Equal(plan.geometries.size(), std::size_t{2},
              "both geometries of the piece are planned");
  test::Equal(plan.placements.size(), std::size_t{4},
              "each instance is placed on each geometry");
  test::Check(Find(plan, GeometryRef{Cuirass(false), "Body", 1}) != nullptr,
              "the ordinal separates geometries of equal name");
  test::Check(plan.geometries.front().model == ModelToken{1},
              "a planned geometry carries its piece's model token");
  const PlannedInstance *glow =
      Find(plan, InstanceRef{"glow", 11, std::nullopt, std::nullopt});
  test::Check(glow != nullptr, "the instance ref carries the fingerprint");
}

void SkipsAPieceWithoutAModel() {
  const Fixture fixture;
  const ActorPlan plan =
      fixture.Plan(LoadedActor({CuirassFacts(false, false)}));
  test::Check(plan.geometries.empty() && plan.placements.empty(),
              "a piece whose model has not attached plans nothing");
}

void PlansNothingForAnAbsentActor() {
  const Fixture fixture;
  ActorFacts unloaded = LoadedActor({CuirassFacts(false, true)});
  unloaded.loaded = false;
  ActorFacts far = LoadedActor({CuirassFacts(false, true)});
  far.inRange = false;
  test::Check(fixture.Plan(unloaded) == ActorPlan{},
              "an unloaded actor has an empty plan");
  test::Check(fixture.Plan(far) == ActorPlan{},
              "an actor out of range has an empty plan");
}

void FollowsTheStudioView() {
  Fixture shown;
  shown.view.recipeSet = {"ring"};
  const ActorPlan ringOnly =
      shown.Plan(LoadedActor({CuirassFacts(false, true)}));
  test::Check(ringOnly.instances.size() == 1 &&
                  ringOnly.instances.front().ref.recipe == "ring",
              "the studio recipe set limits the plan");

  Fixture isolated;
  isolated.view.isolation = Studio::Isolation::ForRecipe("glow");
  const ActorPlan glowOnly =
      isolated.Plan(LoadedActor({CuirassFacts(false, true)}));
  test::Check(glowOnly.instances.size() == 1 &&
                  glowOnly.instances.front().ref.recipe == "glow",
              "studio isolation limits the plan");
}

void KeepsFirstPersonPiecesApart() {
  const Fixture fixture;
  const ActorPlan plan = fixture.Plan(
      LoadedActor({CuirassFacts(false, true), CuirassFacts(true, true)}));
  test::Equal(plan.geometries.size(), std::size_t{4},
              "third- and first-person geometries are separate");
  const PlannedGeometry *firstPerson =
      Find(plan, GeometryRef{Cuirass(true), "Body", 0});
  test::Check(firstPerson != nullptr && firstPerson->model == ModelToken{2},
              "a first-person geometry carries the first-person model");
}

void PinsARecipeOnTheOwningPiece() {
  Recipe pinned = EmissiveRecipe("pinned");
  pinned.keys = {
      RecipeKey{KeyKind::kMaterial, KeyOperandValue{std::string{"other.dds"}}}};
  Fixture fixture;
  fixture.recipes.push_back(pinned);
  const std::array<std::uint64_t, 3> fingerprints{11, 22, 33};
  const RecipeCatalog catalog =
      *RecipeCatalog::Of(fixture.recipes, fingerprints);
  Studio::View view;
  view.pin = Studio::Pin{Studio::PieceRef{0x14, 0x12E49, false}, "pinned"};
  const ActorFacts facts = LoadedActor({CuirassFacts(false, true)});
  const ActorPlan plan = PlanActor({facts, catalog, view});
  test::Check(Find(plan, InstanceRef{"pinned", 33, std::nullopt,
                                     std::nullopt}) != nullptr,
              "a pin on the actor's piece adds its recipe");
  view.pin->piece.armorID = 0x999;
  const ActorPlan elsewhere = PlanActor({facts, catalog, view});
  test::Check(Find(elsewhere, InstanceRef{"pinned", 33, std::nullopt,
                                          std::nullopt}) == nullptr,
              "a pin on another piece adds nothing");
}

void SameFactsGiveTheSamePlan() {
  const Fixture fixture;
  const ActorFacts facts = LoadedActor({CuirassFacts(false, true)});
  test::Check(fixture.Plan(facts) == fixture.Plan(facts),
              "planning is deterministic");
}
}

int main() {
  CatalogRejectsMismatchedFingerprints();
  PlansEveryGeometryOfAnAttachedPiece();
  SkipsAPieceWithoutAModel();
  PlansNothingForAnAbsentActor();
  FollowsTheStudioView();
  KeepsFirstPersonPiecesApart();
  PinsARecipeOnTheOwningPiece();
  SameFactsGiveTheSamePlan();
  return test::Finish("actor planning");
}
