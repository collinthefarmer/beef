#include "Identity.h"
#include "engine/Manager.h"
#include "engine/RecipeEditor.h"
#include "engine/RecipeFiles.h"
#include "engine/RecipeStore.h"
#include "engine/TextFile.h"
#include "studio/MenuState.h"
#include "studio/PaintSession.h"
#include "test_support.h"

#include <limits>
#include <stdexcept>
#include <tuple>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
const std::string kID = "integration";

Recipe Current(const std::string &id = kID) {
  const Recipe *recipe = MutableRecipe(id);
  if (!recipe) {
    throw std::runtime_error("integration recipe is not loaded");
  }
  return *recipe;
}

std::pair<std::size_t, std::size_t> Depths(const RecipeEditor &editor,
                                           const std::string &id = kID) {
  const auto *history = editor.HistoryOf(id);
  return history ? std::pair{history->UndoDepth(), history->RedoDepth()}
                 : std::pair<std::size_t, std::size_t>{0, 0};
}

FileOperationResult FileResult(const RecipeEditor &editor, std::uint64_t id) {
  const auto results = editor.FileOperations();
  const auto *found = FindBy(results, id, &FileOperationResult::requestID);
  if (!found) {
    throw std::runtime_error("missing file-operation result");
  }
  return *found;
}

RecipeEditResult EditResult(const RecipeEditor &editor, std::uint64_t id) {
  const auto results = editor.EditResults();
  const auto *found = FindBy(results, id, &RecipeEditResult::requestID);
  if (!found) {
    throw std::runtime_error("missing edit-operation result");
  }
  return *found;
}

std::filesystem::path Start(const std::filesystem::path &root,
                            std::string_view name, const Recipe &recipe) {
  const auto dir = root / name;
  std::filesystem::create_directories(dir);
  std::filesystem::current_path(dir);
  const auto path = Identity::UserRecipeFolder() / (kID + ".json");
  Check(WriteText(path, SerializeRecipe(recipe)), "write integration fixture");
  Check(LoadRecipes().loaded == 1 && !IsDirty(kID),
        "real store loads one clean recipe");
  return path;
}

void Edit(RecipeEditor &editor, Manager &manager, int priority) {
  editor.EditRecipe(kID, {{SetPriority{priority}}});
  manager.Drain();
  Check(Current().priority == priority, "real queued edit applies priority");
}

void FailedRevert(const std::filesystem::path &root, const Recipe &fixture) {
  const auto path = Start(root, "revert", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  Edit(editor, manager, 11);
  Edit(editor, manager, 22);
  editor.UndoRecipe(kID);
  manager.Drain();
  const Recipe before = Current();
  const auto revision = editor.DocumentRevisionOf(kID);
  const auto depths = Depths(editor);
  const auto graph = GraphFor(before);
  Check(IsDirty(kID) && depths == std::pair<std::size_t, std::size_t>{1, 1},
        "refusal starts with a dirty document and both undo and redo");
  for (const std::string text : {"{", "", "missing"}) {
    if (text == "missing") {
      std::filesystem::remove(path);
    } else {
      Check(WriteText(path, text), "install unreadable file");
    }
    const auto request = editor.RevertRecipe(kID);
    Check(FileResult(editor, request).state == FileOperationState::kPending,
          "revert is pending until its real callback runs");
    manager.Drain();
    const auto result = FileResult(editor, request);
    Check(result.state == FileOperationState::kFailed && result.error &&
              result.path == path.string(),
          "failed revert publishes its error and attempted path");
    Check(Current() == before && IsDirty(kID) && Depths(editor) == depths &&
              editor.DocumentRevisionOf(kID) == revision &&
              GraphFor(before) == graph,
          "failed revert preserves document, dirty state, history, revision "
          "and graph");
  }
  editor.UndoRecipe(kID);
  manager.Drain();
  Check(Current() == fixture && !IsDirty(kID),
        "undo after refused revert still reaches the original saved baseline");
  editor.RedoRecipe(kID);
  manager.Drain();
  Check(Current() == before && IsDirty(kID),
        "redo after refusal restores dirty edit");
  Recipe repaired = fixture;
  repaired.priority = 77;
  Check(WriteText(path, SerializeRecipe(repaired)), "repair revert file");
  const auto retry = editor.RevertRecipe(kID);
  manager.Drain();
  Check(FileResult(editor, retry).state == FileOperationState::kSucceeded &&
            Current() == repaired && !IsDirty(kID) &&
            Depths(editor) == std::pair<std::size_t, std::size_t>{2, 0} &&
            editor.DocumentRevisionOf(kID) > revision,
        "successful retry publishes a new baseline and replaces redo with "
        "revert undo");
  editor.UndoRecipe(kID);
  manager.Drain();
  Check(Current() == before && IsDirty(kID),
        "revert undo restores the unsaved edit");
  editor.RedoRecipe(kID);
  manager.Drain();
  Check(Current() == repaired && !IsDirty(kID) &&
            test::ReadFile(path) == SerializeRecipe(repaired),
        "revert redo returns to the saved baseline without rewriting disk");
}

void GestureBoundaries(const std::filesystem::path &root,
                       const Recipe &fixture) {
  const auto path = Start(root, "gesture", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  const auto gesture =
      editor.BeginGesture(kID, editor.DocumentRevisionOf(kID), "priority");
  manager.Drain();
  editor.UpdateGesture(gesture, "priority", {{SetPriority{31}}});
  manager.Drain();
  const auto revision = editor.DocumentRevisionOf(kID);
  std::filesystem::remove(path);
  const auto revert = editor.RevertRecipe(kID);
  manager.Drain();
  const auto status = editor.LastGesture();
  Check(FileResult(editor, revert).state == FileOperationState::kFailed &&
            Current().priority == 31 && IsDirty(kID) &&
            editor.DocumentRevisionOf(kID) == revision &&
            Depths(editor) == std::pair<std::size_t, std::size_t>{1, 0} &&
            status && status->state == GesturePhase::kCommitted &&
            !status->pending,
        "failed revert commits an active gesture but preserves its applied "
        "document");
  editor.UndoRecipe(kID);
  manager.Drain();
  Check(Current() == fixture && !IsDirty(kID),
        "gesture committed by refused revert remains one undo step");
  const auto next =
      editor.BeginGesture(kID, editor.DocumentRevisionOf(kID), "priority");
  editor.UpdateGesture(next, "priority", {{SetPriority{42}}});
  const auto save = editor.SaveRecipe(kID);
  manager.Drain();
  Check(FileResult(editor, save).state == FileOperationState::kSucceeded &&
            Current().priority == 42 && !IsDirty(kID) &&
            Depths(editor) == std::pair<std::size_t, std::size_t>{1, 0},
        "queued gesture update runs before save and commits exactly one undo "
        "step");
  const auto saved = ReadRecipeFile(path, kID);
  Check(saved && saved->recipe == Current(),
        "save persists the final gesture value");
  editor.UndoRecipe(kID);
  manager.Drain();
  Check(Current() == fixture && IsDirty(kID),
        "undo after save differs from saved baseline");
  editor.RedoRecipe(kID);
  manager.Drain();
  Check(Current().priority == 42 && !IsDirty(kID),
        "redo reaches the new saved baseline");
}

void ImportPromotion(const std::filesystem::path &root, const Recipe &fixture) {
  Recipe imported = fixture;
  imported.metadata.imported = "source shader";
  const auto userPath = Start(root, "promotion", imported);
  const auto source = Identity::ImportedRecipeFolder() / (kID + ".json");
  std::filesystem::create_directories(source.parent_path());
  std::filesystem::rename(userPath, source);
  Check(LoadRecipes().loaded == 1, "load from the imported directory");
  Manager manager;
  RecipeEditor editor{manager};
  Edit(editor, manager, 18);
  const Recipe before = Current();
  const auto revision = editor.DocumentRevisionOf(kID);
  const auto depths = Depths(editor);
  const auto staged = std::filesystem::path{userPath.string() + ".writing"};
  std::filesystem::create_directory(staged);
  const auto refused = editor.SaveRecipe(kID);
  manager.Drain();
  const auto origin = OriginOf(Current());
  Check(FileResult(editor, refused).state == FileOperationState::kFailed &&
            Current() == before && IsDirty(kID) && origin &&
            origin->path == source && Depths(editor) == depths &&
            editor.DocumentRevisionOf(kID) == revision &&
            !std::filesystem::exists(userPath),
        "failed promotion preserves document, origin, dirty state, history and "
        "revision");
  std::filesystem::remove(staged);
  const auto saved = editor.SaveRecipe(kID);
  manager.Drain();
  const Recipe promoted = Current();
  const auto destination = OriginOf(promoted);
  Check(FileResult(editor, saved).state == FileOperationState::kSucceeded &&
            FileResult(editor, saved).path == userPath.string() &&
            destination && destination->path == userPath &&
            promoted.metadata.imported.empty() && !IsDirty(kID) &&
            Depths(editor) == std::pair<std::size_t, std::size_t>{2, 0} &&
            editor.DocumentRevisionOf(kID) > revision &&
            test::ReadFile(source) == SerializeRecipe(imported),
        "real save routing promotes into user storage and preserves the "
        "imported source");
  editor.UndoRecipe(kID);
  manager.Drain();
  Check(Current() == before && IsDirty(kID),
        "promotion normalization is undoable");
  editor.RedoRecipe(kID);
  manager.Drain();
  Check(Current() == promoted && !IsDirty(kID),
        "promotion redo reaches saved baseline");
  editor.ReloadRecipes();
  manager.Drain();
  Check(Current() == promoted && !IsDirty(kID) && !editor.HistoryOf(kID),
        "reload selects the promoted user definition over its imported source");
}

void CreateAndDuplicate(const std::filesystem::path &root,
                        const Recipe &fixture) {
  Recipe imported = fixture;
  imported.metadata.imported = "source shader";
  const auto source = Start(root, "create", imported);
  Manager manager;
  RecipeEditor editor{manager};
  const auto created = editor.NewRecipe("new", RecipeKey{}, "ArmorGeometry");
  manager.Drain();
  const auto createdPath = Identity::UserRecipeFolder() / "new.json";
  Check(!EditResult(editor, created).error && IsDirty("new") &&
            !Current("new").outputs.empty() &&
            !std::filesystem::exists(createdPath),
        "create publishes a dirty document without writing until save");
  const auto save = editor.SaveRecipe("new");
  manager.Drain();
  Check(FileResult(editor, save).state == FileOperationState::kSucceeded &&
            !IsDirty("new") && std::filesystem::exists(createdPath),
        "new document saves to its user destination");
  const Recipe originalNew = Current("new");
  const auto collision = editor.NewRecipe("new", RecipeKey{}, {});
  manager.Drain();
  Check(EditResult(editor, collision).error && Current("new") == originalNew,
        "duplicate creation ID refuses without replacing the loaded document");
  const auto duplicate = editor.DuplicateRecipe(kID, "copy");
  manager.Drain();
  const Recipe copy = Current("copy");
  Recipe expected = imported;
  expected.id = "copy";
  expected.metadata.name = "copy";
  expected.metadata.imported.clear();
  Check(!EditResult(editor, duplicate).error && copy == expected &&
            IsDirty("copy") && Current() == imported &&
            !editor.HistoryOf("copy"),
        "duplicate copies content with independent identity and cleared import "
        "metadata");
  const auto refused = editor.DuplicateRecipe(kID, "copy");
  manager.Drain();
  Check(EditResult(editor, refused).error && Current("copy") == copy,
        "duplicate collision preserves the existing copy");
  const auto invalid = editor.NewRecipe("../escape", RecipeKey{}, {});
  manager.Drain();
  Check(EditResult(editor, invalid).error && !MutableRecipe("../escape"),
        "creation refuses a path-like ID");
  editor.NewRecipe("draft", RecipeKey{}, {});
  manager.Drain();
  const auto renamedDraft = editor.RenameRecipe("draft", "draft-renamed");
  manager.Drain();
  Check(!EditResult(editor, renamedDraft).error && !MutableRecipe("draft") &&
            MutableRecipe("draft-renamed") && IsDirty("draft-renamed") &&
            !std::filesystem::exists(Identity::UserRecipeFolder() /
                                     "draft-renamed.json"),
        "unsaved document can be renamed without creating a file");
  const auto deletedDraft = editor.DeleteRecipe("draft-renamed");
  manager.Drain();
  Check(!EditResult(editor, deletedDraft).error &&
            !MutableRecipe("draft-renamed"),
        "unsaved document can be deleted without a source file");
  editor.EditRecipe("copy", {{SetPriority{49}}});
  editor.SaveRecipe("copy");
  manager.Drain();
  editor.ReloadRecipes();
  manager.Drain();
  Check(Current("new") == originalNew && Current("copy").priority == 49 &&
            !IsDirty("copy") && Current() == imported &&
            test::ReadFile(source) == SerializeRecipe(imported),
        "created and duplicate documents reopen independently without changing "
        "source");
}

void RenameAndDelete(const std::filesystem::path &root, const Recipe &fixture) {
  const auto source = Start(root, "rename", fixture);
  const std::string renamed = "renamed";
  const auto target = Identity::UserRecipeFolder() / (renamed + ".json");
  Manager manager;
  RecipeEditor editor{manager};
  Edit(editor, manager, 11);
  Edit(editor, manager, 22);
  editor.UndoRecipe(kID);
  editor.UpdateView([](View &view) {
    view.isolation = Isolation::ForLayer(kID, 0, 0);
    view.muted.insert({kID, 0, 0});
    view.pin = Pin{{1, 2, false}, kID};
  });
  manager.Drain();
  const Recipe before = Current();
  const auto view = editor.CurrentView();
  const auto revision = editor.DocumentRevisionOf(kID);
  const auto destinationRevision = editor.DocumentRevisionOf(renamed);
  Check(WriteText(target, "unloaded destination"),
        "seed rename destination collision");
  const auto refused = editor.RenameRecipe(kID, renamed);
  manager.Drain();
  Check(EditResult(editor, refused).error && Current() == before &&
            Depths(editor) == std::pair<std::size_t, std::size_t>{1, 1} &&
            editor.CurrentView().isolation == view.isolation &&
            editor.CurrentView().pin == view.pin &&
            editor.CurrentView().muted == view.muted &&
            editor.DocumentRevisionOf(kID) == revision &&
            test::ReadFile(target) == "unloaded destination",
        "rename collision preserves history, view, revision and both files");
  std::filesystem::remove(target);
  const auto moved = editor.RenameRecipe(kID, renamed);
  manager.Drain();
  const auto &renamedView = editor.CurrentView();
  Check(!EditResult(editor, moved).error && !MutableRecipe(kID) &&
            Current(renamed).priority == before.priority && IsDirty(renamed) &&
            !std::filesystem::exists(source) &&
            test::ReadFile(target) == SerializeRecipe(fixture) &&
            !editor.HistoryOf(kID) &&
            Depths(editor, renamed) ==
                std::pair<std::size_t, std::size_t>{1, 1} &&
            editor.DocumentRevisionOf(kID) > revision &&
            editor.DocumentRevisionOf(renamed) > destinationRevision &&
            renamedView.isolation.recipeID == renamed && renamedView.pin &&
            renamedView.pin->recipeID == renamed &&
            renamedView.muted.contains({renamed, 0, 0}),
        "rename moves owned file and carries history/view while invalidating "
        "both IDs");
  editor.UndoRecipe(renamed);
  manager.Drain();
  Recipe renamedBaseline = fixture;
  renamedBaseline.id = renamed;
  Check(Current(renamed) == renamedBaseline && !IsDirty(renamed),
        "undo after owned rename reaches the renamed saved baseline");
  editor.RedoRecipe(renamed);
  manager.Drain();
  Check(Current(renamed).priority == 11 && IsDirty(renamed),
        "redo after rename restores the unsaved edit under its new ID");
  const auto deleteRevision = editor.DocumentRevisionOf(renamed);
  std::filesystem::remove(target);
  std::filesystem::create_directory(target);
  Check(WriteText(target / "keep", "blocker"),
        "block deletion with a non-file destination");
  const auto deleteRefused = editor.DeleteRecipe(renamed);
  manager.Drain();
  Check(EditResult(editor, deleteRefused).error && MutableRecipe(renamed) &&
            editor.HistoryOf(renamed) && editor.CurrentView().pin &&
            editor.DocumentRevisionOf(renamed) == deleteRevision,
        "delete refusal keeps the document, history, view and revision");
  std::filesystem::remove(target / "keep");
  std::filesystem::remove(target);
  Check(WriteText(target, SerializeRecipe(fixture)),
        "restore owned file before delete retry");
  const auto deleted = editor.DeleteRecipe(renamed);
  manager.Drain();
  Check(!EditResult(editor, deleted).error && !MutableRecipe(renamed) &&
            !editor.HistoryOf(renamed) && !std::filesystem::exists(target) &&
            editor.CurrentView().RecipeIDs().empty() &&
            editor.DocumentRevisionOf(renamed) > deleteRevision,
        "delete retry removes owned file, history and view references");
  editor.NewRecipe(renamed, RecipeKey{}, {});
  manager.Drain();
  const Recipe recreated = Current(renamed);
  const auto stale =
      editor.EditRecipe(renamed, {{SetPriority{99}}}, deleteRevision);
  manager.Drain();
  Check(EditResult(editor, stale).error && Current(renamed) == recreated,
        "captured work cannot mutate a newly created document reusing the "
        "deleted ID");
}

void CleanRename(const std::filesystem::path &root, const Recipe &fixture) {
  const auto source = Start(root, "clean_rename", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  const auto request = editor.RenameRecipe(kID, "clean");
  manager.Drain();
  const auto path = Identity::UserRecipeFolder() / "clean.json";
  const auto reopened = ReadRecipeFile(path, "clean");
  Check(!EditResult(editor, request).error && !IsDirty("clean") && reopened &&
            reopened->recipe == Current("clean") &&
            !std::filesystem::exists(source),
        "renaming a clean owned recipe remains clean and agrees with the moved "
        "file");
}

void ShippedSaveRouting(const std::filesystem::path &root,
                        const Recipe &fixture) {
  for (const std::string folder : {"vendor/nested", "user-assets", ""}) {
    Recipe shippedRecipe = fixture;
    shippedRecipe.metadata.imported = "shipped provenance";
    const auto userPath =
        Start(root, "save_routing_" + (folder.empty() ? "root" : folder),
              shippedRecipe);
    const auto source = Identity::RecipeRoot() / folder / (kID + ".json");
    std::filesystem::create_directories(source.parent_path());
    std::filesystem::rename(userPath, source);
    Check(LoadRecipes().loaded == 1, "load shipped save source");
    Manager manager;
    RecipeEditor editor{manager};
    Edit(editor, manager, 46);
    const Recipe before = Current();
    const auto revision = editor.DocumentRevisionOf(kID);
    const auto depths = Depths(editor);
    Recipe existing = fixture;
    existing.priority = 88;
    Check(WriteText(userPath, SerializeRecipe(existing)),
          "seed an existing user destination after load");
    const auto staged = std::filesystem::path{userPath.string() + ".writing"};
    std::filesystem::create_directory(staged);
    const auto refused = editor.SaveRecipe(kID);
    manager.Drain();
    const auto origin = OriginOf(Current());
    const auto result = FileResult(editor, refused);
    Check(result.state == FileOperationState::kFailed && result.error &&
              result.error->message.contains(userPath.string()) && origin &&
              origin->path == source && Current() == before && IsDirty(kID) &&
              Depths(editor) == depths &&
              editor.DocumentRevisionOf(kID) == revision &&
              test::ReadFile(source) == SerializeRecipe(shippedRecipe) &&
              test::ReadFile(userPath) == SerializeRecipe(existing),
          "failed shipped save targets user storage and preserves both files "
          "and all authoring state");
    std::filesystem::remove(staged);
    const auto retry = editor.SaveRecipe(kID);
    manager.Drain();
    const auto savedOrigin = OriginOf(Current());
    Check(FileResult(editor, retry).state == FileOperationState::kSucceeded &&
              FileResult(editor, retry).path == userPath.string() &&
              savedOrigin && savedOrigin->path == userPath &&
              Current() == before && !IsDirty(kID) &&
              Depths(editor) == depths &&
              editor.DocumentRevisionOf(kID) == revision &&
              test::ReadFile(source) == SerializeRecipe(shippedRecipe) &&
              test::ReadFile(userPath) == SerializeRecipe(before),
          "retry writes the user override and adopts its origin without adding "
          "a content history step");
    editor.UndoRecipe(kID);
    manager.Drain();
    Check(Current() == shippedRecipe && IsDirty(kID),
          "undo after shipped save compares against the new user baseline");
    editor.RedoRecipe(kID);
    manager.Drain();
    Check(Current() == before && !IsDirty(kID),
          "redo reaches the saved user override");
    Edit(editor, manager, 47);
    editor.RevertRecipe(kID);
    manager.Drain();
    Check(Current() == before && !IsDirty(kID),
          "revert after saving uses the user override origin");
    editor.ReloadRecipes();
    manager.Drain();
    Check(Current() == before && !IsDirty(kID) &&
              test::ReadFile(source) == SerializeRecipe(shippedRecipe),
          "reload selects saved user override and leaves shipped bytes "
          "unchanged");
    editor.DeleteRecipe(kID);
    manager.Drain();
    Check(!std::filesystem::exists(userPath) && std::filesystem::exists(source),
          "delete after shipped save removes only the adopted user file");
    editor.ReloadRecipes();
    manager.Drain();
    Check(
        Current() == shippedRecipe && !IsDirty(kID),
        "removing the user override reveals the unchanged shipped definition");
  }
}

void NestedUserSave(const std::filesystem::path &root, const Recipe &fixture) {
  for (const std::string folder : {"personal", ".personal"}) {
    const auto flat = Start(root, "nested_user_save_" + folder, fixture);
    const auto nested = Identity::UserRecipeFolder() / folder / (kID + ".json");
    std::filesystem::create_directories(nested.parent_path());
    std::filesystem::rename(flat, nested);
    Check(LoadRecipes().loaded == 1, "load nested user-owned recipe");
    Manager manager;
    RecipeEditor editor{manager};
    Edit(editor, manager, 48);
    const auto saved = editor.SaveRecipe(kID);
    manager.Drain();
    const auto origin = OriginOf(Current());
    Check(FileResult(editor, saved).state == FileOperationState::kSucceeded &&
              FileResult(editor, saved).path == nested.string() && origin &&
              origin->path == nested && !std::filesystem::exists(flat) &&
              test::ReadFile(nested) == SerializeRecipe(Current()),
          "existing user-owned files retain their nested destination on save");
  }
}
void ShippedOwnership(const std::filesystem::path &root,
                      const Recipe &fixture) {
  const auto userPath = Start(root, "shipped", fixture);
  const auto shipped = Identity::RecipeRoot() / "vendor" / (kID + ".json");
  std::filesystem::create_directories(shipped.parent_path());
  std::filesystem::rename(userPath, shipped);
  Check(LoadRecipes().loaded == 1, "load shipped source");
  Manager manager;
  RecipeEditor editor{manager};
  const auto rename = editor.RenameRecipe(kID, "personal");
  manager.Drain();
  const auto personalPath = Identity::UserRecipeFolder() / "personal.json";
  Check(!EditResult(editor, rename).error && !MutableRecipe(kID) &&
            IsDirty("personal") && !std::filesystem::exists(personalPath) &&
            test::ReadFile(shipped) == SerializeRecipe(fixture),
        "renaming shipped recipe preserves its file and creates an unsaved "
        "user document");
  editor.SaveRecipe("personal");
  manager.Drain();
  editor.ReloadRecipes();
  manager.Drain();
  Check(Current() == fixture && MutableRecipe("personal") &&
            !IsDirty("personal") && std::filesystem::exists(personalPath),
        "reload restores shipped identity alongside the saved renamed copy");
  const auto deleted = editor.DeleteRecipe(kID);
  manager.Drain();
  Check(
      !EditResult(editor, deleted).error && !MutableRecipe(kID) &&
          test::ReadFile(shipped) == SerializeRecipe(fixture),
      "delete of shipped recipe affects the session but preserves its source");
  Recipe override = fixture;
  override.priority = 88;
  Check(WriteText(userPath, SerializeRecipe(override)),
        "seed same-ID user override");
  editor.ReloadRecipes();
  manager.Drain();
  const auto origin = OriginOf(Current());
  Check(Current() == override && origin && origin->path == userPath,
        "user definition wins over shipped same-ID definition");
  editor.DeleteRecipe(kID);
  manager.Drain();
  Check(!MutableRecipe(kID) && !std::filesystem::exists(userPath) &&
            std::filesystem::exists(shipped),
        "deleting user override removes only the owned file");
  editor.ReloadRecipes();
  manager.Drain();
  Check(Current() == fixture && !IsDirty(kID),
        "reload reveals shipped definition after its override is deleted");
}

std::string PaintText() {
  const Recipe paint = Current(std::string{kPaintRecipe});
  const Mask *mask = paint.FindMask(kScratchMask);
  if (!mask) {
    throw std::runtime_error("paint scratch mask is missing");
  }
  return mask->text;
}

void PaintCommitLifecycle(const std::filesystem::path &root,
                          const Recipe &fixture) {
  const auto path = Start(root, "paint_commit", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  const Isolation isolation = Isolation::ForLayer(kID, 0, 0);
  editor.Isolate(isolation);
  editor.BeginPaint(kID, RecipeKey{}, Surface::kMaterial, 10, 0);
  manager.Drain();
  const auto started = editor.LastPaintUpdate();
  Check(started && started->sessionID == 10 && !started->problem &&
            IsTransient(kPaintRecipe) && !IsDirty(kPaintRecipe) &&
            Current() == fixture && !IsDirty(kID) &&
            editor.CurrentView().isolation.recipeID == kPaintRecipe,
        "paint begin publishes a transient preview without editing its "
        "destination");
  PaintUpdateRequest update{10, 1, "@paintmetal * @paintrough", {}, {}};
  update.sources = {
      AddSource{"paintmetal", MaterialSource{MaterialChannel::kMetallic}},
      AddSource{"paintrough", MaterialSource{MaterialChannel::kRoughness}}};
  update.peek = "0.75";
  editor.UpdatePaint(update);
  manager.Drain();
  Check(editor.LastPaintUpdate() && !editor.LastPaintUpdate()->problem &&
            PaintText() == update.expression && Current() == fixture &&
            !IsDirty(kID) && editor.HistoryOf(std::string{kPaintRecipe}),
        "paint update applies scratch and dependencies only to the transient "
        "document");
  const auto refusedSave = editor.SaveRecipe(std::string{kPaintRecipe});
  manager.Drain();
  Check(FileResult(editor, refusedSave).state == FileOperationState::kFailed &&
            !std::filesystem::exists(Identity::UserRecipeFolder() /
                                     "paint.json") &&
            IsTransient(kPaintRecipe),
        "saving the preview is refused without ending the paint session");
  PaintCommitRequest keep{101, kID, "painted", update.expression, 10};
  keep.sources = update.sources;
  keep.assignment = PaintAssignment{0, 0, editor.DocumentRevisionOf(kID)};
  editor.KeepPaint(keep);
  editor.KeepPaint(keep);
  manager.Drain();
  const Recipe committed = Current();
  const auto *output = committed.outputs.empty()
                           ? nullptr
                           : Get<SurfaceOutput>(committed.outputs.front());
  Check(
      editor.LastPaintCommit() && editor.LastPaintCommit()->requestID == 101 &&
          !editor.LastPaintCommit()->problem && committed.FindMask("painted") &&
          committed.FindSource("paintrough") &&
          !committed.FindSource("paintmetal") &&
          committed.FindMask("painted")->text == "@metallic * @paintrough" &&
          output && !output->stack.empty() &&
          output->stack.front().mask == Ref{"painted"} && IsDirty(kID) &&
          Depths(editor) == std::pair<std::size_t, std::size_t>{1, 0},
      "Keep atomically adds sources, mask and assignment with one undo despite "
      "duplicate delivery");
  Check(!MutableRecipe(kPaintRecipe) &&
            !editor.HistoryOf(std::string{kPaintRecipe}) &&
            editor.CurrentView().isolation == isolation &&
            test::ReadFile(path) == SerializeRecipe(fixture),
        "Keep drops preview history and restores isolation without saving the "
        "destination");
  editor.UndoRecipe(kID);
  manager.Drain();
  Check(Current() == fixture && !IsDirty(kID),
        "one undo removes the complete paint commit");
  editor.RedoRecipe(kID);
  manager.Drain();
  Check(Current() == committed && IsDirty(kID),
        "redo restores the complete paint commit");
  editor.SaveRecipe(kID);
  editor.ReloadRecipes();
  manager.Drain();
  Check(Current() == committed && !IsDirty(kID) &&
            !MutableRecipe(kPaintRecipe) && !Current().FindMask(kScratchMask) &&
            !Current().FindMask(kPeekMask),
        "paint commit saves and reloads without transient masks or recipes");
}

void PaintDiscardAndStaleWork(const std::filesystem::path &root,
                              const Recipe &fixture) {
  Start(root, "paint_discard", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  const Isolation isolation = Isolation::ForRecipe(kID);
  editor.Isolate(isolation);
  editor.BeginPaint(kID, RecipeKey{}, Surface::kMaterial, 20, 0);
  editor.UpdatePaint({20, 1, "0.25", {}, {}});
  manager.Drain();
  const Recipe before = Current(std::string{kPaintRecipe});
  PaintUpdateRequest invalid{20, 2, "0.75", {}, {}};
  invalid.sources = {SetPriority{900}};
  editor.UpdatePaint(invalid);
  manager.Drain();
  Check(
      editor.LastPaintUpdate() && editor.LastPaintUpdate()->problem &&
          editor.LastPaintUpdate()->revision == 2 &&
          Current(std::string{kPaintRecipe}) == before && Current() == fixture,
      "refused preview update retains both preview and destination documents");
  editor.UpdatePaint({20, 3, "0.5", {}, {}});
  editor.UpdatePaint({20, 1, "0.9", {}, {}});
  manager.Drain();
  Check(PaintText() == "0.5" && editor.LastPaintUpdate() &&
            editor.LastPaintUpdate()->revision == 3 &&
            !editor.LastPaintUpdate()->problem,
        "newer preview update recovers from refusal and older delivery cannot "
        "overwrite it");
  editor.BeginPaint(kID, RecipeKey{}, Surface::kShell, 21, 0);
  editor.UpdatePaint({21, 1, "0.6", {}, {}});
  manager.Drain();
  const Recipe replacement = Current(std::string{kPaintRecipe});
  editor.UpdatePaint({20, 99, "0.9", {}, {}});
  editor.EndPaint(20);
  editor.KeepPaint({201, kID, "stale", "1", 20});
  manager.Drain();
  Check(Current(std::string{kPaintRecipe}) == replacement &&
            Current() == fixture && !editor.LastPaintCommit() &&
            editor.LastPaintUpdate() &&
            editor.LastPaintUpdate()->sessionID == 21,
        "old session update, discard and Keep cannot alter a replacement "
        "session");
  editor.EndPaint(21);
  manager.Drain();
  Check(
      !MutableRecipe(kPaintRecipe) &&
          !editor.HistoryOf(std::string{kPaintRecipe}) &&
          Current() == fixture && !IsDirty(kID) && !editor.HistoryOf(kID) &&
          editor.CurrentView().isolation == isolation,
      "discard removes only transient state and restores pre-paint isolation");
  editor.UpdatePaint({21, 2, "1", {}, {}});
  editor.KeepPaint({202, kID, "late", "1", 21});
  manager.Drain();
  Check(!MutableRecipe(kPaintRecipe) && !Current().FindMask("late") &&
            !editor.LastPaintCommit(),
        "ended session cannot be revived by late work");
  editor.BeginPaint("missing", RecipeKey{}, Surface::kMaterial, 22, 0);
  manager.Drain();
  Check(editor.LastPaintUpdate() && editor.LastPaintUpdate()->problem &&
            !MutableRecipe(kPaintRecipe) && Current() == fixture,
        "begin on an absent destination publishes a refusal without creating a "
        "preview");
}

void PaintDestinationChanges(const std::filesystem::path &root,
                             const Recipe &fixture) {
  Start(root, "paint_destination", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  editor.Isolate(Isolation::ForRecipe(kID));
  editor.BeginPaint(kID, RecipeKey{}, Surface::kMaterial, 30, 0);
  editor.UpdatePaint({30, 1, "0.25", {}, {}});
  manager.Drain();
  PaintCommitRequest keep{301, kID, "painted", "0.25", 30};
  keep.assignment = PaintAssignment{0, 0, editor.DocumentRevisionOf(kID)};
  Edit(editor, manager, 18);
  const Recipe edited = Current();
  const auto history = Depths(editor);
  const auto revision = editor.DocumentRevisionOf(kID);
  editor.KeepPaint(keep);
  manager.Drain();
  Check(editor.LastPaintCommit() && editor.LastPaintCommit()->problem &&
            Current() == edited && Depths(editor) == history &&
            editor.DocumentRevisionOf(kID) == revision &&
            IsTransient(kPaintRecipe),
        "stale assignment refuses atomically and retains the draft for retry");
  keep.id = 302;
  keep.assignment->documentRevision = revision;
  editor.KeepPaint(keep);
  manager.Drain();
  Check(editor.LastPaintCommit() && !editor.LastPaintCommit()->problem &&
            Current().priority == 18 && Current().FindMask("painted") &&
            Depths(editor).first == history.first + 1 &&
            !MutableRecipe(kPaintRecipe),
        "reselected assignment retries as one edit without discarding "
        "unrelated changes");
  editor.BeginPaint(kID, RecipeKey{}, Surface::kMaterial, 31, 0);
  editor.UpdatePaint({31, 1, "0.75", {}, {}});
  manager.Drain();
  editor.RenameRecipe(kID, "renamed-paint");
  manager.Drain();
  editor.KeepPaint({303, kID, "renamedmask", "0.75", 31});
  manager.Drain();
  Check(editor.LastPaintCommit() && editor.LastPaintCommit()->problem &&
            IsTransient(kPaintRecipe) &&
            !Current("renamed-paint").FindMask("renamedmask"),
        "Keep addressed to the old destination ID refuses after rename");
  editor.KeepPaint({304, "renamed-paint", "renamedmask", "0.75", 31});
  manager.Drain();
  Check(editor.LastPaintCommit() && !editor.LastPaintCommit()->problem &&
            Current("renamed-paint").FindMask("renamedmask") &&
            editor.CurrentView().isolation.recipeID == "renamed-paint",
        "Keep can follow the renamed destination and restore its renamed "
        "isolation");
  editor.BeginPaint("renamed-paint", RecipeKey{}, Surface::kMaterial, 32, 0);
  editor.DeleteRecipe("renamed-paint");
  editor.KeepPaint({305, "renamed-paint", "deletedmask", "1", 32});
  manager.Drain();
  Check(editor.LastPaintCommit() && editor.LastPaintCommit()->problem &&
            !MutableRecipe("renamed-paint") && IsTransient(kPaintRecipe),
        "deleted destination refuses Keep without recreating a document or "
        "losing its draft");
  editor.EndPaint(32);
  manager.Drain();
  Check(!MutableRecipe(kPaintRecipe) &&
            editor.CurrentView().RecipeIDs().empty(),
        "discard after destination deletion clears obsolete return isolation");
}

void PaintReservedNames(const std::filesystem::path &root,
                        const Recipe &fixture) {
  for (const std::string name : {"scratch", "peek", "bad name"}) {
    Start(root, "paint_reserved_" + name, fixture);
    Manager manager;
    RecipeEditor editor{manager};
    editor.BeginPaint(kID, RecipeKey{}, Surface::kMaterial, 50, 0);
    editor.UpdatePaint({50, 1, "0.5", {}, {}});
    editor.KeepPaint({501, kID, name, "0.5", 50});
    manager.Drain();
    Check(editor.LastPaintCommit() && editor.LastPaintCommit()->problem &&
              Current() == fixture && !IsDirty(kID) && !editor.HistoryOf(kID) &&
              IsTransient(kPaintRecipe),
          "reserved or invalid Keep name refuses without losing draft or "
          "changing destination");
    editor.KeepPaint({502, kID, "kept", "0.5", 50});
    manager.Drain();
    Check(editor.LastPaintCommit() && !editor.LastPaintCommit()->problem &&
              Current().FindMask("kept") && !MutableRecipe(kPaintRecipe),
          "a valid name retries the refused Keep and completes the session");
  }
}

void PaintLoadCancellation(const std::filesystem::path &root,
                           const Recipe &fixture) {
  Start(root, "paint_load", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  editor.Isolate(Isolation::ForRecipe(kID));
  editor.BeginPaint(kID, RecipeKey{}, Surface::kMaterial, 40, 0);
  editor.UpdatePaint({40, 1, "0.25", {}, {}});
  manager.Drain();
  editor.UpdatePaint({40, 2, "0.75", {}, {}});
  editor.KeepPaint({401, kID, "canceled", "0.75", 40});
  manager.queue.BeginLoad();
  editor.CancelFileOperationsForLoad();
  editor.CancelPaintForLoad();
  const auto reset = editor.LastPaintUpdate();
  Check(reset && reset->ended && reset->revision > 0 &&
            !editor.LastPaintCommit() && !MutableRecipe(kPaintRecipe) &&
            !editor.HistoryOf(std::string{kPaintRecipe}) &&
            Current() == fixture && !IsDirty(kID),
        "load cancellation drops preview and publishes the paint reset "
        "immediately");
  manager.queue.Resume();
  manager.Drain();
  editor.BeginPaint(kID, RecipeKey{}, Surface::kMaterial, 41, 0);
  manager.Drain();
  Check(!MutableRecipe(kPaintRecipe) && Current() == fixture &&
            editor.LastPaintUpdate() && editor.LastPaintUpdate()->ended,
        "old queued work and a begin with an old reset token cannot revive "
        "painting");
  editor.BeginPaint(kID, RecipeKey{}, Surface::kMaterial, 42,
                    reset ? reset->revision : 0);
  editor.UpdatePaint({42, 1, "0.5", {}, {}});
  manager.Drain();
  Check(IsTransient(kPaintRecipe) && PaintText() == "0.5" &&
            editor.LastPaintUpdate() && !editor.LastPaintUpdate()->ended,
        "fresh reset token permits a new paint session after load");
  editor.ReloadRecipes();
  manager.Drain();
  Check(!MutableRecipe(kPaintRecipe) && !editor.LastPaintCommit() &&
            editor.LastPaintUpdate() && editor.LastPaintUpdate()->ended &&
            Current() == fixture && !editor.HistoryOf(kID),
        "recipe reload also ends painting and clears draft state");
}

std::vector<std::tuple<Severity, std::string, std::string>>
PublishedDiagnostics() {
  const auto origin = OriginOf(Current());
  if (!origin) {
    throw std::runtime_error("integration recipe origin is missing");
  }
  std::vector<std::tuple<Severity, std::string, std::string>> result;
  for (const Diagnostic &diagnostic : origin->diagnostics) {
    result.emplace_back(diagnostic.severity, diagnostic.where,
                        diagnostic.message);
  }
  return result;
}

void RejectedAndNoOpEdits(const std::filesystem::path &root,
                          const Recipe &fixture) {
  Start(root, "edit_validation", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  Edit(editor, manager, 11);
  Edit(editor, manager, 22);
  editor.UndoRecipe(kID);
  manager.Drain();
  const Recipe before = Current();
  const auto depths = Depths(editor);
  const auto revision = editor.DocumentRevisionOf(kID);
  const auto graph = GraphFor(before);
  const auto diagnostics = PublishedDiagnostics();
  const std::vector<EditBatch> invalid{
      {{SetPriority{99}, RemoveOutput{999}}},
      {{SetPriority{99},
        SetClockSpeed{std::numeric_limits<float>::infinity()}}}};
  for (const EditBatch &batch : invalid) {
    const auto request = editor.EditRecipe(kID, batch);
    manager.Drain();
    const auto result = EditResult(editor, request);
    Check(result.error && result.error->severity == Severity::kError &&
              !result.error->where.empty() && !result.error->message.empty(),
          "invalid queued batch returns a located error to the UI");
    Check(Current() == before && IsDirty(kID) && Depths(editor) == depths &&
              editor.DocumentRevisionOf(kID) == revision &&
              GraphFor(before) == graph &&
              PublishedDiagnostics() == diagnostics,
          "refused batch preserves document, dirty state, undo/redo, revision "
          "and derived state");
  }
  const std::vector<EditBatch> noOps{
      {}, {{SetPriority{11}}}, {{SetPriority{99}, SetPriority{11}}}};
  for (const EditBatch &batch : noOps) {
    const auto request = editor.EditRecipe(kID, batch);
    manager.Drain();
    Check(!EditResult(editor, request).error && Current() == before &&
              IsDirty(kID) && Depths(editor) == depths &&
              editor.DocumentRevisionOf(kID) == revision &&
              GraphFor(before) == graph &&
              PublishedDiagnostics() == diagnostics,
          "empty, identical and net-zero edits acknowledge success without "
          "consuming redo");
  }
  editor.RedoRecipe(kID);
  manager.Drain();
  Check(Current().priority == 22 && Depths(editor).second == 0,
        "redo still restores the previously undone document after refusals and "
        "no-ops");
  editor.UndoRecipe(kID);
  manager.Drain();
  Edit(editor, manager, 33);
  Check(Depths(editor).second == 0,
        "a genuinely changed document clears the previous redo branch");
}

bool PublishedForApplication() {
  const auto loaded = LoadedRecipes();
  return FindById(loaded, kID) != nullptr;
}

void InputDiagnosticLifecycle(const std::filesystem::path &root,
                              const Recipe &fixture) {
  const auto path = Start(root, "input_diagnostics", fixture);
  const std::string invalid = R"({"format":1,"keys":["default"],"clock":null})";
  Check(WriteText(path, invalid),
        "write decodable file with a clock input error");
  Check(LoadRecipes().heldBack == 1 && LoadedRecipes().empty(),
        "input-invalid recipe is editable but excluded from the applied set");
  Manager manager;
  RecipeEditor editor{manager};
  const auto diagnostics = PublishedDiagnostics();
  Check(!diagnostics.empty(), "store exposes retained input diagnostics");
  Edit(editor, manager, 17);
  Check(PublishedDiagnostics() == diagnostics && LoadedRecipes().empty() &&
            IsDirty(kID),
        "ordinary edit retains input diagnostics and held-back publication");
  const auto depths = Depths(editor);
  const auto revision = editor.DocumentRevisionOf(kID);
  const auto staged = std::filesystem::path{path.string() + ".writing"};
  std::filesystem::create_directory(staged);
  const auto refused = editor.SaveRecipe(kID);
  manager.Drain();
  Check(FileResult(editor, refused).state == FileOperationState::kFailed &&
            PublishedDiagnostics() == diagnostics && LoadedRecipes().empty() &&
            IsDirty(kID) && Depths(editor) == depths &&
            editor.DocumentRevisionOf(kID) == revision &&
            test::ReadFile(path) == invalid,
        "failed save retains input diagnostics, applied exclusion and old file "
        "bytes");
  std::filesystem::remove(staged);
  const auto saved = editor.SaveRecipe(kID);
  manager.Drain();
  Check(FileResult(editor, saved).state == FileOperationState::kSucceeded &&
            PublishedDiagnostics().empty() && !IsDirty(kID) &&
            PublishedForApplication() && Depths(editor) == depths &&
            editor.DocumentRevisionOf(kID) == revision,
        "normalized save clears input diagnostics and republishes without "
        "inventing a document edit");
  editor.UndoRecipe(kID);
  manager.Drain();
  Check(PublishedDiagnostics().empty() && IsDirty(kID) &&
            PublishedForApplication(),
        "undo changes the document but does not resurrect errors from replaced "
        "file bytes");
  editor.ReloadRecipes();
  manager.Drain();
  Check(Current().priority == 17 && !IsDirty(kID) &&
            PublishedDiagnostics().empty(),
        "reload of normalized file stays clean and diagnostic-free");
  Check(WriteText(path, invalid),
        "restore invalid file for revert publication");
  const auto reverted = editor.RevertRecipe(kID);
  manager.Drain();
  Check(FileResult(editor, reverted).state == FileOperationState::kSucceeded &&
            PublishedDiagnostics() == diagnostics && LoadedRecipes().empty() &&
            !IsDirty(kID),
        "decodable invalid revert succeeds as a clean document with visible "
        "errors and applied exclusion");
  editor.ReloadRecipes();
  manager.Drain();
  Check(PublishedDiagnostics() == diagnostics && LoadedRecipes().empty(),
        "reload retains diagnostics while the invalid bytes remain on disk");
  Check(WriteText(path, SerializeRecipe(Current())),
        "repair input file externally");
  editor.ReloadRecipes();
  manager.Drain();
  Check(PublishedDiagnostics().empty() && PublishedForApplication() &&
            !IsDirty(kID),
        "reload clears old diagnostics after the file itself is repaired");
}

void SemanticDiagnosticLifecycle(const std::filesystem::path &root) {
  Recipe fixture;
  fixture.id = kID;
  fixture.keys = {RecipeKey{}};
  ImageSource image;
  image.path = "textures/test.dds";
  fixture.sources = {Source{"image", image, {}}};
  Start(root, "semantic_diagnostics", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  const auto incomplete =
      editor.EditRecipe(kID, {{SetSource{"image", ImageSource{}}}});
  manager.Drain();
  Check(!EditResult(editor, incomplete).error &&
            !PublishedDiagnostics().empty() && IsDirty(kID),
        "incomplete source draft is accepted with persistent row diagnostics");
  const auto problems = PublishedDiagnostics();
  editor.SaveRecipe(kID);
  manager.Drain();
  Check(!IsDirty(kID) && PublishedDiagnostics() == problems,
        "successful save does not erase unresolved semantic diagnostics");
  editor.ReloadRecipes();
  manager.Drain();
  Check(
      PublishedDiagnostics() == problems,
      "semantic diagnostics are reconstructed from the saved draft on reload");
  const auto fixed = editor.EditRecipe(kID, {{SetSource{"image", image}}});
  manager.Drain();
  Check(!EditResult(editor, fixed).error && PublishedDiagnostics().empty() &&
            IsDirty(kID),
        "repairing a semantic draft clears diagnostics before saving");
  editor.UndoRecipe(kID);
  manager.Drain();
  Check(
      PublishedDiagnostics() == problems && !IsDirty(kID),
      "undo restores semantic diagnostics with the saved incomplete document");
  editor.RedoRecipe(kID);
  manager.Drain();
  Check(PublishedDiagnostics().empty() && IsDirty(kID),
        "redo repairs semantic diagnostics again");
}

void GestureValidationRecovery(const std::filesystem::path &root,
                               const Recipe &fixture) {
  Start(root, "gesture_validation", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  Edit(editor, manager, 17);
  editor.UndoRecipe(kID);
  manager.Drain();
  const auto initialRevision = editor.DocumentRevisionOf(kID);
  const auto depths = Depths(editor);
  const auto gesture = editor.BeginGesture(kID, initialRevision, "clock.speed");
  manager.Drain();
  editor.UpdateGesture(
      gesture, "clock.speed",
      {{SetClockSpeed{std::numeric_limits<float>::infinity()}}});
  manager.Drain();
  Check(editor.LastGesture() && editor.LastGesture()->error &&
            editor.LastGesture()->state == GesturePhase::kActive &&
            !editor.LastGesture()->pending && Current() == fixture &&
            !IsDirty(kID) && Depths(editor) == depths &&
            editor.DocumentRevisionOf(kID) == initialRevision,
        "invalid gesture update publishes an error without changing document "
        "or redo");
  editor.UpdateGesture(gesture, "clock.speed", {{SetClockSpeed{2.0f}}});
  manager.Drain();
  const Recipe valid = Current();
  const auto revision = editor.DocumentRevisionOf(kID);
  Check(editor.LastGesture() && !editor.LastGesture()->error &&
            valid.clock.speed == 2.0f && IsDirty(kID) &&
            revision > initialRevision && Depths(editor) == depths,
        "valid gesture update clears its error and changes the document before "
        "history commit");
  editor.UpdateGesture(gesture, "clock.speed", {{SetPriority{99}}});
  manager.Drain();
  Check(editor.LastGesture() && editor.LastGesture()->error &&
            Current() == valid && editor.DocumentRevisionOf(kID) == revision &&
            Depths(editor) == depths,
        "gesture refuses switching its edit target without losing the last "
        "valid value");
  editor.UpdateGesture(gesture, "clock.speed", {{SetClockSpeed{3.0f}}});
  manager.Drain();
  editor.EndGesture(gesture, true);
  manager.Drain();
  Check(editor.LastGesture() && !editor.LastGesture()->error &&
            editor.LastGesture()->state == GesturePhase::kCommitted &&
            !editor.LastGesture()->pending && Current().clock.speed == 3.0f &&
            Depths(editor) == std::pair<std::size_t, std::size_t>{1, 0},
        "recovered gesture commits its final value as one undo step and clears "
        "old redo");
  editor.UndoRecipe(kID);
  manager.Drain();
  Check(Current() == fixture && !IsDirty(kID),
        "recovered gesture undo restores the original document");
  const auto cancel =
      editor.BeginGesture(kID, editor.DocumentRevisionOf(kID), "clock.speed");
  editor.UpdateGesture(cancel, "clock.speed", {{SetClockSpeed{4.0f}}});
  manager.Drain();
  editor.EndGesture(cancel, false);
  manager.Drain();
  Check(Current() == fixture && !IsDirty(kID) &&
            Depths(editor) == std::pair<std::size_t, std::size_t>{0, 1} &&
            editor.LastGesture() &&
            editor.LastGesture()->state == GesturePhase::kCanceled,
        "explicit gesture cancellation restores the original document and "
        "preserves redo");
}

Snapshot EditorResults(const RecipeEditor &editor) {
  Snapshot snapshot;
  snapshot.editResults = editor.EditResults();
  snapshot.fileOperations = editor.FileOperations();
  snapshot.paintUpdate = editor.LastPaintUpdate();
  snapshot.paintCommit = editor.LastPaintCommit();
  return snapshot;
}

void AcknowledgeResults(MenuState &state, const Snapshot &snapshot) {
  if (snapshot.paintUpdate) {
    AcknowledgePaintUpdate(state, *snapshot.paintUpdate);
  }
  if (snapshot.paintCommit) {
    AcknowledgePaintCommit(state, *snapshot.paintCommit);
  }
  AcknowledgeEditorOperations(state, snapshot);
}

void RecipeAcknowledgements(const std::filesystem::path &root,
                            const Recipe &fixture) {
  Start(root, "recipe_ack", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  MenuState state;
  state.selection.recipeID = kID;
  state.selection.subject = MaskSubject{"metal"};
  const auto collision = Identity::UserRecipeFolder() / "renamed.json";
  Check(WriteText(collision, "existing destination"), "seed UI rename refusal");
  const Intent rename = Studio::RenameRecipe{kID, "renamed"};
  const auto refused = editor.RenameRecipe(kID, "renamed");
  TrackEditorChange(state, refused, rename);
  Reduce(state, rename);
  AcknowledgeResults(state, EditorResults(editor));
  Check(state.pendingEditorChange && state.selection.recipeID == kID &&
            !AcceptIntent(state, Studio::EditRecipe{kID, {SetPriority{1}}}) &&
            AcceptIntent(state, PickRecipe{"other", true}),
        "pending real rename blocks mutations while allowing navigation and "
        "retaining selection");
  manager.Drain();
  const auto refusalSnapshot = EditorResults(editor);
  AcknowledgeResults(state, refusalSnapshot);
  Check(!state.pendingEditorChange && state.selection.recipeID == kID &&
            state.selection.subject == InspectorSubject{MaskSubject{"metal"}} &&
            EditResult(editor, refused).error,
        "real rename refusal releases the gate and retains inspector context "
        "and its error");
  std::filesystem::remove(collision);
  const auto retry = editor.RenameRecipe(kID, "renamed");
  TrackEditorChange(state, retry, rename);
  AcknowledgeResults(state, refusalSnapshot);
  Check(state.pendingEditorChange &&
            state.pendingEditorChange->requestID == retry,
        "a previously published refusal cannot acknowledge the rename retry");
  manager.Drain();
  const auto successSnapshot = EditorResults(editor);
  AcknowledgeResults(state, successSnapshot);
  Check(!state.pendingEditorChange && state.selection.recipeID == "renamed" &&
            MutableRecipe("renamed"),
        "accepted rename follows the real new document identity");
  const Intent secondRename = Studio::RenameRecipe{"renamed", "again"};
  const auto second = editor.RenameRecipe("renamed", "again");
  TrackEditorChange(state, second, secondRename);
  Reduce(state, PickRecipe{"other", true});
  state.selection.subject = SignalSubject{"other_signal"};
  state.navigation.scroll = 77.0f;
  state.navigation.back.push_back({state.selection, 12.0f});
  state.navigation.forward.push_back({state.selection, 25.0f});
  manager.Drain();
  AcknowledgeResults(state, EditorResults(editor));
  Check(state.selection.recipeID == "other" &&
            state.selection.subject ==
                InspectorSubject{SignalSubject{"other_signal"}} &&
            state.navigation.scroll == 77.0f &&
            state.navigation.back.size() == 1 &&
            state.navigation.forward.size() == 1,
        "rename acknowledgement cannot reset navigation after the user visits "
        "another document");
  state.selection.recipeID = "again";
  state.selection.subject = MaskSubject{"metal"};
  const Intent remove = Studio::DeleteRecipe{"again"};
  const auto deleted = editor.DeleteRecipe("again");
  TrackEditorChange(state, deleted, remove);
  AcknowledgeResults(state, successSnapshot);
  Check(state.pendingEditorChange && Is<MaskSubject>(state.selection.subject),
        "old rename result cannot settle a pending delete");
  manager.Drain();
  AcknowledgeResults(state, EditorResults(editor));
  Check(!state.pendingEditorChange &&
            Is<RecipeSubject>(state.selection.subject) &&
            !MutableRecipe("again"),
        "accepted deletion releases its gate and resets the deleted inspector");
}

void ResourceAcknowledgements(const std::filesystem::path &root,
                              const Recipe &fixture) {
  Start(root, "resource_ack", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  MenuState state;
  state.selection.recipeID = kID;
  state.selection.subject = MaskSubject{"metal"};
  const Intent invalid =
      Studio::EditRecipe{kID, {RenameMask{"metal", "bad name"}}};
  const auto refused =
      editor.EditRecipe(kID, {{RenameMask{"metal", "bad name"}}});
  TrackEditorChange(state, refused, invalid);
  manager.Drain();
  AcknowledgeResults(state, EditorResults(editor));
  Check(!state.pendingEditorChange && !state.pendingSelection &&
            state.selection.subject == InspectorSubject{MaskSubject{"metal"}} &&
            EditResult(editor, refused).error,
        "refused real resource rename retains its selected subject");
  const Intent rename =
      Studio::EditRecipe{kID, {RenameMask{"metal", "coverage"}}};
  const auto request =
      editor.EditRecipe(kID, {{RenameMask{"metal", "coverage"}}});
  TrackEditorChange(state, request, rename);
  manager.Drain();
  const auto accepted = EditorResults(editor);
  AcknowledgeResults(state, accepted);
  Check(!state.pendingEditorChange &&
            state.pendingSelection ==
                InspectorSubject{MaskSubject{"coverage"}} &&
            Current().FindMask("coverage") && !Current().FindMask("metal"),
        "actual resource rename result schedules the newly named inspector "
        "subject");
  state.pendingSelection.reset();
  state.selection.subject = MaskSubject{"coverage"};
  const auto second =
      editor.EditRecipe(kID, {{RenameMask{"coverage", "coating"}}});
  TrackEditorChange(
      state, second,
      Studio::EditRecipe{kID, {RenameMask{"coverage", "coating"}}});
  state.selection.subject = RecipeSubject{};
  manager.Drain();
  AcknowledgeResults(state, EditorResults(editor));
  Check(!state.pendingSelection && Is<RecipeSubject>(state.selection.subject) &&
            Current().FindMask("coating"),
        "accepted resource rename does not steal later inspector navigation");
  const auto invalidEdit = editor.EditRecipe(kID, {{RemoveOutput{999}}});
  state.pendingIndexedEdit = PendingIndexedEdit{invalidEdit, kID};
  AcknowledgeResults(state, accepted);
  Check(state.pendingIndexedEdit.has_value(),
        "old resource result cannot release an indexed edit gate");
  manager.Drain();
  AcknowledgeResults(state, EditorResults(editor));
  const auto error = EditResult(editor, invalidEdit).error;
  Check(!state.pendingIndexedEdit && error && !ProblemText(error).empty(),
        "actual invalid indexed edit releases its pending gate and retains "
        "displayable error text");
}

void FileAcknowledgements(const std::filesystem::path &root,
                          const Recipe &fixture) {
  const auto path = Start(root, "file_ack", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  MenuState state;
  state.selection.recipeID = kID;
  Edit(editor, manager, 28);
  const auto staged = std::filesystem::path{path.string() + ".writing"};
  std::filesystem::create_directory(staged);
  const auto save = editor.SaveRecipe(kID);
  state.pendingRecipeFile = PendingIndexedEdit{save, kID};
  AcknowledgeResults(state, EditorResults(editor));
  Check(state.pendingRecipeFile && !AcceptIntent(state, Studio::Undo{kID}),
        "pending file result leaves conflicting mutations gated");
  manager.Drain();
  const auto failed = EditorResults(editor);
  AcknowledgeResults(state, failed);
  Check(!state.pendingRecipeFile && FileResult(editor, save).error &&
            IsDirty(kID),
        "failed save releases UI gate while preserving its error and unsaved "
        "state");
  std::filesystem::remove(staged);
  const auto retry = editor.SaveRecipe(kID);
  state.pendingRecipeFile = PendingIndexedEdit{retry, kID};
  AcknowledgeResults(state, failed);
  Check(state.pendingRecipeFile.has_value(),
        "old failed save cannot acknowledge a new file request");
  Reduce(state, PickRecipe{"other", true});
  manager.Drain();
  AcknowledgeResults(state, EditorResults(editor));
  Check(!state.pendingRecipeFile && state.selection.recipeID == "other" &&
            !IsDirty(kID),
        "successful save acknowledgement preserves later navigation");
  const auto pending = editor.RevertRecipe(kID);
  state.pendingRecipeFile = PendingIndexedEdit{pending, kID};
  manager.queue.BeginLoad();
  editor.CancelFileOperationsForLoad();
  AcknowledgeResults(state, EditorResults(editor));
  Check(!state.pendingRecipeFile && FileResult(editor, pending).error &&
            AcceptIntent(state, Studio::Undo{kID}),
        "load-canceled file request releases UI gate before its callback is "
        "discarded");
  state.selection.recipeID = kID;
  state.selection.subject = MaskSubject{"metal"};
  const std::vector<RecipeEdit> droppedEdits{RenameMask{"metal", "dropped"},
                                             RemoveOutput{0}};
  const auto dropped = editor.EditRecipe(kID, EditBatch{droppedEdits});
  TrackEditorChange(state, dropped, Studio::EditRecipe{kID, droppedEdits});
  state.pendingIndexedEdit = PendingIndexedEdit{dropped, kID};
  AcknowledgeResults(state, EditorResults(editor));
  Check(!state.pendingEditorChange && !state.pendingIndexedEdit &&
            !state.pendingSelection && EditResult(editor, dropped).error &&
            state.selection.subject == InspectorSubject{MaskSubject{"metal"}},
        "edit dropped during load releases both acknowledgement gates without "
        "following its rename");
  Reduce(state, PickRecipe{"other", true});
  manager.queue.Resume();
  manager.Drain();
  AcknowledgeResults(state, EditorResults(editor));
  Check(!state.pendingRecipeFile && state.selection.recipeID == "other",
        "late canceled callback and repeated snapshot do not disturb the UI");
}

void PaintAcknowledgements(const std::filesystem::path &root,
                           const Recipe &fixture) {
  Start(root, "paint_ack", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  MenuState state;
  state.selection.recipeID = kID;
  state.selection.subject = LayerSubject{0, 0};
  state.selection.layer = 0;
  state.navigation.scroll = 42.0f;
  Reduce(state, SetMode{Mode::kPaint});
  const Studio::BeginPaint begin{
      kID, {}, Surface::kMaterial,
      60,  0,  PaintAssignment{0, 0, editor.DocumentRevisionOf(kID)}};
  Reduce(state, begin);
  editor.BeginPaint(kID, {}, Surface::kMaterial, 60, 0);
  Check(state.paint && !state.paint->ready,
        "UI draft waits for the actual begin acknowledgement");
  manager.Drain();
  AcknowledgeResults(state, EditorResults(editor));
  Check(state.paint && state.paint->ready && !state.paint->problem,
        "real begin result makes the UI draft ready");
  Reduce(state, AddTerm{Term{TermOp::kSet, "0.5", "all", RawTerm{}}});
  const auto update = PendingPaintUpdate(state);
  Check(update.has_value(), "dirty UI mask produces a preview request");
  if (!update) {
    return;
  }
  Reduce(state, *update);
  editor.UpdatePaint(update->request);
  manager.Drain();
  AcknowledgeResults(state, EditorResults(editor));
  Check(
      state.paint && !state.paint->pendingRevision && !state.mask.dirty &&
          !state.paint->problem,
      "actual preview acknowledgement clears only the matching pending update");
  PaintCommitRequest keep{601, kID, "peek", "0.5", 60};
  keep.assignment = begin.assignment;
  Reduce(state, Studio::KeepPaint{keep});
  editor.KeepPaint(keep);
  Check(state.paint && state.paint->pendingCommit == 601 &&
            !AcceptIntent(state, Studio::EndPaint{}) &&
            !AcceptIntent(state, Studio::Undo{kID}),
        "pending Keep retains UI draft and blocks conflicting actions");
  manager.Drain();
  const auto refused = EditorResults(editor);
  AcknowledgeResults(state, refused);
  Check(state.paint && !state.paint->pendingCommit && state.paint->problem &&
            !state.mask.terms.empty() && IsTransient(kPaintRecipe),
        "real Keep refusal exposes its error and preserves the editable UI "
        "draft");
  keep.id = 602;
  keep.maskName = "acknowledged";
  Reduce(state, Studio::KeepPaint{keep});
  editor.KeepPaint(keep);
  AcknowledgeResults(state, refused);
  Check(state.paint && state.paint->pendingCommit == 602,
        "repeated old Keep refusal cannot clear a pending retry");
  manager.Drain();
  const auto accepted = EditorResults(editor);
  AcknowledgeResults(state, accepted);
  Check(!state.paint && state.mask.terms.empty() &&
            state.mode == Mode::kCompose && state.selection.recipeID == kID &&
            state.selection.subject == InspectorSubject{LayerSubject{0, 0}} &&
            state.navigation.scroll == 42.0f &&
            Current().FindMask("acknowledged"),
        "successful real Keep closes draft and restores destination inspector "
        "and scroll");
  Reduce(state, SetMode{Mode::kPaint});
  Reduce(state, Studio::BeginPaint{kID, {}, Surface::kMaterial, 61, 0});
  editor.BeginPaint(kID, {}, Surface::kMaterial, 61, 0);
  manager.Drain();
  AcknowledgeResults(state, EditorResults(editor));
  keep.id = 603;
  keep.sessionID = 61;
  keep.maskName = "canceled";
  keep.assignment.reset();
  Reduce(state, Studio::KeepPaint{keep});
  editor.KeepPaint(keep);
  manager.queue.BeginLoad();
  editor.CancelFileOperationsForLoad();
  editor.CancelPaintForLoad();
  AcknowledgeResults(state, EditorResults(editor));
  Check(!state.paint && state.lastPaintReset > 0 &&
            Is<RecipeSubject>(state.selection.subject) &&
            !Current().FindMask("canceled"),
        "real load reset clears even a pending Keep and resets positional UI "
        "state");
  manager.queue.Resume();
  manager.Drain();
  AcknowledgeResults(state, accepted);
  Check(!state.paint && Is<RecipeSubject>(state.selection.subject),
        "old successful Keep acknowledgement cannot undo the later load reset");
}

void ReloadAndLoad(const std::filesystem::path &root, const Recipe &fixture) {
  Start(root, "reload", fixture);
  Manager manager;
  RecipeEditor editor{manager};
  Edit(editor, manager, 19);
  const auto revision = editor.DocumentRevisionOf(kID);
  editor.ReloadRecipes();
  const auto staleEdit = editor.EditRecipe(kID, {{SetPriority{99}}}, revision);
  manager.Drain();
  const auto results = editor.EditResults();
  const auto *refused =
      FindBy(results, staleEdit, &RecipeEditResult::requestID);
  Check(
      Current() == fixture && !IsDirty(kID) && !editor.HistoryOf(kID) &&
          editor.DocumentRevisionOf(kID) > revision && refused &&
          refused->error,
      "real reload clears history and rejects an edit captured before reload");
  const auto gesture =
      editor.BeginGesture(kID, editor.DocumentRevisionOf(kID), "priority");
  manager.Drain();
  editor.UpdateGesture(gesture, "priority", {{SetPriority{55}}});
  manager.Drain();
  const auto save = editor.SaveRecipe(kID);
  manager.queue.BeginLoad();
  editor.CancelFileOperationsForLoad();
  Check(Current() == fixture && !IsDirty(kID) &&
            FileResult(editor, save).state == FileOperationState::kFailed,
        "load cancellation restores the active gesture and fails pending save "
        "immediately");
  manager.queue.Resume();
  manager.Drain();
  Check(Current() == fixture && !IsDirty(kID) &&
            FileResult(editor, save).state == FileOperationState::kFailed,
        "old save callback cannot overwrite load cancellation after resume");
}
}

int main() {
  const auto original = std::filesystem::current_path();
  const auto root =
      std::filesystem::absolute(test::ScratchDir("editor_integration"));
  const auto parsed =
      ParseRecipe(test::ReadFile(test::Fixtures().parent_path().parent_path() /
                                 "schema/example-magicka.json"),
                  kID);
  Check(parsed.recipe.has_value(), "integration fixture decodes");
  if (parsed.recipe) {
    FailedRevert(root, *parsed.recipe);
    GestureBoundaries(root, *parsed.recipe);
    ImportPromotion(root, *parsed.recipe);
    ReloadAndLoad(root, *parsed.recipe);
    CreateAndDuplicate(root, *parsed.recipe);
    RenameAndDelete(root, *parsed.recipe);
    CleanRename(root, *parsed.recipe);
    ShippedOwnership(root, *parsed.recipe);
    ShippedSaveRouting(root, *parsed.recipe);
    NestedUserSave(root, *parsed.recipe);
    PaintCommitLifecycle(root, *parsed.recipe);
    PaintDiscardAndStaleWork(root, *parsed.recipe);
    PaintDestinationChanges(root, *parsed.recipe);
    PaintLoadCancellation(root, *parsed.recipe);
    PaintReservedNames(root, *parsed.recipe);
    RejectedAndNoOpEdits(root, *parsed.recipe);
    InputDiagnosticLifecycle(root, *parsed.recipe);
    SemanticDiagnosticLifecycle(root);
    GestureValidationRecovery(root, *parsed.recipe);
    RecipeAcknowledgements(root, *parsed.recipe);
    ResourceAcknowledgements(root, *parsed.recipe);
    FileAcknowledgements(root, *parsed.recipe);
    PaintAcknowledgements(root, *parsed.recipe);
  }
  std::filesystem::current_path(original);
  return test::Finish("editor integration");
}
