#include "studio/RecipeContent.h"
#include "test_support.h"

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
  recipe.outputs = {Output{surface}, Output{LightOutput{}}};
  return recipe;
}

RecipeContentInput BaseInput(const Recipe &a_recipe,
                             const ReferenceCounts &a_refs) {
  return RecipeContentInput{a_recipe, RecipeKey{}, 3,       0.5f, std::nullopt,
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

  RecipeContentInput lean = BaseInput(recipe, refs);
  lean.priority = 7;
  const RecipeRow basic = RecipeContent(lean);
  Check(basic.id == "test", "RecipeContent copies the recipe id");
  Check(basic.priority == 7, "RecipeContent copies the match priority");
  Check(Near(basic.clockSpeed, 2.0f), "RecipeContent copies the clock speed");
  Check(basic.undoDepth == 2 && basic.redoDepth == 1,
        "RecipeContent passes through the history depths");
  Check(basic.signals.empty() && basic.masks.empty() &&
            basic.sourceRows.empty() && basic.curves.empty(),
        "a lean (not full) row carries no detail rows");

  RecipeContentInput detailed = BaseInput(recipe, refs);
  detailed.full = true;
  detailed.graph = &graph;
  detailed.signals = &state;
  const RecipeRow full = RecipeContent(detailed);
  Check(full.signals.size() == recipe.signals.size(),
        "a full row has one signal row per signal");
  Check(!full.signals.empty() && full.signals[0].name == "glow" &&
            full.signals[0].value == state.ValueOf("glow"),
        "RecipeContent overlays the live signal value");
  Check(full.masks.size() == 1 && full.masks[0] == "edge",
        "a full row lists the mask names");
  Check(full.maskRows.size() == 1, "a full row has one mask row per mask");
  Check(full.sourceRows.size() == recipe.sources.size(),
        "a full row has one source row per source");
  Check(full.curves.size() == recipe.curves.size(),
        "a full row has one curve row per curve");

  Diagnostic diag{Severity::kError, "output 0", "bad"};
  RecipeContentInput withProblem = BaseInput(recipe, refs);
  withProblem.full = true;
  const std::array<Diagnostic, 1> problems{diag};
  withProblem.problems = problems;
  const RecipeRow flagged = RecipeContent(withProblem);
  Check(flagged.problems.size() == 1 && flagged.problems[0].message == "bad",
        "RecipeContent carries the supplied problems");

  return test::Finish("studio recipecontent");
}
