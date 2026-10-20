// GPL-3.0-only with the additional permission in COPYING.md.
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

Renumbering Removing(std::optional<LayerPlace> a_layer,
                     std::optional<std::size_t> a_output = std::nullopt) {
  Renumbering renumbering;
  for (std::size_t output = 0; output < 6; ++output) {
    OutputRenumbering entry;
    if (output != a_output) {
      entry.to = a_output && output > *a_output ? output - 1 : output;
    }
    for (std::size_t layer = 0; layer < 4; ++layer) {
      if (!a_layer || a_layer->output != output || layer < a_layer->layer) {
        entry.layers.emplace_back(layer);
      } else if (layer == a_layer->layer) {
        entry.layers.emplace_back(std::nullopt);
      } else {
        entry.layers.emplace_back(layer - 1);
      }
    }
    renumbering.outputs.push_back(std::move(entry));
  }
  return renumbering;
}
}

int main() {
  const RecipeRow recipe = Document();
  {
    RecipeRow linked = recipe;
    const PropertyLocation first{LayerOwner{3, 1}, "opacity", {}};
    const PropertyLocation second{LayerOwner{3, 1}, "tint", 2};
    linked.relationships = {{first, {ResourceKind::kSignal, "strength"}},
                            {second, {ResourceKind::kSignal, "strength"}}};
    Selection selection = Context();
    Navigation navigation;
    Check(NavigateProperty(navigation, selection, {LayerSubject{3, 1}, first},
                           linked),
          "a consumer link selects its owning layer and exact property");
    navigation.scroll = 42.0f;
    Check(NavigateProperty(navigation, selection, {LayerSubject{3, 1}, second},
                           linked) &&
              selection.property == second && navigation.back.size() == 2,
          "different properties on one owner retain separate Back entries");
    Check(GoBack(navigation, selection, linked) &&
              selection.property == first && navigation.scroll == 42.0f,
          "Back restores the property and scroll position");
    Check(!NavigateProperty(navigation, selection, {OutputSubject{3}, second},
                            linked),
          "a property cannot be paired with a different owner");
    RenumberIndexedSubjects(navigation, selection, "glow",
                            Removing(LayerPlace{3, 0}));
    Check(selection.property ==
                  PropertyLocation{LayerOwner{3, 0}, "opacity", {}} &&
              selection.subject == InspectorSubject{LayerSubject{3, 0}},
          "a layer removal moves the property focus with its layer");
    Check(NavigateProperty(navigation, selection, {LayerSubject{3, 1}, second},
                           linked),
          "a surviving consumer can be selected again");
    linked.relationships.clear();
    Check(ResolveInspectorSubject(selection, &linked) && !selection.property,
          "removing a relationship clears its obsolete property focus");
  }
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
    RenumberIndexedSubjects(navigation, selection, "glow",
                            Removing(LayerPlace{3, 0}));
    Check(Is<SignalSubject>(selection.subject),
          "a layer removal preserves a named inspector");
    Check(GoBack(navigation, selection, recipe) &&
              Is<RecipeSubject>(selection.subject),
          "Back skips a visit whose layer was removed");
    Check(Navigate(navigation, selection, OutputSubject{3}, recipe),
          "an output may be selected after a removal");
    navigation.scroll = 50.0f;
    RenumberIndexedSubjects(navigation, selection, "other",
                            Removing(std::nullopt, 3));
    Check(Is<OutputSubject>(selection.subject) && navigation.scroll == 50.0f,
          "edits to another document do not disturb the current subject");
    RenumberIndexedSubjects(navigation, selection, "glow",
                            Removing(LayerPlace{3, 0}));
    Check(selection.subject == InspectorSubject{OutputSubject{3}} &&
              navigation.scroll == 50.0f,
          "a layer removal keeps the output inspector in place");
    RenumberIndexedSubjects(navigation, selection, "glow",
                            Removing(std::nullopt, 2));
    Check(selection.subject == InspectorSubject{OutputSubject{2}},
          "removing an earlier output renumbers the output inspector");
    RenumberIndexedSubjects(navigation, selection, "glow",
                            Removing(std::nullopt, 2));
    Check(Is<RecipeSubject>(selection.subject) && navigation.scroll == 0.0f,
          "removing the inspected output returns to the recipe");
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
        AddLayer{},
        RemoveLayer{},
        MoveLayer{},
        ClearLayers{},
        AddOutput{},
        RemoveOutput{},
        ClearOutputs{},
        ClearRecipe{},
        AddLight{},
        RemoveSignal{.name = "strength", .cascade = true},
        RemoveCurve{.name = "response", .cascade = true},
        RemoveMask{.name = "spine", .cascade = true},
        RemoveSource{.name = "pattern", .cascade = true}};
    for (const RecipeEdit &edit : structural) {
      Check(CanRenumber(std::span{&edit, std::size_t{1}}),
            "structural edits and cascade removals can renumber layers");
    }
    const std::vector<RecipeEdit> values{
        SetLayerOpacity{},   SetScalar{},
        SetExpression{},     SetSource{},
        SetOutputSelector{}, RenameSignal{},
        ResetLight{},        RemoveSignal{.name = "strength"}};
    Check(!CanRenumber(values) && !CanRenumber({}),
          "value edits, plain removals and empty batches renumber nothing");
  }
  {
    Selection selection = Context();
    Navigation navigation;
    std::optional<PendingSubject> pending =
        PendingSubject{"glow", SignalSubject{"pending"}};
    RecipeRow absent = recipe;
    Check(!ResolvePendingSubject(navigation, selection, pending, &absent) &&
              pending.has_value(),
          "a pending subject waits while its resource is absent");
    RecipeRow arrived = recipe;
    SignalRow appeared;
    appeared.name = "pending";
    arrived.signals.push_back(appeared);
    Check(ResolvePendingSubject(navigation, selection, pending, &arrived) &&
              Is<SignalSubject>(selection.subject) && !pending,
          "a pending subject navigates once its resource appears");
    pending = PendingSubject{"glow", SourceSubject{"pattern"}};
    RecipeRow other = recipe;
    other.id = "elsewhere";
    Check(!ResolvePendingSubject(navigation, selection, pending, &other) &&
              !pending,
          "a pending subject is dropped when the document changes");
    pending = PendingSubject{"elsewhere", SourceSubject{"pattern"}};
    Check(!ResolvePendingSubject(navigation, selection, pending, &recipe) &&
              !pending && !Is<SourceSubject>(selection.subject),
          "a pending subject of another recipe never applies to this one");
  }
  return test::Finish("studio_navigation");
}
