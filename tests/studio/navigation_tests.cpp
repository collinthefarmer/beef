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
    Check(NavigateProperty(navigation, selection, LayerSubject{3, 1}, first,
                           linked),
          "a consumer link selects its owning layer and exact property");
    navigation.scroll = 42.0f;
    Check(NavigateProperty(navigation, selection, LayerSubject{3, 1}, second,
                           linked) &&
              selection.property == second && navigation.back.size() == 2,
          "different properties on one owner retain separate Back entries");
    Check(GoBack(navigation, selection, linked) &&
              selection.property == first && navigation.scroll == 42.0f,
          "Back restores the property and scroll position");
    Check(!NavigateProperty(navigation, selection, OutputSubject{3}, second,
                            linked),
          "a property cannot be paired with a different owner");
    InvalidateIndexedSubjects(navigation, selection, "glow");
    Check(!selection.property && Is<RecipeSubject>(selection.subject),
          "structural changes clear positional property focus");
    Check(NavigateProperty(navigation, selection, LayerSubject{3, 1}, second,
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
  {
    Selection selection = Context();
    Navigation navigation;
    std::optional<InspectorSubject> pending = SignalSubject{"pending"};
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
    pending = SourceSubject{"pattern"};
    RecipeRow other = recipe;
    other.id = "elsewhere";
    Check(!ResolvePendingSubject(navigation, selection, pending, &other) &&
              !pending,
          "a pending subject is dropped when the document changes");
  }
  {
    RecipeRow live = recipe;
    GeometryRow geometry;
    geometry.name = "armor";
    live.geometries.push_back(geometry);
    Selection selected = Context();
    selected.subject = SourceSubject{"pattern"};
    std::optional<PreviewPin> pin = PreviewPin{selected, 4};
    selected.subject = SignalSubject{"strength"};
    live.documentRevision = 7;
    ResolvePreviewPin(pin, selected, &live, 4);
    Check(pin && Is<SourceSubject>(pin->selection.subject),
          "a pinned source survives inspector navigation and value edits");
    InvalidatePreviewPin(pin, "glow");
    Check(pin.has_value(),
          "structural output edits do not invalidate a named resource pin");
    pin->selection.subject = OutputSubject{3};
    InvalidatePreviewPin(pin, "other");
    Check(pin.has_value(), "another recipe cannot invalidate this output pin");
    InvalidatePreviewPin(pin, "glow");
    Check(!pin, "an output pin is cleared before indices can be reassigned");
    pin = PreviewPin{selected, 4};
    ResolvePreviewPin(pin, selected, &live, 5);
    Check(!pin, "load reset clears pins even when document names are reused");
    pin = PreviewPin{selected, 5};
    selected.piece.actorID = 99;
    ResolvePreviewPin(pin, selected, &live, 5);
    Check(!pin, "a preview pin never transfers to another wearer");
    selected = Context();
    selected.subject = SourceSubject{"gone"};
    pin = PreviewPin{selected, 5};
    ResolvePreviewPin(pin, selected, &live, 5);
    Check(!pin, "removed resources do not leave stale preview pins");
  }
  return test::Finish("studio_navigation");
}
