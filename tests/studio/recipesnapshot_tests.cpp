// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/RecipeSnapshot.h"
#include "test_support.h"

#include <algorithm>
#include <array>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;
using test::Near;

namespace {
Recipe MakeRecipe() {
  Recipe recipe;
  recipe.id = "test";
  recipe.clock.speed = 2.0f;
  recipe.signals = {
      Signal{"glow", ConstantSignal{2.0f}, std::nullopt},
      Signal{"scaled", ExprSignal{"glow * 2"}, std::nullopt},
  };
  recipe.curves = {Curve{"ramp", "0,0 1,1"}};
  recipe.masks = {Mask{"edge", "@glow"}};
  recipe.sources = {Source{"tex", MaterialSource{}}};

  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kFuzz;
  surface.replace = true;
  surface.selector.anyOf.push_back(
      {SelectorKind::kGeometry, std::string{"ExcludedGeometry*"}});
  LightOutput light;
  light.replace = true;
  light.selector.anyOf.push_back(
      {SelectorKind::kAddon, FormRef::From("ArmorAddon")});
  recipe.outputs = {Output{surface}, Output{light}};
  return recipe;
}

RecipeRowInput BaseInput(const Recipe &a_recipe,
                         const ReferenceCounts &a_refs) {
  return RecipeRowInput{a_recipe, RecipeKey{}, 3,       0.5f, std::nullopt,
                        false,    false,       false,   2,    1,
                        a_refs,   nullptr,     nullptr, {}};
}
}

int main() {
  const Recipe recipe = MakeRecipe();
  const SignalGraph graph = SignalGraph::Compile(recipe.signals, recipe.curves);
  SignalState state{graph};
  NullEnvironment environment;
  state.Tick(environment, TickInputs{0.0f, 0.0f});
  const ReferenceCounts refs;

  RecipeRowInput lean = BaseInput(recipe, refs);
  lean.priority = 7;
  const RecipeRow basic = BuildRecipeRow(lean);
  Check(basic.id == "test", "BuildRecipeRow copies the recipe id");
  Check(basic.priority == 7, "BuildRecipeRow copies the match priority");
  Check(Near(basic.clockSpeed, 2.0f), "BuildRecipeRow copies the clock speed");
  Check(basic.undoDepth == 2 && basic.redoDepth == 1,
        "BuildRecipeRow passes through the history depths");
  Check(basic.signals.empty() && basic.masks.empty() &&
            basic.sourceRows.empty() && basic.curves.empty() &&
            basic.outputs.empty(),
        "a lean (not full) row carries no detail rows");

  RecipeRowInput detailed = BaseInput(recipe, refs);
  detailed.full = true;
  detailed.graph = &graph;
  detailed.signals = &state;
  const RecipeRow full = BuildRecipeRow(detailed);
  Check(full.geometries.empty() && full.outputs.size() == recipe.outputs.size(),
        "full recipe rows retain all output definitions without geometry");
  if (full.outputs.size() == 2) {
    const SurfaceOutput *surface = Get<SurfaceOutput>(recipe.outputs[0]);
    const LightOutput *light = Get<LightOutput>(recipe.outputs[1]);
    Check(surface && full.outputs[0].index == 0 && full.outputs[0].replace &&
              full.outputs[0].selection == surface->selector,
          "excluded surface output retains its editable selector and replace");
    Check(light && full.outputs[1].target == Target::kLight &&
              full.outputs[1].index == 1 && full.outputs[1].replace &&
              full.outputs[1].selection == light->selector,
          "light output retains its editable selector and replace");
  }
  Check(full.signals.size() == recipe.signals.size(),
        "a full row has one signal row per signal");
  Check(!full.signals.empty() && full.signals[0].name == "glow" &&
            full.signals[0].value == state.ValueOf("glow") &&
            full.signals[0].live,
        "BuildRecipeRow overlays the live signal value");
  Check(full.masks.size() == 1 && full.masks[0] == "edge",
        "a full row lists the mask names");
  Check(full.maskRows.size() == 1, "a full row has one mask row per mask");
  Check(full.sourceRows.size() == recipe.sources.size(),
        "a full row has one source row per source");
  Check(full.curves.size() == recipe.curves.size(),
        "a full row has one curve row per curve");

  Diagnostic diag{Severity::kError, "output 0", "bad"};
  RecipeRowInput withProblem = BaseInput(recipe, refs);
  withProblem.full = true;
  const std::array<Diagnostic, 1> problems{diag};
  withProblem.problems = problems;
  const RecipeRow flagged = BuildRecipeRow(withProblem);
  Check(flagged.problems.size() == 1 && flagged.problems[0].message == "bad",
        "BuildRecipeRow carries the supplied problems");

  {
    RecipeRowInput document = BaseInput(recipe, refs);
    document.full = true;
    document.graph = &graph;
    const RecipeRow projected = BuildRecipeRow(document);
    Check(projected.signals.size() == recipe.signals.size() &&
              projected.signals.front().definition ==
                  recipe.signals.front().kind &&
              projected.signals.front().constant == Value{2.0f},
          "document projection retains editable signal definitions without "
          "actor state");
    Check(std::ranges::none_of(projected.signals,
                               [](const SignalRow &row) { return row.live; }) &&
              projected.geometries.empty(),
          "document-only projection never claims live values or geometry");
    document.graph = nullptr;
    const RecipeRow uncompiled = BuildRecipeRow(document);
    Check(uncompiled.signals.size() == recipe.signals.size() &&
              uncompiled.signals.back().inert &&
              !uncompiled.signals.back().problem.empty(),
          "missing cached graph still exposes signal definitions and compile "
          "errors");
  }
  {
    Recipe multiple = recipe;
    LightOutput extra;
    extra.intensity = 17.0f;
    multiple.outputs.push_back(extra);
    RecipeRowInput input = BaseInput(multiple, refs);
    input.full = true;
    const RecipeRow projected = BuildRecipeRow(input);
    Check(projected.lights.size() == 2 &&
              projected.lights.front().output == 1 &&
              projected.lights.back().output == 2 &&
              projected.lights.back().intensity == "17",
          "every authored light gets independent editable properties and its "
          "original output index");
  }
  return test::Finish("studio recipesnapshot");
}
