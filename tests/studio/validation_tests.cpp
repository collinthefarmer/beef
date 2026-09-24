// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/Gesture.h"
#include "test_support.h"

#include <format>
#include <limits>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
void Refused(const Recipe &a_recipe, const RecipeEdit &a_edit,
             std::string_view a_label) {
  Recipe copy = a_recipe;
  const auto problem = Apply(copy, a_edit);
  Check(problem && problem->severity == Severity::kError &&
            !problem->where.empty() && !problem->message.empty(),
        std::format("{} returns a located UI diagnostic", a_label));
  Check(copy == a_recipe, std::format("{} preserves the document", a_label));
}
}

int main() {
  Recipe recipe;
  recipe.id = "validation";
  recipe.keys.push_back(RecipeKey{});
  recipe.signals.push_back(Signal{"drive", ConstantSignal{1.0f}, {}, {}});
  recipe.sources.push_back(
      Source{"image", ImageSource{"textures/test.dds"}, {}});
  recipe.outputs.push_back(LightOutput{});
  const float infinity = std::numeric_limits<float>::infinity();
  const auto gesture = BeginRecipeGesture(recipe, 1, 1, "clock.speed");
  Check(gesture.has_value(), "numeric gesture starts");
  if (gesture) {
    const auto update = PrepareGestureUpdate(
        *gesture, {recipe, 1}, "clock.speed", {{SetClockSpeed{infinity}}});
    Check(!update && update.error().contains("finite") &&
              gesture->applied == recipe,
          "gesture refuses nonfinite update with a useful UI error");
  }
  Refused(recipe, SetClockSpeed{infinity}, "nonfinite clock");
  Refused(recipe, SetConstant{"drive", Vec3{0.0f, infinity, 0.0f}},
          "nonfinite constant");
  Refused(recipe,
          SetSignal{"drive",
                    NoiseSignal{1.0f, 1.0f,
                                std::numeric_limits<std::uint32_t>::max()}},
          "oversized seed");
  ImageSource image{"textures/test.dds"};
  image.mip = -1.0f;
  Refused(recipe, SetSource{"image", image}, "negative mip");
  MaterialClustersSource clusters;
  clusters.settings.iterations = kMaxClusterIterations + 1;
  Refused(recipe, SetSource{"image", clusters}, "excessive cluster iterations");
  Refused(recipe, SetShellAlphaTest{2.0f}, "alpha test out of range");
  Refused(recipe, SetShellAlphaTest{std::numeric_limits<float>::quiet_NaN()},
          "nonfinite alpha test");
  Refused(recipe, SetShellPoint{ShellPoint::kScalePoint, Vec3{infinity, 0, 0}},
          "nonfinite shell point");
  Refused(recipe, SetLightBones{0, SkinnedBones{2, 0.1f}},
          "invalid bone share");
  Refused(recipe, SetLightBones{0, NamedBones{{""}}}, "empty bone name");
  NamedBones bones;
  bones.bones.resize(kMaxRecipeRows + 1, "bone");
  Refused(recipe, SetLightBones{0, bones}, "too many bones");
  const Recipe before = recipe;
  Check(Apply(recipe, EditBatch{{SetClockSpeed{2.0f}, SetShellAlphaTest{2.0f}}})
            .has_value(),
        "batch rejects a newly introduced model error");
  Check(recipe == before, "failed batch rolls back earlier successful edits");
  recipe.shell.alphaTest = 2.0f;
  Check(!Apply(recipe, SetClockSpeed{2.0f}),
        "existing model error permits unrelated valid edits");
  Check(!Apply(recipe, SetShellAlphaTest{0.5f}) && !HasErrors(Validate(recipe)),
        "an existing model error can be repaired");
  Check(!Apply(recipe, SetSource{"image", ImageSource{}}) &&
            HasErrors(Validate(recipe)),
        "incomplete image type switch remains editable with diagnostics");
  Check(!Apply(recipe, SetSource{"image", ImageSource{"textures/test.dds"}}) &&
            !HasErrors(Validate(recipe)),
        "completing an image draft clears its model diagnostics");
  Check(!Apply(recipe, SetSignal{"drive", GradientSignal{}}) &&
            HasErrors(Validate(recipe)),
        "incomplete gradient draft remains editable with diagnostics");
  recipe.signals.clear();
  for (std::size_t i = 0; i < kMaxRecipeRows; ++i) {
    recipe.signals.push_back(
        Signal{std::format("s{}", i), ConstantSignal{}, {}, {}});
  }
  Check(!HasErrors(Validate(recipe)), "collection at cap is valid");
  Refused(recipe, AddSignal{"overflow"}, "signal collection overflow");
  const LoadResult decoded =
      ParseRecipe(R"({"format":1,"keys":["default"],"clock":null})", "decode");
  Check(decoded.recipe && HasErrors(decoded.inputDiagnostics),
        "file decoding errors are retained separately from model diagnostics");
  Check(decoded.recipe && !HasErrors(Validate(*decoded.recipe)),
        "a decoded fallback cannot reconstruct the original file error");
  if (decoded.recipe) {
    Recipe edited = *decoded.recipe;
    Check(!Apply(edited, SetClockSpeed{2.0f}),
          "a recipe with file errors remains editable");
    Check(HasErrors(Validate(edited, decoded.inputDiagnostics)),
          "refreshing model diagnostics retains file errors");
    const LoadResult saved = ParseRecipe(SerializeRecipe(edited), "decode");
    Check(!saved.HasErrors() && saved.inputDiagnostics.empty(),
          "saving normalized fields and reloading clears file errors");
  }
  const LoadResult semantic = ParseRecipe(
      R"({"format":1,"keys":["default"],"signals":{"x":{"expr":"@missing"}}})",
      "semantic");
  Check(!HasErrors(semantic.inputDiagnostics) && semantic.HasErrors(),
        "repairable semantic errors are not retained as file decoding errors");
  return test::Finish("studio_validation");
}
