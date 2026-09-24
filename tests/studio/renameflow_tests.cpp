#include "studio/MenuState.h"
#include "studio/Relationships.h"
#include "test_support.h"

#include <algorithm>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
void ResourceReferences() {
  Recipe recipe;
  recipe.id = "recipe";
  recipe.signals = {{"drive", ConstantSignal{}, {}, {}},
                    {"response", ExprSignal{"@drive"}, {}, {}}};
  recipe.sources = {{"pattern", MaterialSource{}, {}}};
  recipe.masks = {{"coverage", "@pattern", {}}, {"nested", "@coverage", {}}};
  recipe.curves = {{"tone", "x", {}}};
  SurfaceOutput output = DefaultOutput(Surface::kMaterial, Slot::kEmissive);
  Layer layer = DefaultLayer();
  layer.source = Ref{"pattern"};
  layer.opacity = Ref{"drive"};
  layer.mask = Ref{"coverage"};
  layer.curve = CurveRef{"@tone"};
  output.stack = {layer};
  recipe.outputs = {output};
  const Recipe original = recipe;
  EditHistory history;
  const auto rename = [&](const RecipeEdit &a_edit, ResourceKind a_kind,
                          const std::string &a_old, const std::string &a_new) {
    const Recipe before = recipe;
    Check(!Apply(recipe, a_edit), "resource rename accepted");
    history.Push(before);
    const auto links = RelationshipsOf(recipe);
    Check(std::ranges::none_of(links,
                               [&](const Relationship &link) {
                                 return link.driver ==
                                        ResourceRef{a_kind, a_old};
                               }) &&
              std::ranges::any_of(links,
                                  [&](const Relationship &link) {
                                    return link.driver ==
                                           ResourceRef{a_kind, a_new};
                                  }),
          "renaming updates direct and expression references");
  };
  rename(RenameSignal{"drive", "energy"}, ResourceKind::kSignal, "drive",
         "energy");
  rename(RenameSource{"pattern", "texture"}, ResourceKind::kSource, "pattern",
         "texture");
  rename(RenameMask{"coverage", "area"}, ResourceKind::kMask, "coverage",
         "area");
  rename(RenameCurve{"tone", "response_curve"}, ResourceKind::kCurve, "tone",
         "response_curve");
  const Recipe renamed = recipe;
  for (int i = 0; i < 4; ++i) {
    const auto restored = history.Undo(recipe);
    Check(restored.has_value(), "rename can be undone");
    if (restored)
      recipe = *restored;
  }
  Check(recipe == original, "undo restores names and all references");
  for (int i = 0; i < 4; ++i) {
    const auto restored = history.Redo(recipe);
    Check(restored.has_value(), "rename can be redone");
    if (restored)
      recipe = *restored;
  }
  Check(recipe == renamed, "redo restores renamed references");
  const auto parsed = ParseRecipe(SerializeRecipe(recipe), recipe.id);
  Check(parsed.recipe && *parsed.recipe == recipe,
        "renamed recipe round-trips");
  for (const RecipeEdit &edit : std::vector<RecipeEdit>{
           RenameSignal{"energy", "texture"}, RenameSource{"texture", "area"},
           RenameMask{"area", "response_curve"},
           RenameCurve{"response_curve", "energy"},
           RenameSignal{"energy", "1bad"}, RenameSource{"texture", "bad name"},
           RenameMask{"area", ""}, RenameCurve{"response_curve", "bad-name"}}) {
    Check(Apply(recipe, edit).has_value() && recipe == renamed,
          "invalid or duplicate rename preserves the recipe");
  }
}

Snapshot Result(std::uint64_t a_request, bool a_refused) {
  Snapshot snapshot;
  RecipeEditResult result{a_request, "recipe", std::nullopt};
  if (a_refused) {
    result.error = MakeDiagnostic(Severity::kError, "recipe recipe", "refused");
  }
  snapshot.editResults.push_back(std::move(result));
  return snapshot;
}

void ResourceRename(const InspectorSubject &a_from,
                    const InspectorSubject &a_to, const RecipeEdit &a_edit) {
  for (const bool refused : {false, true}) {
    MenuState state;
    state.selection.recipeID = "recipe";
    state.selection.subject = a_from;
    const Intent intent = EditRecipe{"recipe", {a_edit}};
    TrackEditorChange(state, 7, intent);
    Check(state.pendingEditorChange.has_value(), "resource rename is tracked");
    Check(!AcceptIntent(state, intent),
          "pending rename blocks overlapping edits");
    Check(state.selection.subject == a_from && !state.pendingSelection,
          "resource selection waits for acceptance");
    AcknowledgeEditorOperations(state, Result(6, false));
    Check(state.pendingEditorChange.has_value(),
          "unrelated results cannot settle rename");
    AcknowledgeEditorOperations(state, Result(7, refused));
    Check(!state.pendingEditorChange, "rename acknowledgment releases gate");
    Check(refused ? !state.pendingSelection : state.pendingSelection == a_to,
          "only an accepted resource rename follows the new name");
    Check(state.selection.subject == a_from,
          "current subject remains until snapshot navigation");
  }
  MenuState moved;
  moved.selection.recipeID = "recipe";
  moved.selection.subject = a_from;
  TrackEditorChange(moved, 7, EditRecipe{"recipe", {a_edit}});
  moved.selection.subject = RecipeSubject{};
  AcknowledgeEditorOperations(moved, Result(7, false));
  Check(!moved.pendingSelection, "accepted rename does not steal navigation");
}
}

int main() {
  ResourceReferences();
  ResourceRename(SignalSubject{"old"}, SignalSubject{"new"},
                 RenameSignal{"old", "new"});
  ResourceRename(SourceSubject{"old"}, SourceSubject{"new"},
                 RenameSource{"old", "new"});
  ResourceRename(MaskSubject{"old"}, MaskSubject{"new"},
                 RenameMask{"old", "new"});
  ResourceRename(CurveSubject{"old"}, CurveSubject{"new"},
                 RenameCurve{"old", "new"});
  for (const bool refused : {false, true}) {
    MenuState state;
    state.selection.recipeID = "recipe";
    const Intent rename = RenameRecipe{"recipe", "renamed"};
    TrackEditorChange(state, 9, rename);
    Reduce(state, rename);
    Check(state.selection.recipeID == "recipe",
          "recipe selection waits for file rename");
    AcknowledgeEditorOperations(state, Result(9, refused));
    Check(state.selection.recipeID == (refused ? "recipe" : "renamed"),
          "recipe selection changes only after successful rename");
    state = MenuState{};
    state.selection.recipeID = "recipe";
    state.selection.subject = SignalSubject{"old"};
    const Intent remove = DeleteRecipe{"recipe"};
    TrackEditorChange(state, 10, remove);
    Reduce(state, remove);
    Check(Is<SignalSubject>(state.selection.subject),
          "selection waits for file removal");
    AcknowledgeEditorOperations(state, Result(10, refused));
    Check(refused ? Is<SignalSubject>(state.selection.subject)
                  : Is<RecipeSubject>(state.selection.subject),
          "failed delete preserves inspector context");
  }
  return test::Finish("studio rename flow");
}
