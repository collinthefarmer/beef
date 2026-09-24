#include "engine/RecipeFiles.h"
#include "engine/TextFile.h"
#include "studio/DocumentRevisions.h"
#include "studio/Gesture.h"
#include "studio/History.h"
#include "studio/PaintSession.h"
#include "test_support.h"

#include <filesystem>
#include <string>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
Recipe Fixture() {
  const auto path = test::Fixtures().parent_path().parent_path() / "schema" /
                    "example-magicka.json";
  const auto loaded = ParseRecipe(test::ReadFile(path), "same-id");
  Check(loaded.recipe.has_value(), "editor-state fixture decodes");
  return loaded.recipe.value_or(Recipe{});
}

void ReloadInvalidatesCapturedWork() {
  const Recipe recipe = Fixture();
  DocumentRevisions revisions;
  const auto initial = RevisionOf(revisions, recipe.id);
  const auto untouched = RevisionOf(revisions, "untouched");
  AdvanceRevision(revisions, recipe.id);
  const auto edited = RevisionOf(revisions, recipe.id);
  Check(edited > initial && RevisionOf(revisions, "untouched") == untouched,
        "editing one document does not invalidate another document");
  const auto gesture = BeginRecipeGesture(recipe, edited, edited, "priority");
  Check(gesture.has_value(), "capture a gesture before reload");
  const Recipe paint = PaintRecipe(recipe, RecipeKey{}, Surface::kMaterial);
  PaintCommitRequest request{1, recipe.id, "kept", "0.5"};
  request.assignment = PaintAssignment{0, 0, edited};
  Check(PreparePaintCommit(&paint, &recipe, request, edited).has_value(),
        "paint destination is valid before reload");
  ResetRevisions(revisions);
  const auto reloaded = RevisionOf(revisions, recipe.id);
  Check(
      reloaded > edited && RevisionOf(revisions, "untouched") > untouched &&
          revisions.documents.empty(),
      "reload invalidates edited and untouched IDs without retaining entries");
  Check(CheckEditRevision(edited, reloaded).has_value(),
        "queued indexed edit cannot target the reloaded same-ID document");
  Check(!BeginRecipeGesture(recipe, edited, reloaded, "priority"),
        "queued gesture begin is stale even when recipe contents are equal");
  if (gesture) {
    Check(!PrepareGestureUpdate(*gesture, {recipe, reloaded}, "priority",
                                {{SetPriority{17}}}) &&
              !FinishRecipeGesture(*gesture, recipe, reloaded, false),
          "old gesture update or cancellation cannot overwrite a reloaded "
          "document");
  }
  Check(!PreparePaintCommit(&paint, &recipe, request, reloaded),
        "old paint assignment cannot target a reused recipe ID");
  request.assignment->documentRevision = reloaded;
  Check(PreparePaintCommit(&paint, &recipe, request, reloaded).has_value(),
        "reselecting the layer permits a new paint assignment");
  AdvanceRevision(revisions, recipe.id);
  const auto recreated = RevisionOf(revisions, recipe.id);
  Check(recreated > reloaded && CheckEditRevision(edited, recreated),
        "recreating or advancing the same ID cannot revive an old revision");
  ResetRevisions(revisions);
  Check(RevisionOf(revisions, recipe.id) > recreated,
        "consecutive reloads retain a monotonically advancing epoch");
}

void RevertPreparation(const std::filesystem::path &a_dir) {
  const auto path = a_dir / "revert.json";
  const auto missing = ReadRecipeFile(path, "same-id");
  Check(!missing && missing.error().message.contains(path.string()),
        "missing revert source reports the destination path");
  Check(WriteText(path, "{"), "prepare an interrupted external file");
  const auto malformed = ReadRecipeFile(path, "same-id");
  Check(!malformed && malformed.error().where.contains("same-id"),
        "malformed revert source cannot produce a replacement document");
  Check(WriteText(path, ""), "prepare an empty external file");
  Check(!ReadRecipeFile(path, "same-id"), "empty revert source is refused");
  const Recipe recipe = Fixture();
  Check(WriteRecipeFile(path, recipe, false).has_value(),
        "repair the recipe file after refused revert attempts");
  const auto restored = ReadRecipeFile(path, recipe.id);
  Check(restored && restored->recipe && !restored->HasErrors() &&
            SerializeRecipe(*restored->recipe) == SerializeRecipe(recipe),
        "revert retry returns the complete repaired document");
  Check(WriteText(path,
                  R"({"format":1,"keys":["default"],"priority":2147483648})"),
        "prepare a decodable file with a field error");
  const auto diagnostic = ReadRecipeFile(path, recipe.id);
  Check(diagnostic && diagnostic->recipe && diagnostic->HasErrors() &&
            !diagnostic->inputDiagnostics.empty(),
        "revert preserves decodable input diagnostics instead of treating them "
        "as I/O failure");
}

void GestureSaveHistory(const std::filesystem::path &a_dir) {
  const Recipe original = Fixture();
  Recipe current = original;
  DocumentRevisions revisions;
  EditHistory history;
  const auto revision = RevisionOf(revisions, current.id);
  auto gesture = BeginRecipeGesture(current, revision, revision, "priority");
  if (!gesture) {
    Check(false, "gesture begins on the current revision");
    return;
  }
  for (int value : {17, 23, 29}) {
    const auto update = PrepareGestureUpdate(
        *gesture, {current, RevisionOf(revisions, current.id)}, "priority",
        {{SetPriority{value}}});
    Check(update.has_value(), "tuning update prepares");
    if (!update) {
      return;
    }
    current = update->recipe;
    AdvanceRevision(revisions, current.id);
    gesture->applied = current;
    gesture->revision = RevisionOf(revisions, current.id);
    gesture->editTarget = update->editTarget;
  }
  const auto finish = FinishRecipeGesture(
      *gesture, current, RevisionOf(revisions, current.id), true);
  Check(finish && finish->undo, "save boundary finishes tuning with one undo");
  if (!finish || !finish->undo) {
    return;
  }
  history.Push(*finish->undo);
  const auto path = a_dir / "gesture.json";
  const auto saved = WriteRecipeFile(path, current, false);
  Check(saved && *saved == current && history.UndoDepth() == 1,
        "ordinary save needs no second document change after gesture commit");
  const auto undone = history.Undo(current);
  Check(undone == original, "one undo restores the pre-gesture document");
  AdvanceRevision(revisions, current.id);
  Check(CheckEditRevision(gesture->revision, RevisionOf(revisions, current.id))
            .has_value(),
        "undo invalidates work captured at the saved gesture revision");
  const auto onDisk = ReadRecipeFile(path, current.id);
  Check(onDisk && onDisk->recipe &&
            SerializeRecipe(*onDisk->recipe) == SerializeRecipe(current),
        "undo retains the saved gesture values on disk");
  Check(history.Redo(original) == current,
        "redo restores the exact saved gesture document");
}
}

int main() {
  const auto dir = test::ScratchDir("editor_state");
  ReloadInvalidatesCapturedWork();
  RevertPreparation(dir);
  GestureSaveHistory(dir);
  return test::Finish("editor state");
}
