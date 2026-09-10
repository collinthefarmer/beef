#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "test_support.h"

#include <filesystem>
#include <optional>
#include <string>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
const Recipe &Canonical() {
  static const Recipe recipe = [] {
    const auto path =
        std::filesystem::path{BEEF_FIXTURES_DIR}.parent_path().parent_path() /
        "schema" / "example-magicka.json";
    const auto loaded = ParseRecipe(test::ReadFile(path), "example-magicka");
    Check(loaded.recipe.has_value(), "schema/example-magicka.json parses");
    return loaded.recipe.value_or(Recipe{});
  }();
  return recipe;
}

const SurfaceOutput *MaterialAt(const Recipe &a_recipe, std::size_t a_output) {
  return a_output < a_recipe.outputs.size()
             ? Get<SurfaceOutput>(a_recipe.outputs[a_output])
             : nullptr;
}

const LightOutput *LightAt(const Recipe &a_recipe, std::size_t a_output) {
  return a_output < a_recipe.outputs.size()
             ? Get<LightOutput>(a_recipe.outputs[a_output])
             : nullptr;
}

bool RoundTrips(const Recipe &a_recipe) {
  const auto back = ParseRecipe(SerializeRecipe(a_recipe), a_recipe.id);
  return back.recipe && *back.recipe == a_recipe;
}

void Accepts(Recipe &a_recipe, const RecipeEdit &a_edit,
             const std::string &a_what) {
  const auto problem = Apply(a_recipe, a_edit);
  Check(!problem,
        a_what + " is accepted" +
            (problem ? ": " + problem->where + ": " + problem->message : ""));
  if (!problem) {
    Check(RoundTrips(a_recipe), a_what + ": edited recipe round-trips");
  }
}

void Refused(const Recipe &a_base, const RecipeEdit &a_edit,
             const std::string &a_what) {
  Recipe copy = a_base;
  const auto problem = Apply(copy, a_edit);
  Check(problem.has_value(), a_what + " is refused");
  Check(copy == a_base,
        a_what + ": a refused edit leaves the recipe unchanged");
}

void Undoes(const Recipe &a_base, const RecipeEdit &a_edit,
            const RecipeEdit &a_inverse, const std::string &a_what) {
  Recipe copy = a_base;
  Accepts(copy, a_edit, a_what);
  Accepts(copy, a_inverse, a_what + " (undo)");
  Check(copy == a_base, a_what + ": applying the inverse returns the recipe");
}
}

int main() {
  const Recipe &base = Canonical();
  Check(!base.outputs.empty(), "canonical recipe has outputs");

  {
    const auto *m = MaterialAt(base, 0);
    const auto *strength =
        m ? ScalarOf(m->scalars, ScalarField::kStrength) : nullptr;
    Check(strength && strength->has_value(),
          "output 0 emissive has a strength scalar");
    if (strength && *strength) {
      Undoes(base, SetScalar{0, ScalarField::kStrength, Param{3.0f}},
             SetScalar{0, ScalarField::kStrength, **strength},
             "SetScalar strength");
    }
  }

  {
    const auto *m = MaterialAt(base, 1);
    Check(m && m->scalars.color.has_value(),
          "output 1 fuzz has a color scalar");
    if (m && m->scalars.color) {
      Undoes(
          base,
          SetColorScalar{1, Vec3Param{std::array<Param, 3>{0.2f, 0.4f, 0.6f}}},
          SetColorScalar{1, *m->scalars.color}, "SetColorScalar");
    }
  }

  {
    const auto *m = MaterialAt(base, 2);
    Check(m != nullptr, "output 2 is a material output");
    if (m) {
      const std::size_t top = m->stack.size();
      Undoes(base, AddLayer{2, DefaultLayer(), std::nullopt},
             RemoveLayer{2, top}, "AddLayer then RemoveLayer");
    }
  }

  Undoes(base, MoveLayer{0, 0, 2}, MoveLayer{0, 2, 0}, "MoveLayer round trip");

  {
    const auto *m = MaterialAt(base, 2);
    const Layer *layer = m && m->stack.size() > 1 ? &m->stack[1] : nullptr;
    Check(layer != nullptr, "output 2 has a second layer");
    if (layer) {
      Undoes(base, SetLayerOpacity{2, 1, Param{0.5f}},
             SetLayerOpacity{2, 1, layer->opacity}, "SetLayerOpacity");
    }
  }

  Undoes(base, AddSignal{"probeSignal"}, RemoveSignal{"probeSignal"},
         "AddSignal then RemoveSignal");
  Undoes(base, AddCurve{"probeCurve"}, RemoveCurve{"probeCurve"},
         "AddCurve then RemoveCurve");
  Undoes(base, AddMask{"probeMask"}, RemoveMask{"probeMask"},
         "AddMask then RemoveMask");
  Undoes(base, AddSource{"probeSource", MaterialClustersSource{}},
         RemoveSource{"probeSource"}, "AddSource clusters then RemoveSource");

  Undoes(base, RenameSignal{"glowHue", "glowTint"},
         RenameSignal{"glowTint", "glowHue"}, "RenameSignal and back");

  {
    const auto *rest = base.FindCurve("rest");
    Check(rest != nullptr, "curve rest exists");
    if (rest) {
      Undoes(base, SetCurve{"rest", "x * 0.5"}, SetCurve{"rest", rest->text},
             "SetCurve");
    }
  }

  {
    const auto *light = LightAt(base, 4);
    Check(light != nullptr, "output 4 is a light");
    if (light) {
      Undoes(base, SetLightParam{4, LightParam::kIntensity, Param{2.5f}},
             SetLightParam{4, LightParam::kIntensity, light->intensity},
             "SetLightParam intensity");
      Undoes(base, SetLightShadow{4, !light->shadow},
             SetLightShadow{4, light->shadow}, "SetLightShadow");
      Undoes(base, SetLightReplace{4, !light->replace},
             SetLightReplace{4, light->replace}, "SetLightReplace");
    }
  }

  Undoes(base, SetShellParam{ShellParam::kAlpha, Param{0.5f}},
         SetShellParam{ShellParam::kAlpha, base.shell.alpha},
         "SetShellParam alpha");
  Undoes(base, SetShellBlend{ShellBlend::kAlpha},
         SetShellBlend{base.shell.blend}, "SetShellBlend");
  Undoes(base, SetShellAlphaTest{0.5f}, SetShellAlphaTest{base.shell.alphaTest},
         "SetShellAlphaTest");

  Undoes(base, SetPriority{7}, SetPriority{base.priority}, "SetPriority");
  Undoes(base, SetClockSpeed{2.0f}, SetClockSpeed{base.clock.speed},
         "SetClockSpeed");

  {
    RecipeKey key;
    key.kind = KeyKind::kMaterial;
    key.operand = std::string{"*fire*"};
    Undoes(base, AddKey{key}, RemoveKey{key}, "AddKey then RemoveKey");
    Refused(base, AddKey{base.keys.front()}, "AddKey duplicate");
    Refused(base, RemoveKey{base.keys.front()}, "RemoveKey the only key");
  }

  MaterialClustersSource bad;
  bad.clusters = 0;
  Refused(base, AddSource{"badClusters", bad},
          "AddSource with zero clusters (loader parity)");

  Refused(base,
          SetScalar{0, ScalarField::kStrength, Param{Ref{"noSuchSignal"}}},
          "SetScalar reading an unknown signal");
  Refused(base, SetScalar{99, ScalarField::kStrength, Param{1.0f}},
          "SetScalar on an out-of-range output");
  Refused(base, SetLightParam{2, LightParam::kIntensity, Param{1.0f}},
          "SetLightParam on a material output");
  Refused(base, SetShellPoint{ShellPoint::kSpinAxis, Vec3{0.0f, 0.0f, 0.0f}},
          "SetShellPoint with a zero spin axis");
  Refused(base, SetShellAlphaTest{2.0f}, "SetShellAlphaTest out of range");
  Refused(base, SetShellMaterial{ShellMaterial::kVanilla},
          "SetShellMaterial vanilla while a non-emissive shell slot is bound");
  Refused(base, RemoveSignal{"glowHue"}, "RemoveSignal that is referenced");

  {
    const auto counts = CountReferences(base);
    const auto found = counts.signals.find("glowHue");
    Check(found != counts.signals.end() && found->second > 0,
          "CountReferences finds glowHue in use");
  }

  {
    Check(!Describe(RecipeEdit{AddLight{}}).empty(),
          "Describe AddLight is text");
    Check(!Describe(RecipeEdit{SetPriority{3}}).empty(),
          "Describe SetPriority is text");
    EditBatch batch;
    batch.edits.push_back(AddSignal{"a"});
    batch.edits.push_back(AddSignal{"b"});
    const auto text = Describe(batch);
    Check(text.find("; ") != std::string::npos,
          "Describe(batch) joins edits with a separator");
  }

  {
    Recipe copy = base;
    EditBatch good;
    good.edits.push_back(AddSignal{"batchOne"});
    good.edits.push_back(RenameSignal{"batchOne", "batchTwo"});
    Check(!Apply(copy, good), "a valid batch applies");
    Check(RoundTrips(copy), "the batched recipe round-trips");

    Recipe rollback = base;
    EditBatch bad;
    bad.edits.push_back(AddSignal{"batchThree"});
    bad.edits.push_back(RemoveSignal{"noSuchSignal"});
    Check(Apply(rollback, bad).has_value(),
          "a batch with a bad edit is refused");
    Check(rollback == base, "a refused batch leaves the recipe unchanged");
  }

  return test::Finish("studio_edits");
}
