// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ActorPlanning.h"
#include "planners/BindingPlan.h"
#include "recipe/DefinitionOrder.h"
#include "studio/Selection.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
Recipe MakeRecipe(std::string a_id, MergeMode a_mode = MergeMode::kStack) {
  Recipe recipe;
  recipe.id = std::move(a_id);
  recipe.keys = {RecipeKey{KeyKind::kMaterial, std::string{"*"}}};
  recipe.mergeMode = a_mode;
  recipe.outputs = {SurfaceOutput{}};
  return recipe;
}

Geometry MakeGeometry(std::string a_name) {
  Geometry geometry;
  geometry.identity.name = std::move(a_name);
  geometry.keys.diffusePaths = {"armor.dds"};
  return geometry;
}

std::string SampledChoice(const WornPiece &a_piece,
                          std::span<const Recipe> a_recipes,
                          std::uint32_t a_actor) {
  for (const ResolvedRecipe &selected : Resolve(a_piece, a_recipes, a_actor)) {
    if (selected.recipe->mergeMode == MergeMode::kSampled) {
      return selected.recipe->id;
    }
  }
  return {};
}

void DefinitionPrecedence() {
  std::vector<Recipe> definitions;
  const auto identity = [](const Recipe &a_recipe) -> const std::string & {
    return a_recipe.id;
  };
  AppendDefinition(definitions, MakeRecipe("A"), identity);
  AppendDefinition(definitions, MakeRecipe("B"), identity);
  Recipe user = MakeRecipe("A", MergeMode::kReplace);
  user.priority = 7;
  AppendDefinition(definitions, user, identity);
  Check(definitions.size() == 2 && definitions[0].id == "B" &&
            definitions[1].id == "A" && definitions[1].priority == 7,
        "a later same-ID definition replaces the whole record at its new "
        "position");
  definitions[1].outputs.clear();
  Check(definitions[0].id == "B" && definitions[1].id == "A",
        "editing a definition leaves its precedence position intact");
}

void KeywordRequirements() {
  const RecipeKey heavy{KeyKind::kKeyword,
                        FormRef{"heavy", FormKey{"test.esp", 10}}};
  const RecipeKey cuirass{KeyKind::kKeyword,
                          FormRef{"cuirass", FormKey{"test.esp", 11}}};
  Recipe recipe = MakeRecipe("filtered");
  recipe.keys = {heavy, cuirass};
  std::array<Recipe, 1> recipes{recipe};
  WornPiece piece;
  Check(Resolve(piece, recipes).empty(),
        "keyword-only recipes reject no keywords");
  piece.keywords = {FormKey{"test.esp", 10}};
  Check(Resolve(piece, recipes).empty(),
        "one of two required keywords is insufficient");
  piece.keywords.push_back(FormKey{"test.esp", 11});
  auto selected = Resolve(piece, recipes);
  Check(selected.size() == 1 && selected[0].priority == 20 &&
            selected[0].key == heavy,
        "all required keywords select once with keyword priority and first-key "
        "identity");
  recipes[0].keys.push_back(heavy);
  Check(Resolve(piece, recipes).size() == 1,
        "repeated keyword requirements do not require duplicate keywords on "
        "the item");
  recipes[0].keys.push_back(
      {KeyKind::kArmor, FormRef{"armor", FormKey{"test.esp", 12}}});
  Check(Resolve(piece, recipes).empty(),
        "keywords alone cannot satisfy a recipe that also has selection keys");
  piece.armor = FormKey{"test.esp", 12};
  selected = Resolve(piece, recipes);
  Check(
      selected.size() == 1 && selected[0].priority == 30 &&
          selected[0].key.kind == KeyKind::kArmor,
      "a matching armor key selects after every keyword requirement succeeds");
  piece.keywords.pop_back();
  Check(Resolve(piece, recipes).empty(),
        "an explicit armor match cannot bypass a missing keyword");
  piece.keywords.push_back(FormKey{"test.esp", 11});
  recipes[0].keys.push_back(
      {KeyKind::kEnchantment, FormRef{"enchantment", FormKey{"test.esp", 13}}});
  piece.armor.reset();
  piece.enchantment = FormKey{"test.esp", 13};
  Check(Resolve(piece, recipes).size() == 1,
        "non-keyword selection keys remain alternatives after the keyword "
        "filter");
  recipes[0].keys = {heavy, {KeyKind::kMaterial, std::string{"*"}}, cuirass};
  piece.diffusePaths = {"armor.dds"};
  selected = Resolve(piece, recipes);
  Check(selected.size() == 1 && selected[0].priority == 10 &&
            selected[0].key.kind == KeyKind::kMaterial,
        "required keywords do not promote a material selector's priority");
  recipes[0].priority = 100;
  selected = Resolve(piece, recipes);
  Check(selected.size() == 1 && selected[0].priority == 100,
        "explicit recipe priority still overrides the matched selector's "
        "default");
  recipes[0].keys.push_back({KeyKind::kKeyword, FormRef{"unresolved", {}}});
  Check(Resolve(piece, recipes).empty(),
        "an unresolved required keyword fails closed");
  recipes[0].keys.clear();
  Check(Resolve(piece, recipes).empty(),
        "an empty key list never becomes a vacuous match");
}

void MagicEffectMatching() {
  WornPiece piece;
  piece.enchantment = FormKey{"test.esp", 1};
  piece.magicEffects = {{"test.esp", 2}, {"test.esp", 3}};
  Recipe secondary = MakeRecipe("secondary");
  secondary.keys = {{KeyKind::kMagicEffect,
                     FormRef{"secondary effect", FormKey{"test.esp", 3}}}};
  Recipe fallback = MakeRecipe("default");
  fallback.keys = {RecipeKey{}};
  Recipe enchanted = MakeRecipe("enchanted");
  enchanted.keys = {{KeyKind::kEnchanted, {}}};
  std::array recipes{fallback, enchanted, secondary};
  std::vector<RecipeSelection> outcomes;
  auto selected = Resolve(piece, recipes, 0, &outcomes);
  Check(selected.size() == 1 && selected[0].recipe->id == "secondary" &&
            outcomes[0].outcome == SelectionOutcome::kFallbackSuppressed &&
            outcomes[1].outcome == SelectionOutcome::kFallbackSuppressed,
        "a secondary magic effect matches and suppresses generic fallbacks");
  recipes[2].keys.push_back(
      {KeyKind::kMagicEffect, FormRef{"first effect", FormKey{"test.esp", 2}}});
  Check(Resolve(piece, recipes).size() == 1,
        "multiple matching effects select the recipe once");
  recipes[2].keys.push_back(
      {KeyKind::kKeyword, FormRef{"required", FormKey{"test.esp", 4}}});
  selected = Resolve(piece, recipes, 0, &outcomes);
  Check(selected.size() == 1 && selected[0].recipe->id == "enchanted" &&
            outcomes[2].outcome == SelectionOutcome::kNonmatching,
        "a magic-effect match with missing keywords cannot suppress a valid "
        "fallback");
  piece.keywords = {{"test.esp", 4}};
  selected = Resolve(piece, recipes);
  Check(selected.size() == 1 && selected[0].recipe->id == "secondary",
        "the keyword-filtered magic-effect recipe becomes eligible when all "
        "keywords exist");
  piece.magicEffects.clear();
  selected = Resolve(piece, recipes);
  Check(selected.size() == 1 && selected[0].recipe->id == "enchanted",
        "an enchantment with no matching effects keeps its generic fallback");
}

void KeyContractRoundTrip() {
  const auto parsed = ParseRecipe(R"({"format":1,"keys":[
    {"keyword":"0xA~test.esp"},{"keyword":"0xB~test.esp"},
    {"magicEffect":"0xC~test.esp"},{"armor":"0xD~test.esp"}
  ]})",
                                  "mixed");
  Check(parsed.recipe.has_value(),
        "mixed keyword requirements use the existing wire format");
  if (!parsed.recipe)
    return;
  const auto restored = ParseRecipe(SerializeRecipe(*parsed.recipe), "mixed");
  Check(restored.recipe.has_value(),
        "mixed requirements survive serialization");
  if (!restored.recipe)
    return;
  const std::array recipes{*restored.recipe};
  WornPiece piece;
  piece.magicEffects = {{"test.esp", 99}, {"test.esp", 12}};
  piece.keywords = {{"test.esp", 10}, {"test.esp", 11}};
  const auto selected = Resolve(piece, recipes);
  Check(selected.size() == 1 && selected[0].key.kind == KeyKind::kMagicEffect,
        "a round-tripped recipe requires both keywords and accepts a secondary "
        "effect");
  piece.keywords.pop_back();
  Check(Resolve(piece, recipes).empty(),
        "round-trip does not weaken keyword requirements");
  Recipe eligible = MakeRecipe("eligible", MergeMode::kSampled);
  eligible.keys = {{KeyKind::kKeyword, FormRef::From("0xA~test.esp")}};
  Recipe ineligible = eligible;
  ineligible.id = "ineligible";
  ineligible.keys.push_back({KeyKind::kKeyword, FormRef::From("0xB~test.esp")});
  const std::array pool{eligible, ineligible};
  Check(SampledChoice(piece, pool, 0x14) == "eligible" &&
            SampledChoice(piece, pool, 0x12345678) == "eligible",
        "missing keyword requirements exclude a candidate before sampling");
}

void SamplingAndFallback() {
  Check(SamplingHash(0) == 0x4b95f515u && SamplingHash(1) == 0xfb69b604u &&
            SamplingHash(0x14) == 0x0da8e9e1u &&
            SamplingHash(0x12345678) == 0xa3649785u &&
            SamplingHash(0xffffffff) == 0xe3160fb1u,
        "FNV-1a over four little-endian actor bytes has fixed golden vectors");
  const WornPiece piece = MakeGeometry("gloves").keys;
  std::vector<Recipe> recipes{MakeRecipe("A", MergeMode::kSampled),
                              MakeRecipe("B", MergeMode::kSampled),
                              MakeRecipe("C")};
  Check(Resolve(piece, recipes, 0x14).size() == 2,
        "same-key sampled alternatives coexist with an ordinary recipe");
  const std::string chosen = SampledChoice(piece, recipes, 0x14);
  Check(chosen == "B", "canonical A/B pool selects golden actor's B");
  std::ranges::reverse(recipes);
  recipes[0].priority = 999;
  recipes[1].priority = -200;
  recipes[2].priority = 500;
  recipes[1].outputs.clear();
  Check(SampledChoice(piece, recipes, 0x14) == chosen,
        "load order, priority and output changes do not reroll sampling");
  recipes[2].keys.push_back(recipes[2].keys.front());
  Check(SampledChoice(piece, recipes, 0x14) == chosen,
        "duplicate matching keys do not add sampling weight");
  for (Recipe &recipe : recipes) {
    recipe.mergeMode = MergeMode::kStack;
  }
  Check(Resolve(piece, recipes).size() == 3,
        "different stack identities sharing a key all participate");

  const std::array utf8{MakeRecipe("\xc3\xa9", MergeMode::kSampled),
                        MakeRecipe("a", MergeMode::kSampled),
                        MakeRecipe("Z", MergeMode::kSampled)};
  Check(SampledChoice(piece, utf8, 0) == "a",
        "canonical identity order is case-sensitive UTF-8 byte order");

  Recipe fallback = MakeRecipe("fallback");
  fallback.keys = {RecipeKey{}};
  Recipe enchanted = MakeRecipe("enchanted");
  enchanted.keys = {RecipeKey{KeyKind::kEnchanted, {}}};
  Recipe specific = MakeRecipe("specific", MergeMode::kSampled);
  specific.keys = {RecipeKey{KeyKind::kEnchantment,
                             FormRef{"enchantment", FormKey{"test.esp", 1}}}};
  Recipe other = specific;
  other.id = "other";
  Recipe nonmatching = MakeRecipe("nonmatching");
  nonmatching.keys = {RecipeKey{KeyKind::kMaterial, std::string{"missing"}}};
  WornPiece worn = piece;
  worn.enchantment = FormKey{"test.esp", 1};
  const std::vector<Recipe> pool{fallback, enchanted, specific, other,
                                 nonmatching};
  std::vector<RecipeSelection> selections;
  Check(Resolve(worn, pool, 0x14, &selections).size() == 1,
        "fallback suppression precedes the sampled choice");
  Check(selections.size() == 5 &&
            selections[0].outcome == SelectionOutcome::kFallbackSuppressed &&
            selections[1].outcome == SelectionOutcome::kFallbackSuppressed &&
            selections[4].outcome == SelectionOutcome::kNonmatching &&
            std::ranges::count(selections, SelectionOutcome::kSampledOut,
                               &RecipeSelection::outcome) == 1,
        "selection reports distinguish fallback, nonmatching and sampled-out "
        "recipes");
  specific.keys.push_back(RecipeKey{KeyKind::kMagicEffect,
                                    FormRef{"effect", FormKey{"test.esp", 2}}});
  specific.priority = -100;
  worn.magicEffects = {FormKey{"test.esp", 2}};
  const std::array<Recipe, 1> one{specific};
  const auto resolved = Resolve(worn, one);
  Check(resolved.size() == 1 && resolved[0].key.kind == KeyKind::kMagicEffect &&
            resolved[0].priority == -100,
        "strongest key is reported independently of explicit composition "
        "priority");
}

void PlacementPriorityAndLights() {
  Recipe a = MakeRecipe("A");
  a.keys.push_back(RecipeKey{KeyKind::kMagicEffect,
                             FormRef{"effect", FormKey{"test.esp", 2}}});
  Recipe b = MakeRecipe("B");
  b.priority = 30;
  a.outputs.push_back(LightOutput{});
  b.outputs.push_back(LightOutput{});
  a.mergeMode = MergeMode::kReplace;
  std::vector<Recipe> recipes{a, b};
  Geometry gloves = MakeGeometry("gloves");
  Geometry boots = MakeGeometry("boots");
  boots.keys.magicEffects = {FormKey{"test.esp", 2}};
  const std::array geometries{gloves, boots};
  ActorPlan actor = MatchActor(geometries, recipes);
  Check(actor.instances.size() == 3,
        "effect-selected placements have independent evaluation instances");
  const auto glovePlan = PlanGeometryPlacement(actor, recipes, GeometryId{0});
  const auto bootPlan = PlanGeometryPlacement(actor, recipes, GeometryId{1});
  Check(glovePlan.placed.size() == 2 && glovePlan.placed[0].priority == 10 &&
            glovePlan.placed[1].priority == 30 && bootPlan.placed.size() == 2 &&
            bootPlan.placed[1].priority == 60,
        "shared instances never promote the lower-priority surface placement");
  const auto matches = MatchesForPiece(actor, GeometryId{0}, 1);
  Check(matches.size() == 2 && matches[0].priority == 10,
        "piece diagnostics carry placement priority");
  auto lights = PlanActorLights(actor, recipes);
  Check(
      lights.plan.shown.size() == 2 && lights.plan.replaced.size() == 1 &&
          lights.placed[IndexOf(lights.plan.shown[0].placed)].recipe->id == "A",
      "light groups aggregate priority independently and honor recipe replace");
  Get<LightOutput>(recipes[0].outputs[1])->selector.anyOf = {
      SelectorClause{SelectorKind::kGeometry, std::string{"gloves"}}};
  lights = PlanActorLights(actor, recipes);
  Check(lights.plan.shown.size() == 2 && lights.plan.replaced.empty(),
        "light priority considers only eligible selected placements");
  Get<LightOutput>(recipes[0].outputs[1])->selector.anyOf = {
      SelectorClause{SelectorKind::kGeometry, std::string{"missing"}}};
  lights = PlanActorLights(actor, recipes);
  Check(lights.plan.shown.size() == 1 && lights.plan.replaced.empty(),
        "a replacing light with no selector match cannot clear another light");
  Get<LightOutput>(recipes[0].outputs[1])->selector.anyOf.clear();
  actor.geometries[1].firstPerson = true;
  actor.geometries[0].lost = true;
  Check(
      PlanActorLights(actor, recipes).plan.shown.empty(),
      "lost or first-person placements cannot enter actor-wide light planning");
}

void EffectInstanceIdentity() {
  const RecipeKey first{KeyKind::kMagicEffect,
                        FormRef{"first", FormKey{"test.esp", 1}}};
  const RecipeKey second{KeyKind::kMagicEffect,
                         FormRef{"second", FormKey{"test.esp", 2}}};
  Recipe recipe = MakeRecipe("scoped");
  recipe.keys = {first, second};
  const std::array recipes{recipe};
  Geometry a = MakeGeometry("first");
  a.keys.enchantment = FormKey{"test.esp", 3};
  a.keys.magicEffects = {FormKey{"test.esp", 1}};
  Geometry b = a;
  b.keys.magicEffects = {FormKey{"test.esp", 2}};
  const std::array geometries{a, b, a};
  const auto plan = MatchActor(geometries, recipes);
  Check(plan.instances.size() == 2 && plan.placements.size() == 3,
        "one enchantment can have distinct selected-effect instances");
  if (plan.instances.size() != 2 || plan.placements.size() != 3)
    return;
  Check(plan.instances[0].effectKey == first &&
            plan.instances[1].effectKey == second,
        "winning effect keys reach the evaluation plan");
  Check(plan.placements[0].instance == plan.placements[2].instance &&
            plan.placements[0].instance != plan.placements[1].instance,
        "matching effect contexts share state while different contexts remain "
        "isolated");
}

void AddonSelectedLights() {
  const FormKey glovesAddon{"armor.esp", 0x801};
  const FormKey bootsAddon{"armor.esp", 0x802};
  LightOutput light;
  light.selector.anyOf = {
      SelectorClause{SelectorKind::kAddon, FormRef{"gloves", glovesAddon}}};
  Recipe selected = MakeRecipe("selected", MergeMode::kReplace);
  selected.priority = 20;
  selected.outputs = {light};
  Recipe base = MakeRecipe("base");
  base.outputs = {LightOutput{}};
  const std::vector<Recipe> recipes{base, selected};
  Geometry gloves = MakeGeometry("shared mesh name");
  gloves.identity.addon = glovesAddon;
  Geometry boots = gloves;
  boots.identity.addon = bootsAddon;
  Geometry unknown = gloves;
  unknown.identity.addon.reset();
  Check(LightEligible(gloves, light) && !LightEligible(boots, light) &&
            !LightEligible(unknown, light),
        "addon selectors distinguish matching, different and absent addons "
        "even when geometry names and textures agree");
  Geometry otherPlugin = gloves;
  otherPlugin.identity.addon = FormKey{"other.esp", 0x801};
  Check(!LightEligible(otherPlugin, light),
        "addon identity includes the plugin as well as the local form ID");
  const std::array matching{gloves, boots};
  auto plan = PlanActorLights(MatchActor(matching, recipes), recipes);
  Check(plan.plan.shown.size() == 1 && plan.plan.replaced.size() == 1 &&
            plan.placed[IndexOf(plan.plan.shown[0].placed)].recipe->id ==
                "selected",
        "a matching addon light participates and replaces the earlier group");
  const std::array nonmatching{boots, unknown};
  plan = PlanActorLights(MatchActor(nonmatching, recipes), recipes);
  Check(plan.plan.shown.size() == 1 && plan.plan.replaced.empty() &&
            plan.placed[IndexOf(plan.plan.shown[0].placed)].recipe->id ==
                "base",
        "different or absent addons cannot let a replacing light suppress "
        "the eligible base light");
  gloves.firstPerson = true;
  Check(!LightEligible(gloves, light),
        "a matching addon does not bypass third-person light eligibility");
  gloves.firstPerson = false;
  gloves.lost = true;
  Check(!LightEligible(gloves, light),
        "a matching addon does not bypass lost-geometry exclusion");
}

void GroupedReplacement() {
  Recipe a = MakeRecipe("A");
  SurfaceOutput emissive;
  emissive.slot = Slot::kEmissive;
  emissive.scalars.color = Vec3Param{std::array<Param, 3>{1.0f, 0.0f, 0.0f}};
  SurfaceOutput diffuse;
  diffuse.slot = Slot::kDiffuse;
  a.outputs = {emissive, diffuse};
  Recipe b = MakeRecipe("B", MergeMode::kReplace);
  b.outputs = {emissive, emissive};
  Get<SurfaceOutput>(b.outputs[1])->scalars.strength = 3.0f;
  Recipe c = MakeRecipe("C");
  c.outputs = {emissive};
  const std::vector<PlacedRecipe> placed{
      {&a, 1, {0, 1}, 0}, {&b, 2, {0, 1}, 1}, {&c, 3, {0}, 2}};
  for (bool outputReplace : {false, true}) {
    b.mergeMode = outputReplace ? MergeMode::kStack : MergeMode::kReplace;
    Get<SurfaceOutput>(b.outputs[1])->replace = outputReplace;
    const auto plan = PlanGeometry(placed);
    const auto *slot = SlotPlanOf(plan, Surface::kMaterial, Slot::kEmissive);
    const auto *other = SlotPlanOf(plan, Surface::kMaterial, Slot::kDiffuse);
    Check(slot && slot->chain.size() == 3 && slot->replaced.size() == 1 &&
              slot->chain[0].output == 0 && slot->chain[1].output == 1 &&
              other && other->chain.size() == 1,
          "recipe and output replacement preserve siblings, later groups and "
          "other slots");
    Check(slot && ScalarOwnerOf(*slot, ScalarField::kStrength) ==
                      SlotContribution{SlotContributor{1}, 1},
          "later sibling scalar survives when higher groups omit it");
  }
  SurfaceOutput fuzz;
  fuzz.slot = Slot::kFuzz;
  fuzz.scalars.color = std::array<Param, 3>{1.0f, 0.0f, 0.0f};
  fuzz.scalars.weight = 0.25f;
  a.outputs = {fuzz};
  fuzz.scalars.color.reset();
  fuzz.scalars.weight = 0.75f;
  b.outputs = {fuzz};
  b.mergeMode = MergeMode::kStack;
  const std::vector<PlacedRecipe> scalars{{&a, 1, {0}, 0}, {&b, 2, {0}, 1}};
  const auto scalarPlan = PlanGeometry(scalars);
  const auto *fuzzPlan =
      SlotPlanOf(scalarPlan, Surface::kMaterial, Slot::kFuzz);
  Check(fuzzPlan &&
            ScalarOwnerOf(*fuzzPlan, ScalarField::kColor) ==
                SlotContribution{SlotContributor{0}, 0} &&
            ScalarOwnerOf(*fuzzPlan, ScalarField::kWeight) ==
                SlotContribution{SlotContributor{1}, 0},
        "omitted properties preserve earlier surviving owners independently");

  a.outputs = {LightOutput{}};
  b.outputs = {LightOutput{}};
  b.mergeMode = MergeMode::kReplace;
  const std::vector<PlacedRecipe> lightRows{
      {&b, 5, {}, 1}, {&a, 20, {}, 0}, {&b, 30, {}, 1}};
  const auto lights = PlanLights(lightRows);
  Check(lights.shown.size() == 2 && lights.replaced.size() == 1 &&
            lights.shown[0].placed == LightContributor{0} &&
            lights.shown[1].placed == LightContributor{2},
        "actor-wide recipe replacement retains both enchantment instances in "
        "its group");
}

void NoRerollAfterSelectorExclusion() {
  Recipe a = MakeRecipe("A", MergeMode::kSampled);
  Recipe b = MakeRecipe("B", MergeMode::kSampled);
  Get<SurfaceOutput>(b.outputs[0])->selector.anyOf = {
      SelectorClause{SelectorKind::kGeometry, std::string{"missing"}}};
  const std::array recipes{a, b};
  const std::array geometries{MakeGeometry("gloves")};
  const ActorPlan actor = MatchActor(
      geometries, recipes, [&](const Geometry &a_geometry, GeometryId) {
        return Resolve(a_geometry.keys, recipes, 0x14);
      });
  const auto plan = PlanGeometryPlacement(actor, recipes, GeometryId{0});
  Check(actor.instances.size() == 1 &&
            actor.instances[0].recipe == RecipeId{1} &&
            actor.placements.size() == 1 &&
            !actor.placements[0].outputs[0].selected && plan.plan.slots.empty(),
        "sampled selector exclusion reports failure without rerolling to A");
  Get<SurfaceOutput>(a.outputs[0])->replace = true;
  const auto absent =
      PlanGeometryPlacement(actor, recipes, GeometryId{0},
                            [](const Recipe &, std::size_t) { return false; });
  Check(absent.plan.slots.empty(),
        "filtering all selected outputs never revives another recipe");
}

void ShellOrderAndPreview() {
  Recipe a = MakeRecipe("A");
  Recipe b = MakeRecipe("B");
  SurfaceOutput shell;
  shell.surface = Surface::kShell;
  shell.slot = Slot::kDiffuse;
  a.outputs = {shell};
  shell.slot = Slot::kEmissive;
  b.outputs = {shell};
  const std::vector<PlacedRecipe> rows{{&b, 7, {0}, 1}, {&a, 7, {0}, 0}};
  auto plan = PlanGeometry(rows);
  Check(PlanBinding(rows, plan).shellOwner == SlotContributor{0},
        "equal-priority shell settings use definition order across slots");
  std::ranges::reverse(plan.slots);
  Check(PlanBinding(rows, plan).shellOwner == SlotContributor{0},
        "slot enumeration cannot change shell ownership");
  a.mergeMode = MergeMode::kSampled;
  b.mergeMode = MergeMode::kSampled;
  a.priority = 0;
  const std::array recipes{a, b};
  const WornPiece piece = MakeGeometry("gloves").keys;
  const auto selected = Resolve(piece, recipes, 0x14);
  Studio::View view;
  const Studio::PieceRef ref{0x14, 1, 0};
  const auto normal =
      Studio::ViewedRecipes({selected, piece, ref, view, recipes});
  Check(normal.size() == 1 && normal[0].recipe->id == "B",
        "unisolated preview preserves the actual actor's sampled selection");
  view.isolation = Studio::Isolation::ForRecipe("A");
  const auto isolated =
      Studio::ViewedRecipes({selected, piece, ref, view, recipes});
  Check(isolated.size() == 1 && isolated[0].recipe->id == "A",
        "explicit isolation can preview a sampled-out alternative");
  Check(isolated.size() == 1 && isolated[0].loadOrder == 0,
        "isolation preserves the selected definition's original load position");
  view.isolation = Studio::Isolation::ForRecipe("B");
  const auto later =
      Studio::ViewedRecipes({selected, piece, ref, view, recipes});
  Check(later.size() == 1 && later[0].loadOrder == 1,
        "isolating a later definition retains its real load position");
  view.isolation = {};
  const auto restored =
      Studio::ViewedRecipes({selected, piece, ref, view, recipes});
  Check(restored.size() == 1 && restored[0].recipe->id == "B",
        "leaving isolation restores the actor's sampled choice");
  WornPiece pinPiece = piece;
  pinPiece.armor = FormKey{"test.esp", 1};
  view.pin = Studio::Pin{ref, "A"};
  const auto pinned =
      Studio::ViewedRecipes({selected, pinPiece, ref, view, recipes});
  Check(pinned.size() == 2 && pinned[0].recipe->id == "A" &&
            pinned[1].recipe->id == "B" && pinned[0].priority == 0 &&
            pinned[0].loadOrder == 0,
        "explicit sampled-out pin composes at its preview placement priority");
}
}

int main() {
  DefinitionPrecedence();
  KeywordRequirements();
  MagicEffectMatching();
  KeyContractRoundTrip();
  SamplingAndFallback();
  PlacementPriorityAndLights();
  EffectInstanceIdentity();
  AddonSelectedLights();
  GroupedReplacement();
  ShellOrderAndPreview();
  NoRerollAfterSelectorExclusion();
  return test::Finish("resolution contract");
}
