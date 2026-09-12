#include "recipe/Expression.h"
#include "studio/Edits.h"
#include "studio/PaintSession.h"
#include "studio/TermTemplates.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <span>
#include <string>
#include <utility>
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
  const std::array swap{ExpressionRename{"a", "b"}, ExpressionRename{"b", "a"}};
  Check(RenameInExpression("@a - @b + @a", swap, false) == "@b - @a + @b",
        "swapped references are renamed from the original tokens only");
  const std::array chain{ExpressionRename{"a", "b"},
                         ExpressionRename{"b", "c"}};
  Check(RenameInExpression("@a - @b", chain, false) == "@b - @c",
        "a replacement cannot be renamed again by a later mapping");
  const std::array aliases{ExpressionRename{"a", "shared"},
                           ExpressionRename{"b", "shared"}};
  Check(RenameInExpression("@a + @b", aliases, false) == "@shared + @shared",
        "distinct aliases may intentionally reuse one source");
  Check(RenameInExpression("@a + @ab + @a_2 + @z", swap, false) ==
            "@b + @ab + @a_2 + @z",
        "remapping changes whole matching identifiers only");
  Check(RenameInExpression("@a + @a \t\n(@b)", swap, false) ==
            "@b + @a \t\n(@a)",
        "source remapping preserves curve names with arbitrary whitespace");
  Check(RenameInExpression("@a + @a \t\n(@b)", swap, true) ==
            "@a + @b \t\n(@b)",
        "curve remapping preserves the argument's source reference");
  Check(RenameInExpression("@a + @ab", "a", "b", false) == "@b + @ab",
        "the existing single-name API retains whole-token behavior");
  Check(RenameInExpression("@a + 1", std::span<const ExpressionRename>{},
                           false) == "@a + 1",
        "an empty mapping preserves the expression");

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
