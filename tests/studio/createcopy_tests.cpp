// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Create.h"
#include "studio/RecipeSnapshot.h"
#include "test_support.h"

#include <string>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
Recipe Document() {
  Recipe recipe;
  recipe.id = "glow";
  recipe.curves = {{"tone", "x * x", "a note"}};
  WaveSignal wave;
  wave.amplitude = 0.5f;
  wave.period = 3.0f;
  recipe.signals = {{"drive", wave, CurveRef{"@tone"}, {}},
                    {"level", ConstantSignal{0.25f}, {}, {}}};
  ImageSource image;
  image.texture = ImageFile{"textures/glow.dds"};
  image.transpose = true;
  image.mip = 2.0f;
  recipe.sources = {{"pattern", image, {}}};
  recipe.masks = {{"coverage", "@pattern", {}}};
  SurfaceOutput output = DefaultOutput(Surface::kMaterial, Slot::kEmissive);
  Layer layer = DefaultLayer();
  layer.source = Ref{"pattern"};
  layer.opacity = Ref{"drive"};
  layer.mask = Ref{"coverage"};
  layer.curve = CurveRef{"@tone"};
  output.stack = {layer};
  recipe.outputs = {output};
  return recipe;
}

RecipeRow RowOf(const Recipe &a_recipe, const ReferenceCounts &a_refs) {
  return BuildRecipeRow(RecipeRowInput{.recipe = a_recipe,
                                       .key = RecipeKey{},
                                       .full = true,
                                       .references = a_refs});
}

Recipe Applied(const Recipe &a_recipe, const Created &a_made) {
  Recipe copy = a_recipe;
  Check(!Apply(copy, EditBatch{a_made.edits}), "the copy's edits apply");
  return copy;
}

void CopiesSignal(const Recipe &a_recipe, const RecipeRow &a_row) {
  const Created made = Create(NewCopy{SignalSubject{"drive"}}, a_row);
  const Recipe after = Applied(a_recipe, made);
  const Signal *original = after.FindSignal("drive");
  const Signal *copy = after.FindSignal("drive2");
  Check(made.subject == InspectorSubject{SignalSubject{"drive2"}},
        "a signal copy selects the copy");
  Check(original && copy && copy->kind == original->kind &&
            copy->curve == original->curve,
        "a signal copy keeps the kind, settings and curve");
  Check(after.outputs == a_recipe.outputs,
        "references to the copied signal stay on the original");

  const Created constant = Create(NewCopy{SignalSubject{"level"}}, a_row);
  const Recipe leveled = Applied(a_recipe, constant);
  const Signal *level = leveled.FindSignal("level2");
  Check(level && level->kind == SignalKind{ConstantSignal{0.25f}},
        "a constant signal copy keeps its value");
}

void CopiesSource(const Recipe &a_recipe, const RecipeRow &a_row) {
  const Created made = Create(NewCopy{SourceSubject{"pattern"}}, a_row);
  const Recipe after = Applied(a_recipe, made);
  const Source *copy = after.FindSource("pattern2");
  Check(made.subject == InspectorSubject{SourceSubject{"pattern2"}},
        "a source copy selects the copy");
  Check(copy && copy->kind == a_recipe.sources.front().kind,
        "a source copy keeps the kind and its settings");
  Check(after.outputs == a_recipe.outputs &&
            after.masks.front().text == a_recipe.masks.front().text,
        "references to the copied source stay on the original");
}

void CopiesMaskAndCurve(const Recipe &a_recipe, const RecipeRow &a_row) {
  const Created mask = Create(NewCopy{MaskSubject{"coverage"}}, a_row);
  const Recipe masked = Applied(a_recipe, mask);
  const Mask *maskCopy = masked.FindMask("coverage2");
  Check(mask.subject == InspectorSubject{MaskSubject{"coverage2"}} &&
            maskCopy && maskCopy->text == "@pattern",
        "a mask copy keeps the expression under a fresh name");
  Check(masked.outputs == a_recipe.outputs,
        "references to the copied mask stay on the original");

  const Created curve = Create(NewCopy{CurveSubject{"tone"}}, a_row);
  const Recipe curved = Applied(a_recipe, curve);
  const Curve *curveCopy = curved.FindCurve("tone2");
  Check(curve.subject == InspectorSubject{CurveSubject{"tone2"}} && curveCopy &&
            curveCopy->text == "x * x",
        "a curve copy keeps the expression under a fresh name");
  Check(curved.outputs == a_recipe.outputs &&
            curved.signals == a_recipe.signals,
        "references to the copied curve stay on the original");
}

void NamesAvoidEveryKind(const RecipeRow &a_row) {
  RecipeRow crowded = a_row;
  crowded.maskRows.push_back(TextRow{.name = "drive2", .text = "1"});
  const Created made = Create(NewCopy{SignalSubject{"drive"}}, crowded);
  Check(made.subject == InspectorSubject{SignalSubject{"drive3"}},
        "a copy's name avoids names of every resource kind");
}

void MissingRowsCopyNothing(const RecipeRow &a_row) {
  const InspectorSubject subjects[]{
      SignalSubject{"absent"}, SourceSubject{"absent"}, MaskSubject{"absent"},
      CurveSubject{"absent"},  OutputSubject{0},        LayerSubject{0, 0},
      RecipeSubject{}};
  for (const InspectorSubject &subject : subjects) {
    const Created made = Create(NewCopy{subject}, a_row);
    Check(made.edits.empty() && made.subject == subject,
          "a copy of a missing or unsupported row makes no edits");
  }
}
}

int main() {
  const Recipe recipe = Document();
  const ReferenceCounts refs = CountReferences(recipe);
  const RecipeRow row = RowOf(recipe, refs);
  CopiesSignal(recipe, row);
  CopiesSource(recipe, row);
  CopiesMaskAndCurve(recipe, row);
  NamesAvoidEveryKind(row);
  MissingRowsCopyNothing(row);
  return test::Finish("studio_createcopy");
}
