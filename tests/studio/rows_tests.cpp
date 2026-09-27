// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Rows.h"
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
      Signal{"scaled", ExprSignal{"glow * 2"}, std::nullopt},
      Signal{"hit",
             TriggerSignal{EventOrigin{"HitEvent", EventFilter{}}, 1.0f, 4},
             std::nullopt},
  };
  recipe.curves = {Curve{"ramp", "0,0 1,1"}};
  recipe.masks = {Mask{"edge", "@glow"}};

  SurfaceOutput surface;
  surface.surface = Surface::kMaterial;
  surface.slot = Slot::kFuzz;
  surface.replace = true;
  surface.selector.anyOf.push_back(
      {SelectorKind::kGeometry, std::string{"Body*"}});
  surface.scalars.color = std::array<Param, 3>{1.0f, 0.5f, 0.0f};
  surface.scalars.weight = Param{0.75f};

  Layer first;
  first.source = Ref{"tex"};
  first.blend = Blend::kAdd;
  first.opacity = Param{0.5f};
  first.mask = Ref{"edge"};
  first.color = std::array<Param, 3>{0.0f, 1.0f, 0.0f};
  first.curve = CurveRef{"@ramp"};

  Layer second;
  second.source = Vec3{1.0f, 0.0f, 0.0f};
  second.blend = Blend::kReplace;

  surface.stack = {first, second};

  LightOutput light;
  light.replace = true;
  light.selector.anyOf.push_back(
      {SelectorKind::kAddon, FormRef::From("ArmorAddon")});
  recipe.outputs = {Output{surface}, Output{light}};
  return recipe;
}
}

int main() {
  const Recipe recipe = MakeRecipe();
  const RecipeGraph graph = RecipeGraph::Compile(recipe);
  const RowTypes rows{recipe, graph};

  const SignalRow constant = SignalRowOf(recipe.signals[0], rows, 3);
  Check(constant.name == "glow", "constant signal keeps its name");
  Check(constant.kind == SignalKindId::kConstant, "constant signal kind");
  Check(constant.type == ValueType::kScalar, "constant signal is scalar typed");
  Check(constant.constant.has_value() &&
            Near(AsScalar(*constant.constant), 2.0f),
        "constant signal carries its constant value");
  Check(constant.references == 3, "signal references pass through");
  Check(Is<ConstantSignal>(constant.definition),
        "signal definition holds the declared kind");

  const SignalRow expr = SignalRowOf(recipe.signals[1], rows, 0);
  Check(expr.kind == SignalKindId::kExpr, "expression signal kind");
  Check(expr.text == "glow * 2", "expression signal carries its text");

  const SignalRow trigger = SignalRowOf(recipe.signals[2], rows, 1);
  Check(trigger.kind == SignalKindId::kTrigger, "trigger signal kind");
  Check(trigger.event == "HitEvent", "trigger signal carries its event id");

  const TextRow curve = CurveRowOf(recipe.curves[0], 2);
  Check(curve.name == "ramp" && curve.text == "0,0 1,1" &&
            curve.references == 2,
        "curve row projects name, text and references");

  const TextRow mask = MaskRowOf(recipe.masks[0], 4);
  Check(mask.name == "edge" && mask.text == "@glow" && mask.references == 4,
        "mask row projects name, text and references");

  const OutputRow surface = OutputRowOf(recipe, 0);
  Check(surface.index == 0, "output keeps its index");
  Check(surface.target == Target::kMaterial, "surface output targets material");
  Check(surface.surface == Surface::kMaterial && surface.slot == Slot::kFuzz,
        "surface output projects surface and slot");
  Check(surface.replace, "surface output projects replace");
  const SurfaceOutput *surfaceDefinition =
      Get<SurfaceOutput>(recipe.outputs[0]);
  Check(surfaceDefinition && surface.selection == surfaceDefinition->selector,
        "surface output preserves the typed selector for editing");
  Check(surface.layers.size() == 2, "surface output projects every layer");
  Check(surface.scalars.size() == 2,
        "fuzz output projects its two set scalars");

  bool sawColor = false;
  bool sawWeight = false;
  for (const ScalarRow &scalar : surface.scalars) {
    sawColor = sawColor || scalar.name == "color";
    sawWeight = sawWeight || (scalar.name == "weight" && scalar.text == "0.75");
  }
  Check(sawColor, "the color scalar is projected");
  Check(sawWeight, "the weight scalar projects its text");

  const LayerRow &first = surface.layers[0];
  Check(first.source == "@tex", "layer reference source becomes @name");
  Check(first.mask == "@edge", "layer mask becomes @name");
  Check(first.blend == Blend::kAdd, "layer blend projects its kind");
  Check(first.opacityText == "0.5", "layer opacity text projects the param");
  Check(first.curve == "@ramp", "layer curve projects its reference");
  Check(!first.color.empty(), "layer colour projects when present");
  Check(Near(first.opacity, 1.0f), "layer live opacity stays at its default");
  Check(first.texture == TextureHandle{}, "layer live texture stays empty");

  const LayerRow &second = surface.layers[1];
  Check(second.source == "1, 0, 0", "layer colour source becomes a literal");
  Check(second.mask.empty(), "a layer with no mask projects an empty mask");

  const OutputRow light = OutputRowOf(recipe, 1);
  Check(light.target == Target::kLight, "light output targets light");
  const LightOutput *lightDefinition = Get<LightOutput>(recipe.outputs[1]);
  Check(lightDefinition && light.replace &&
            light.selection == lightDefinition->selector,
        "light output preserves replace and its typed form selector");
  Check(light.layers.empty() && light.scalars.empty(),
        "light output carries no material layers or scalars");

  const OutputRow missing = OutputRowOf(recipe, 99);
  Check(missing.index == 99 && missing.layers.empty(),
        "an out-of-range output index is inert, not a crash");

  return test::Finish("studio_rows");
}
