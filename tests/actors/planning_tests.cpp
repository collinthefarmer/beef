// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Planning.h"
#include "test_support.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects::Actors;
using BetterEnchantmentEffects::CatalogEntry;
using BetterEnchantmentEffects::FormKey;
using BetterEnchantmentEffects::KeyKind;
using BetterEnchantmentEffects::KeyOperandValue;
using BetterEnchantmentEffects::Recipe;
using BetterEnchantmentEffects::RecipeCatalog;
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
    piece.geometries.push_back({"Body", "body.dds"});
    piece.geometries.push_back({"Body", "body_trim.dds"});
  }
  return piece;
}

ActorFacts LoadedActor(std::vector<PieceFacts> a_pieces) {
  ActorFacts facts;
  facts.actor = ActorId{0x14};
  facts.loaded = true;
  facts.pieces = std::move(a_pieces);
  return facts;
}

struct Fixture {
  std::vector<CatalogEntry> entries{{EmissiveRecipe("glow"), 11},
                                    {EmissiveRecipe("ring"), 22}};
  RecipeCatalog catalog = RecipeCatalog::Of(entries);
  Studio::View view;

  ActorPlan Plan(const ActorFacts &a_facts) const {
    return PlanActor({a_facts, catalog, view}).plan;
  }
};

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
  fixture.entries.push_back({pinned, 33});
  const RecipeCatalog catalog = RecipeCatalog::Of(fixture.entries);
  Studio::View view;
  view.pin = Studio::Pin{Studio::PieceRef{0x14, 0x12E49, false}, "pinned"};
  const ActorFacts facts = LoadedActor({CuirassFacts(false, true)});
  const ActorPlan plan = PlanActor({facts, catalog, view}).plan;
  test::Check(Find(plan, InstanceRef{"pinned", 33, std::nullopt,
                                     std::nullopt}) != nullptr,
              "a pin on the actor's piece adds its recipe");
  view.pin->piece.armorID = 0x999;
  const ActorPlan elsewhere = PlanActor({facts, catalog, view}).plan;
  test::Check(Find(elsewhere, InstanceRef{"pinned", 33, std::nullopt,
                                          std::nullopt}) == nullptr,
              "a pin on another piece adds nothing");
}

PieceFacts Gauntlets(std::optional<FormKey> a_addon, std::uint64_t a_model) {
  PieceFacts piece;
  piece.ref = PieceRef{FormKey{"Skyrim.esm", 0x12E46}, a_addon, false};
  piece.itemID = 0x12E46;
  piece.keys.armor = piece.ref.item;
  piece.model = ModelToken{a_model};
  piece.geometries.push_back({"Hands", "hands.dds"});
  return piece;
}

void MatchesFollowThePlannedPieces() {
  const Fixture fixture;
  const ActorFacts facts =
      LoadedActor({CuirassFacts(false, true), CuirassFacts(true, false),
                   Gauntlets(std::nullopt, 3)});
  const std::vector<const PieceFacts *> planned = PlannedPieces(facts);
  test::Check(planned.size() == 2 && planned[0] == &facts.pieces[0] &&
                  planned[1] == &facts.pieces[2],
              "the planned pieces are the attached ones, in facts order");
  const PlannedActor result = PlanActor({facts, fixture.catalog, fixture.view});
  std::vector<std::string> order;
  for (const BetterEnchantmentEffects::Geometry &geometry :
       result.matches.geometries) {
    order.push_back(geometry.identity.diffusePath);
  }
  test::Check(order == std::vector<std::string>{"body.dds", "body_trim.dds",
                                                "hands.dds"},
              "the matches list each planned piece's geometries in order");
}

void LeavesAClaimedGeometryOut() {
  const Fixture fixture;
  const ActorFacts facts = LoadedActor({CuirassFacts(false, true)});
  const GeometryRef trim{Cuirass(false), "Body", 1};
  const std::vector<Claim> claims{Claim{trim, ModelToken{1}}};
  const PlannedActor result =
      PlanActor({facts, fixture.catalog, fixture.view, claims});
  test::Check(Find(result.plan, trim) == nullptr &&
                  Find(result.plan, GeometryRef{Cuirass(false), "Body", 0}),
              "a claimed geometry leaves the plan and its sibling stays");
  test::Check(result.matches.geometries.size() == 2,
              "the matches keep the claimed geometry's position");
  bool placedOnTrim = false;
  for (const BetterEnchantmentEffects::Placement &placement :
       result.matches.placements) {
    placedOnTrim = placedOnTrim ||
                   BetterEnchantmentEffects::IndexOf(placement.geometry) == 1;
  }
  test::Check(!placedOnTrim, "nothing is placed on the claimed geometry");
  const std::vector<Claim> stale{Claim{trim, ModelToken{9}}};
  const ActorPlan replanned =
      PlanActor({facts, fixture.catalog, fixture.view, stale}).plan;
  test::Check(Find(replanned, trim) != nullptr,
              "a claim on another model leaves the geometry planned");
}

void SeparatesTwoAddonsOfOneArmor() {
  const Fixture fixture;
  const FormKey left{"Skyrim.esm", 0x100};
  const FormKey right{"Skyrim.esm", 0x101};
  const ActorPlan plan =
      fixture.Plan(LoadedActor({Gauntlets(left, 3), Gauntlets(right, 4)}));
  const PlannedGeometry *first = Find(
      plan, GeometryRef{PieceRef{FormKey{"Skyrim.esm", 0x12E46}, left, false},
                        "Hands", 0});
  const PlannedGeometry *second = Find(
      plan, GeometryRef{PieceRef{FormKey{"Skyrim.esm", 0x12E46}, right, false},
                        "Hands", 0});
  test::Check(first && second && first->model == ModelToken{3} &&
                  second->model == ModelToken{4},
              "each attached addon of one armor is its own piece");
}

void SameFactsGiveTheSamePlan() {
  const Fixture fixture;
  const ActorFacts facts = LoadedActor({CuirassFacts(false, true)});
  test::Check(fixture.Plan(facts) == fixture.Plan(facts),
              "planning is deterministic");
}
}

int main() {
  PlansEveryGeometryOfAnAttachedPiece();
  SkipsAPieceWithoutAModel();
  FollowsTheStudioView();
  KeepsFirstPersonPiecesApart();
  PinsARecipeOnTheOwningPiece();
  MatchesFollowThePlannedPieces();
  SeparatesTwoAddonsOfOneArmor();
  LeavesAClaimedGeometryOut();
  SameFactsGiveTheSamePlan();
  return test::Finish("actor planning");
}
