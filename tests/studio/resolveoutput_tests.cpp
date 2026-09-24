// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/ResolveOutput.h"
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
  recipe.signals = {
      Signal{"glow", ConstantSignal{2.0f}, std::nullopt},
      Signal{"quarter", ConstantSignal{0.25f}, std::nullopt},
  };
  return recipe;
}

SurfaceOutput MakeFuzz() {
  SurfaceOutput fuzz;
  fuzz.surface = Surface::kMaterial;
  fuzz.slot = Slot::kFuzz;
  fuzz.scalars.color =
      std::array<Param, 3>{Ref{"glow"}, Param{0.5f}, Param{0.0f}};
  fuzz.scalars.weight = Ref{"glow"};

  Layer first;
  first.opacity = Ref{"quarter"};
  Layer second;
  second.opacity = Param{0.5f};
  fuzz.stack = {first, second};
  return fuzz;
}
}

int main() {
  const Recipe recipe = MakeRecipe();
  const SignalGraph graph = SignalGraph::Compile(recipe.signals, recipe.curves);
  SignalState state{graph};
  NullEnvironment environment;
  state.Tick(environment, TickInputs{0.0f, 0.0f});

  const SurfaceOutput fuzz = MakeFuzz();
  const ResolvedOutput resolved = ResolveOutput(fuzz, state);

  Check(resolved.IsNamed(ScalarField::kColor),
        "the set colour scalar is named");
  Check(Near(resolved.color.x, 2.0f) && Near(resolved.color.y, 0.5f) &&
            Near(resolved.color.z, 0.0f),
        "colour resolves its signal reference and literals");
  Check(resolved.IsNamed(ScalarField::kWeight),
        "the set weight scalar is named");
  Check(Near(resolved.Scalar(ScalarField::kWeight), 2.0f),
        "weight resolves through the glow signal");

  Check(!resolved.IsNamed(ScalarField::kStrength),
        "a scalar the fuzz slot does not carry is not named");
  Check(Near(resolved.Scalar(ScalarField::kStrength),
             ScalarFallback(ScalarField::kStrength)),
        "an unnamed scalar reads its fallback");

  Check(resolved.opacities.size() == 2, "every layer opacity is resolved");
  Check(Near(resolved.opacities[0], 0.25f),
        "layer opacity resolves its signal reference");
  Check(Near(resolved.opacities[1], 0.5f),
        "layer opacity resolves its literal");

  SurfaceOutput bare;
  bare.slot = Slot::kFuzz;
  const ResolvedOutput empty = ResolveOutput(bare, state);
  Check(!empty.IsNamed(ScalarField::kColor) &&
            !empty.IsNamed(ScalarField::kWeight),
        "an output that sets no scalar names none");
  Check(Near(empty.color.x, ScalarFallback(ScalarField::kColor)) &&
            Near(empty.color.y, ScalarFallback(ScalarField::kColor)) &&
            Near(empty.color.z, ScalarFallback(ScalarField::kColor)),
        "unset colour reads the colour fallback");
  Check(Near(empty.Scalar(ScalarField::kWeight),
             ScalarFallback(ScalarField::kWeight)),
        "unset weight reads the weight fallback");
  Check(empty.opacities.empty(),
        "an output with no layers resolves no opacity");

  LightOutput light;
  light.color = std::array<Param, 3>{Ref{"glow"}, Param{1.0f}, Param{1.0f}};
  light.intensity = Ref{"glow"};
  light.size = Ref{"quarter"};
  light.cutoff = Param{0.5f};
  const ResolvedLight lit = ResolveLight(light, state);
  Check(Near(lit.color.x, 2.0f) && Near(lit.color.y, 1.0f) &&
            Near(lit.color.z, 1.0f),
        "light colour resolves its signal reference and literals");
  Check(Near(lit.intensity, 2.0f), "light intensity resolves through glow");
  Check(Near(lit.size, 0.25f), "light size resolves through quarter");
  Check(Near(lit.cutoff, 0.5f), "light cutoff resolves its literal");

  return test::Finish("studio_resolveoutput");
}
