#include "studio/Navigation.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
RecipeRow Document() {
  RecipeRow recipe;
  recipe.id = "glow";
  OutputRow output;
  output.index = 3;
  output.slot = Slot::kEmissive;
  output.layers.resize(2);
  recipe.outputs.push_back(output);
  OutputRow light;
  light.index = 5;
  light.target = Target::kLight;
  recipe.outputs.push_back(light);
  SignalRow signal;
  signal.name = "strength";
  recipe.signals.push_back(signal);
  SourceRow source;
  source.name = "pattern";
  recipe.sourceRows.push_back(source);
  recipe.maskRows.push_back(TextRow{.name = "spine", .text = "1"});
  recipe.curves.push_back(TextRow{.name = "response", .text = "x"});
  return recipe;
}

Selection Context() {
  Selection selection;
  selection.recipeID = "glow";
  selection.piece = PieceRef{1, 2, false};
  selection.geometry = "armor";
  return selection;
}
}

int main() {
  const RecipeRow recipe = Document();
  {
    Check(InspectorSubjectExists(RecipeSubject{}, recipe) &&
              InspectorSubjectExists(ShellSubject{}, recipe),
          "document inspectors do not require applied geometry");
    Check(InspectorSubjectExists(OutputSubject{3}, recipe) &&
              InspectorSubjectExists(LayerSubject{3, 1}, recipe) &&
              InspectorSubjectExists(OutputSubject{5}, recipe),
          "output subjects resolve authored indices rather than row positions");
    Check(!InspectorSubjectExists(OutputSubject{0}, recipe) &&
              !InspectorSubjectExists(LayerSubject{3, 2}, recipe) &&
              !InspectorSubjectExists(LayerSubject{5, 0}, recipe),
          "missing indices and light layers are rejected");
    Check(InspectorSubjectExists(SignalSubject{"strength"}, recipe) &&
              InspectorSubjectExists(SourceSubject{"pattern"}, recipe) &&
              InspectorSubjectExists(MaskSubject{"spine"}, recipe) &&
              InspectorSubjectExists(CurveSubject{"response"}, recipe),
          "named subjects resolve within their own resource collection");
    Check(!InspectorSubjectExists(SourceSubject{"strength"}, recipe),
          "resource identity includes its type");
  }
  {
    Selection selection = Context();
    Navigation navigation;
    Check(Navigate(navigation, selection, LayerSubject{3, 1}, recipe),
          "navigation selects an authored layer");
    Check(selection.slot == Slot::kEmissive && selection.layer == 1,
          "layer navigation aligns existing output selection");
    navigation.scroll = 120.0f;
    Check(Navigate(navigation, selection, SignalSubject{"strength"}, recipe),
          "layer inspector can follow its named input");
    navigation.scroll = 30.0f;
    Check(GoBack(navigation, selection, recipe) &&
              selection.subject == InspectorSubject{LayerSubject{3, 1}} &&
              selection.geometry == "armor" && selection.layer == 1 &&
              navigation.scroll == 120.0f,
          "Back restores the originating layer, geometry, and scroll");
    Check(GoBack(navigation, selection, recipe) &&
              Is<RecipeSubject>(selection.subject) &&
              navigation.scroll == 0.0f &&
              !GoBack(navigation, selection, recipe),
          "Back terminates at the original document inspector");
  }
  {
    Selection selection = Context();
    Navigation navigation;
    Check(!Navigate(navigation, selection, SignalSubject{"gone"}, recipe) &&
              !Navigate(navigation, selection, RecipeSubject{}, recipe) &&
              navigation.back.empty(),
          "missing and duplicate destinations do not change history");
    Check(Navigate(navigation, selection, SignalSubject{"strength"}, recipe),
          "navigate to a resource before it is removed");
    Check(Navigate(navigation, selection, SourceSubject{"pattern"}, recipe),
          "navigate onward before resource deletion");
    RecipeRow changed = recipe;
    changed.signals.clear();
    Check(GoBack(navigation, selection, changed) &&
              Is<RecipeSubject>(selection.subject),
          "Back skips a deleted resource instead of choosing another");
    selection.subject = SignalSubject{"gone"};
    Check(!ResolveInspectorSubject(selection, &changed) &&
              Is<RecipeSubject>(selection.subject),
          "a deleted current subject resolves to its document");
    changed.id = "other";
    selection.subject = SourceSubject{"pattern"};
    Check(!ResolveInspectorSubject(selection, &changed) &&
              selection.recipeID == "glow",
          "a different document cannot retarget an inspector");
    Check(!ResolveInspectorSubject(selection, nullptr),
          "missing documents resolve without dereferencing them");
  }
  {
    Selection selection = Context();
    Navigation navigation;
    Check(
        Navigate(navigation, selection, LayerSubject{3, 0}, recipe) &&
            Navigate(navigation, selection, SignalSubject{"strength"}, recipe),
        "a layer origin can be retained behind a resource inspector");
    InvalidateIndexedSubjects(navigation, selection, "glow");
    Check(Is<SignalSubject>(selection.subject) && !selection.layer,
          "structural invalidation preserves a named inspector but clears its "
          "layer context");
    Check(GoBack(navigation, selection, recipe) &&
              Is<RecipeSubject>(selection.subject),
          "Back cannot revisit an index whose layer may have moved");
    Check(Navigate(navigation, selection, OutputSubject{3}, recipe),
          "an output may be explicitly selected after invalidation");
    navigation.scroll = 50.0f;
    InvalidateIndexedSubjects(navigation, selection, "other");
    Check(Is<OutputSubject>(selection.subject) && navigation.scroll == 50.0f,
          "edits to another document do not disturb the current subject");
    InvalidateIndexedSubjects(navigation, selection, "glow");
    Check(Is<RecipeSubject>(selection.subject) && navigation.scroll == 0.0f,
          "current index subjects are cleared before a structural edit");
  }
  {
    Selection selection = Context();
    Navigation navigation;
    for (std::size_t i = 0; i < kMaxInspectorHistory + 8; ++i) {
      const InspectorSubject subject = i % 2 == 0
                                           ? InspectorSubject{ShellSubject{}}
                                           : InspectorSubject{RecipeSubject{}};
      Check(Navigate(navigation, selection, subject, recipe),
            "alternating inspectors creates a navigation step");
    }
    Check(navigation.back.size() == kMaxInspectorHistory,
          "inspector history has bounded storage");
    selection.piece.actorID = 9;
    Check(!GoBack(navigation, selection, recipe) && navigation.back.empty(),
          "history from another wearer is discarded");
  }
  {
    const std::vector<RecipeEdit> structural{
        AddLayer{},     RemoveLayer{}, MoveLayer{},
        ClearLayers{},  AddOutput{},   RemoveOutput{},
        ClearOutputs{}, ClearRecipe{}, AddLight{}};
    for (const RecipeEdit &edit : structural) {
      Check(
          ShouldInvalidateIndexedSubjects(std::span{&edit, std::size_t{1}}),
          "structural edits invalidate positional subjects before publication");
    }
    const std::vector<RecipeEdit> values{
        SetLayerOpacity{},   SetScalar{},    SetExpression{}, SetSource{},
        SetOutputSelector{}, RenameSignal{}, ResetLight{}};
    Check(!ShouldInvalidateIndexedSubjects(values) &&
              !ShouldInvalidateIndexedSubjects({}),
          "value edits and empty batches preserve positional subjects");
  }
  return test::Finish("studio_navigation");
}
