#include "recipe/Expression.h"
#include "studio/Edits.h"
#include "studio/PaintSession.h"
#include "studio/TermTemplates.h"
#include "test_support.h"

#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
float Evaluate(std::string_view a_expression, const Recipe &a_recipe) {
  const auto program = Program::Parse(a_expression);
  Check(program.has_value(), "the remapped expression parses");
  if (!program) {
    return 0.0f;
  }
  std::vector<Value> values;
  for (const std::string &name : program->References()) {
    const Source *source = a_recipe.FindSource(name);
    const MaterialSource *material =
        source ? Get<MaterialSource>(source->kind) : nullptr;
    Check(material != nullptr,
          "every remapped reference resolves to its source");
    values.emplace_back(
        material && material->channel == MaterialChannel::kRoughness ? 0.75f
                                                                     : 0.25f);
  }
  Program::Inputs inputs;
  inputs.refs = values;
  return AsScalar(program->Evaluate(inputs));
}

Recipe Destination() {
  Recipe recipe;
  recipe.id = "destination";
  recipe.sources = {{"a", MaterialSource{MaterialChannel::kMetallic}},
                    {"b", MaterialSource{MaterialChannel::kRoughness}}};
  return recipe;
}
}

int main() {
  Recipe paint;
  paint.sources = {{"a", MaterialSource{MaterialChannel::kRoughness}},
                   {"b", MaterialSource{MaterialChannel::kMetallic}}};
  paint.masks = {{std::string{kScratchMask}, "@a - @b"}};
  Recipe target = Destination();
  const float expected = Evaluate("@a - @b", paint);
  const EditBatch keep{KeepEdits(paint, target, "kept")};
  Check(!Apply(target, keep),
        "keeping a mask with swapped source names succeeds");
  const Mask *kept = target.FindMask("kept");
  Check(kept && test::Near(Evaluate(kept->text, target), expected),
        "Keep preserves mask values when reusing differently named sources");
  Check(target.sources.size() == 2,
        "Keep reuses both equivalent sources without adding duplicates");

  MaskPreset preset;
  preset.name = "swapped";
  preset.expression = "@a - @b";
  for (const Source &source : paint.sources) {
    preset.sources.emplace_back(source.name, source.kind);
  }
  const BuiltTerm materialised =
      MaterialiseTerm(preset, SourceCatalogOf(target));
  Check(materialised.edits.empty() &&
            test::Near(Evaluate(materialised.expression, target), expected),
        "preset materialisation preserves values while reusing swapped names");
  MaskPresets presets;
  presets.presets.push_back(preset);
  const BuiltTerm built =
      BuildTerm(PresetTerm{preset.name}, presets, SourceCatalogOf(target));
  Check(built.edits.empty() &&
            test::Near(Evaluate(built.expression, target), expected),
        "the offer BuildTerm path preserves the preset's remapped meaning");
  return test::Finish("studio_expressionrename");
}
