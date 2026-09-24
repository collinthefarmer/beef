#include "recipe/Merge.h"
#include "test_support.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
SurfaceOutput Emissive(bool a_replace, bool a_strength) {
  SurfaceOutput output;
  output.surface = Surface::kMaterial;
  output.slot = Slot::kEmissive;
  output.replace = a_replace;
  if (a_strength) {
    output.scalars.strength = Param{1.0f};
  }
  return output;
}

SurfaceOutput Diffuse() {
  SurfaceOutput output;
  output.surface = Surface::kMaterial;
  output.slot = Slot::kDiffuse;
  return output;
}

LightOutput Light(bool a_replace) {
  LightOutput output;
  output.replace = a_replace;
  return output;
}

Recipe RecipeWith(std::vector<Output> a_outputs) {
  Recipe recipe;
  static unsigned identity = 0;
  recipe.id = std::to_string(identity++);
  recipe.outputs = std::move(a_outputs);
  return recipe;
}
}

int main() {
  const SlotContribution slot{SlotContributor{0}, 2};
  const LightContribution light{LightContributor{1}, 0};

  Check(slot == SlotContribution{SlotContributor{0}, 2},
        "a slot contribution compares by its two fields");
  Check(!(light == LightContribution{LightContributor{0}, 0}),
        "a light contribution distinguishes its source index");

  SlotPlan plan;
  plan.slot = Slot::kEmissive;
  plan.chain.push_back(slot);
  Check(plan.chain.size() == 1, "a slot plan holds a chain of contributions");
  Check(plan.scalars.empty(), "a fresh slot plan owns no scalars");

  LightPlan lights;
  lights.shown.push_back(light);
  Check(lights.shown.size() == 1, "a light plan holds the lights shown");

  {
    const Recipe recipeA =
        RecipeWith({Output{Emissive(false, false)}, Output{Diffuse()}});
    const Recipe recipeB = RecipeWith({Output{Emissive(false, false)}});
    const std::vector<PlacedRecipe> placed{
        PlacedRecipe{&recipeA, 10, {0, 1}},
        PlacedRecipe{&recipeB, 5, {0}},
    };
    const GeometryPlan geometry = PlanGeometry(placed);

    const SlotPlan *emissive =
        SlotPlanOf(geometry, Surface::kMaterial, Slot::kEmissive);
    const SlotPlan *diffuse =
        SlotPlanOf(geometry, Surface::kMaterial, Slot::kDiffuse);
    Check(emissive != nullptr,
          "both recipes on one geometry produce an emissive slot plan");
    Check(diffuse != nullptr,
          "the geometry keeps the second recipe's diffuse slot too");
    Check(SlotPlanOf(geometry, Surface::kMaterial, Slot::kNormal) == nullptr,
          "a slot no recipe wrote is absent");

    if (emissive) {
      Check(emissive->chain.size() == 2,
            "both emissive contributions compose, none dropped");
      Check(emissive->chain.front().placed == SlotContributor{1},
            "the lower-priority recipe leads the chain");
      Check(emissive->chain.back().placed == SlotContributor{0},
            "the higher-priority recipe ends the chain");
      Check(emissive->replacer == std::nullopt,
            "no replace leaves the chain uncut");
      Check(emissive->replaced.empty(), "no replace replaces nothing");
    }
  }

  {
    const Recipe low = RecipeWith({Output{Emissive(false, false)}});
    const Recipe mid = RecipeWith({Output{Emissive(true, false)}});
    const Recipe high = RecipeWith({Output{Emissive(false, false)}});
    const std::vector<PlacedRecipe> placed{
        PlacedRecipe{&low, 1, {0}},
        PlacedRecipe{&mid, 2, {0}},
        PlacedRecipe{&high, 3, {0}},
    };
    const GeometryPlan geometry = PlanGeometry(placed);
    const SlotPlan *emissive =
        SlotPlanOf(geometry, Surface::kMaterial, Slot::kEmissive);
    Check(emissive != nullptr,
          "the replace scenario builds an emissive slot plan");
    if (emissive) {
      Check(emissive->chain.size() == 2,
            "the replace cut keeps the replacer and everything above it");
      Check(emissive->chain.front().placed == SlotContributor{1},
            "the chain begins at the replacing contribution");
      Check(emissive->replaced.size() == 1,
            "everything below the replace is replaced");
      Check(emissive->replaced.front() ==
                SlotContribution{SlotContributor{0}, 0},
            "the lowest contribution is the one replaced");
      Check(emissive->replacer == SlotContribution{SlotContributor{1}, 0},
            "the replacer is the highest replace");

      const std::optional<std::size_t> ofReplaced =
          ReplacerOf(*emissive, SlotContribution{SlotContributor{0}, 0});
      const std::optional<std::size_t> ofSurvivor =
          ReplacerOf(*emissive, SlotContribution{SlotContributor{1}, 0});
      Check(ofReplaced == std::optional<std::size_t>{1},
            "a replaced contribution names the placement that replaced it");
      Check(ofSurvivor == std::nullopt,
            "a surviving contribution has no replacer");
    }
  }

  {
    Recipe low = RecipeWith({Output{Emissive(false, false)}});
    Recipe midReplaces = RecipeWith({Output{Emissive(false, false)}});
    midReplaces.mergeMode = MergeMode::kReplace;
    Recipe high = RecipeWith({Output{Emissive(false, false)}});
    const std::vector<PlacedRecipe> placed{
        PlacedRecipe{&low, 1, {0}},
        PlacedRecipe{&midReplaces, 2, {0}},
        PlacedRecipe{&high, 3, {0}},
    };
    const GeometryPlan geometry = PlanGeometry(placed);
    const SlotPlan *emissive =
        SlotPlanOf(geometry, Surface::kMaterial, Slot::kEmissive);
    Check(emissive != nullptr, "the recipe-level replace builds a slot plan");
    if (emissive) {
      Check(emissive->chain.size() == 2,
            "an override:replace recipe cuts the chain like an output replace");
      Check(emissive->replacer == SlotContribution{SlotContributor{1}, 0},
            "the override:replace recipe is the replacer");
      Check(emissive->replaced.size() == 1,
            "the lower-priority contribution is replaced");
    }
  }

  {
    const Recipe lower = RecipeWith({Output{Emissive(false, true)}});
    const Recipe upper = RecipeWith({Output{Emissive(false, true)}});
    const std::vector<PlacedRecipe> placed{
        PlacedRecipe{&lower, 5, {0}},
        PlacedRecipe{&upper, 10, {0}},
    };
    const GeometryPlan geometry = PlanGeometry(placed);
    const SlotPlan *emissive =
        SlotPlanOf(geometry, Surface::kMaterial, Slot::kEmissive);
    Check(emissive != nullptr, "two recipes naming strength build a slot plan");
    if (emissive) {
      Check(emissive->scalars.size() == 1,
            "exactly one owner records strength, no contention");
      const std::optional<SlotContribution> owner =
          ScalarOwnerOf(*emissive, ScalarField::kStrength);
      Check(owner == SlotContribution{SlotContributor{1}, 0},
            "the highest-priority namer owns the scalar");
      Check(ScalarOwnerOf(*emissive, ScalarField::kScale) == std::nullopt,
            "a field no output names has no owner");
    }
  }

  {
    const Recipe first = RecipeWith({Output{Diffuse()}, Output{Light(false)}});
    const Recipe second = RecipeWith({Output{Light(true)}});
    const std::vector<PlacedRecipe> placed{
        PlacedRecipe{&first, 3, {}},
        PlacedRecipe{&second, 7, {}},
    };
    const LightPlan lightPlan = PlanLights(placed);
    Check(lightPlan.shown.size() == 1, "the replacing light is all that shows");
    Check(!lightPlan.shown.empty() &&
              lightPlan.shown.front() ==
                  LightContribution{LightContributor{1}, 0},
          "the higher-priority light replaces the lower");
    Check(
        lightPlan.replaced.size() == 1 &&
            lightPlan.replaced.front() ==
                LightContribution{LightContributor{0}, 1},
        "the first recipe's light, found past its surface output, is replaced");
    Check(lightPlan.replacer == LightContribution{LightContributor{1}, 0},
          "the replacer is the higher-priority light");
    Check(ReplacerOf(lightPlan, LightContribution{LightContributor{0}, 1}) ==
              std::optional<std::size_t>{1},
          "the replaced light names its replacer's placement");
    Check(ReplacerOf(lightPlan, LightContribution{LightContributor{1}, 0}) ==
              std::nullopt,
          "the shown light has no replacer");
  }

  {
    const Recipe recipe = RecipeWith(
        {Output{Diffuse()}, Output{Light(false)}, Output{Light(true)}});
    const std::vector<PlacedRecipe> placed{PlacedRecipe{&recipe, 1, {}}};
    const LightPlan normal = PlanLights(placed);
    Check(normal.shown == std::vector<LightContribution>{LightContribution{
                              LightContributor{0}, 1}},
          "normal light planning retains its first-light policy");
    const LightPlan solo =
        PlanLights(placed, [](const Recipe &, std::size_t a_output) {
          return a_output == 2;
        });
    Check(solo.shown == std::vector<LightContribution>{LightContribution{
                            LightContributor{0}, 2}},
          "soloing a second light output selects the first visible light "
          "rather than hiding the recipe");
    Check(solo.replacer == LightContribution{LightContributor{0}, 2},
          "the selected second light participates in replacement planning");
    const LightPlan hidden =
        PlanLights(placed, [](const Recipe &, std::size_t a_output) {
          return a_output == 0;
        });
    Check(hidden.shown.empty(),
          "soloing a surface output excludes every light output");
    const LightPlan restored = PlanLights(placed);
    Check(restored.shown == normal.shown &&
              restored.replacer == normal.replacer,
          "unsolo restores the original first light output");
  }

  {
    const auto glob = [](const char *a_pattern) {
      return RecipeKey{KeyKind::kMaterial,
                       KeyOperandValue{std::string{a_pattern}}};
    };
    const auto sampled = [&](std::string a_id, const char *a_pattern) {
      Recipe recipe;
      recipe.id = std::move(a_id);
      recipe.mergeMode = MergeMode::kSampled;
      recipe.keys = {glob(a_pattern)};
      return recipe;
    };
    WornPiece piece;
    piece.diffusePaths = {"armor/iron.dds"};
    const Recipe a = sampled("a", "*iron*");
    const Recipe b = sampled("b", "armor/*");
    const Recipe c = sampled("c", "*.dds");
    const std::vector<Recipe> loaded{a, b, c};

    const std::vector<ResolvedRecipe> seed0 = Resolve(piece, loaded, 0);
    const std::vector<ResolvedRecipe> seed1 = Resolve(piece, loaded, 1);
    Check(seed0.size() == 1 && seed1.size() == 1,
          "a sampled pool of three collapses to one per actor");
    Check(seed0.front().recipe->id != seed1.front().recipe->id,
          "different actor seeds pick different pool members");
    Check(Resolve(piece, loaded, 0).front().recipe->id ==
              seed0.front().recipe->id,
          "the pick is stable per actor");

    Recipe plain;
    plain.id = "plain";
    plain.keys = {glob("*")};
    const std::vector<Recipe> mixed{a, b, plain};
    const std::vector<ResolvedRecipe> withPlain = Resolve(piece, mixed, 0);
    Check(withPlain.size() == 2,
          "a non-sampled recipe survives beside the one sampled pick");
    const bool keptPlain =
        std::ranges::any_of(withPlain, [](const ResolvedRecipe &r) {
          return r.recipe->id == "plain";
        });
    Check(keptPlain, "the surviving recipes include the non-sampled one");
  }

  {
    Recipe anyEnchanted;
    anyEnchanted.id = "anyEnchanted";
    anyEnchanted.keys = {RecipeKey{KeyKind::kEnchanted}};
    Recipe fallback;
    fallback.id = "fallback";
    fallback.keys = {RecipeKey{KeyKind::kDefault}};
    const std::vector<Recipe> loaded{anyEnchanted, fallback};

    WornPiece bare;
    const auto unenchanted = Resolve(bare, loaded, 0);
    Check(unenchanted.size() == 1 &&
              unenchanted.front().recipe->id == "fallback",
          "an enchanted key skips a piece without an enchantment");

    WornPiece enchanted;
    enchanted.enchantment = FormKey{"Skyrim.esm", 0x123};
    const auto generic = Resolve(enchanted, loaded, 0);
    Check(generic.size() == 1 && generic.front().recipe->id == "anyEnchanted",
          "an enchanted key matches an enchanted piece and suppresses default");

    Recipe specific;
    specific.id = "specific";
    specific.keys = {
        RecipeKey{KeyKind::kEnchantment,
                  KeyOperandValue{FormRef::From("0x123~Skyrim.esm")}}};
    enchanted.enchantment = *specific.keys.front().Form()->key;
    const std::vector<Recipe> withSpecific{anyEnchanted, fallback, specific};
    const auto resolved = Resolve(enchanted, withSpecific, 0);
    Check(resolved.size() == 1 && resolved.front().recipe->id == "specific",
          "a specific enchantment key suppresses the generic enchanted look");
  }

  {
    Selector selector;
    SelectorClause addon;
    addon.kind = SelectorKind::kAddon;
    addon.operand = FormRef::From("0x800~Test.esp");
    selector.anyOf.push_back(addon);
    GeometryIdentity geometry;
    geometry.name = "body01";
    Check(!Matches(selector, geometry),
          "an addon term skips a geometry without an addon");
    geometry.addon = FormKey{"Test.esp", 0x800};
    Check(Matches(selector, geometry),
          "an addon term matches the geometry's armor addon");
  }

  return test::Finish("merge");
}
