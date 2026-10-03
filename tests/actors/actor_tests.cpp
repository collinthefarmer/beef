// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Actor.h"
#include "test_support.h"

#include <string>

using namespace BetterEnchantmentEffects::Actors;
using BetterEnchantmentEffects::FormKey;
using BetterEnchantmentEffects::RecipeKey;

namespace {
PieceRef Cuirass() {
  return PieceRef{FormKey{"Skyrim.esm", 0x12E49}, std::nullopt, false};
}

GeometryRef Body(std::uint32_t a_ordinal) {
  return GeometryRef{Cuirass(), "Body", a_ordinal};
}

InstanceRef Circuit(std::uint64_t a_fingerprint) {
  return InstanceRef{"arcane-circuit", a_fingerprint, std::nullopt,
                     std::nullopt};
}

void RefsCompareByEveryField() {
  test::Check(Body(0) == Body(0), "equal geometry refs compare equal");
  test::Check(!(Body(0) == Body(1)),
              "the ordinal separates geometries of equal name");
  PieceRef firstPerson = Cuirass();
  firstPerson.firstPerson = true;
  test::Check(!(Cuirass() == firstPerson),
              "the first-person piece is a different piece");
  test::Check(!(Circuit(1) == Circuit(2)),
              "a recipe edit gives a different instance ref");
}

void FindLooksUpByRef() {
  ActorPlan plan;
  plan.instances.push_back({Circuit(1)});
  plan.geometries.push_back({Body(0), ModelToken{7}});
  plan.placements.push_back(
      {PlacementRef{Circuit(1), Body(0)}, RecipeKey{}, {}, 0});
  test::Check(Find(plan, Circuit(1)) == &plan.instances.front(),
              "an instance is found by its ref");
  test::Check(Find(plan, Circuit(2)) == nullptr,
              "another fingerprint is not found");
  test::Check(Find(plan, Body(1)) == nullptr, "another ordinal is not found");
  test::Check(Find(plan, PlacementRef{Circuit(1), Body(0)}) ==
                  &plan.placements.front(),
              "a placement is found by instance and geometry");
}
}

int main() {
  RefsCompareByEveryField();
  FindLooksUpByRef();
  return test::Finish("actor refs");
}
