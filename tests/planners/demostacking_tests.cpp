// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ActorPlanning.h"
#include "test_support.h"

#include <array>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  std::vector<Recipe> recipes;
  const auto folder =
      test::Fixtures().parent_path().parent_path() / "recipes/examples";
  for (const std::string name :
       {"arcane-circuit", "resonant-ward", "winterglass"}) {
    auto loaded = ParseRecipe(test::ReadFile(folder / (name + ".json")), name);
    Check(loaded.recipe && !loaded.HasErrors(), "demo recipe loads cleanly");
    if (!loaded.recipe || loaded.HasErrors())
      return test::Finish("demo stacking");
    auto roundtrip = ParseRecipe(SerializeRecipe(*loaded.recipe), name);
    Check(roundtrip.recipe && !roundtrip.HasErrors(),
          "demo recipe survives serialization");
    recipes.push_back(*loaded.recipe);
  }
  Geometry geometry;
  geometry.keys.armor = FormKey{"BetterEnchantmentEffectsDemo.esp", 0x803};
  geometry.keys.enchantment =
      FormKey{"BetterEnchantmentEffectsDemo.esp", 0x802};
  geometry.keys.magicEffects = {{"Skyrim.esm", 0x493AA},
                                {"BetterEnchantmentEffectsDemo.esp", 0x801}};
  geometry.keys.keywords = {{"BetterEnchantmentEffectsDemo.esp", 0x800},
                            {"Skyrim.esm", 0x6BBD2}};
  const std::array geometries{geometry};
  const auto actor = MatchActor(geometries, recipes);
  Check(actor.placements.size() == 3,
        "all three real recipes select the same demo cuirass");
  const auto plan = PlanGeometryPlacement(actor, recipes, GeometryId{0});
  const auto *material =
      SlotPlanOf(plan.plan, Surface::kMaterial, Slot::kEmissive);
  const auto *shell = SlotPlanOf(plan.plan, Surface::kShell, Slot::kEmissive);
  Check(material && material->chain.size() == 1 && material->replaced.empty(),
        "Arcane Circuit retains its material contribution");
  Check(shell && shell->chain.size() == 1 && shell->replaced.empty(),
        "Resonant Ward adds its shell contribution");
  for (const auto slot : {Slot::kDiffuse, Slot::kRmaos}) {
    const auto *frost = SlotPlanOf(plan.plan, Surface::kMaterial, slot);
    Check(
        frost && frost->chain.size() == 1 && frost->replaced.empty(),
        "Winterglass contributes distinct material slots without replacement");
  }
  for (const auto slot : {Slot::kHeight, Slot::kFuzz}) {
    const auto *detail = SlotPlanOf(plan.plan, Surface::kMaterial, slot);
    Check(detail && detail->chain.size() == 1 && detail->replaced.empty(),
          "configured Arcane Circuit contributes height and fuzz");
  }
  const auto lights = PlanActorLights(actor, recipes);
  Check(lights.plan.shown.size() == 1 && lights.plan.replaced.empty(),
        "the ward contributes one light without replacing another recipe");
  geometry.keys.armor = FormKey{"Skyrim.esm", 0x1394D};
  const auto otherArmor = Resolve(geometry.keys, recipes);
  Check(otherArmor.size() == 2,
        "effect and keyword recipes still match another armor form");
  for (const auto &match : otherArmor)
    Check(match.recipe->id != "winterglass",
          "Winterglass requires the exact demo armor form");
  geometry.keys.keywords.pop_back();
  const auto withoutHeavy = Resolve(geometry.keys, recipes);
  Check(withoutHeavy.size() == 1 &&
            withoutHeavy.front().recipe->id == "arcane-circuit",
        "collection membership alone cannot satisfy the ward");
  geometry.keys.keywords = {{"Skyrim.esm", 0x6BBD2}};
  Check(Resolve(geometry.keys, recipes).empty(),
        "heavy armor alone cannot satisfy any demo recipe on another form");
  geometry.keys.armor = FormKey{"BetterEnchantmentEffectsDemo.esp", 0x803};
  geometry.keys.enchantment.reset();
  geometry.keys.magicEffects.clear();
  geometry.keys.keywords.clear();
  const auto exactArmor = Resolve(geometry.keys, recipes);
  Check(exactArmor.size() == 1 &&
            exactArmor.front().recipe->id == "winterglass",
        "Winterglass selects its armor independently of effects and keywords");
  return test::Finish("demo stacking");
}
