#pragma once

#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/History.h"
#include "studio/PaintCommit.h"
#include "studio/Snapshot.h"
#include "studio/View.h"

#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace BetterEnchantmentEffects {
class Manager;

class RecipeEditor {
public:
  explicit RecipeEditor(Manager &a_runtime);
  RecipeEditor(const RecipeEditor &) = delete;
  RecipeEditor &operator=(const RecipeEditor &) = delete;

  std::uint64_t EditRecipe(std::string a_id, Studio::EditBatch a_edits);
  std::uint64_t UndoRecipe(std::string a_id);
  std::uint64_t RedoRecipe(std::string a_id);
  std::uint64_t SaveRecipe(std::string a_id);
  std::uint64_t RevertRecipe(std::string a_id);
  std::uint64_t ReloadRecipes();
  std::uint64_t NewRecipe(std::string a_id, RecipeKey a_key,
                          std::string a_geometry);
  std::uint64_t RenameRecipe(std::string a_from, std::string a_to);
  void BeginPaint(std::string a_active, RecipeKey a_key, Surface a_surface,
                  std::uint64_t a_sessionID, std::uint64_t a_resetID);
  void UpdatePaint(Studio::PaintUpdateRequest a_request);
  void KeepPaint(Studio::PaintCommitRequest a_request);
  void EndPaint(std::uint64_t a_sessionID = 0);
  void CancelPaintForLoad();
  void CancelFileOperationsForLoad();
  void Isolate(Studio::Isolation a_isolation);
  void ChangeView(Studio::ViewCommand a_command);
  void PinRecipe(Studio::PieceRef a_piece, std::string a_recipeID);
  void UpdateView(std::function<void(Studio::View &)> a_change);

  [[nodiscard]] const Studio::View &CurrentView() const noexcept;
  [[nodiscard]] const Studio::History<Recipe> *
  HistoryOf(const std::string &a_id) const noexcept;
  [[nodiscard]] const std::optional<Studio::PaintCommitResult> &
  LastPaintCommit() const noexcept;
  [[nodiscard]] const std::optional<Studio::PaintUpdateResult> &
  LastPaintUpdate() const noexcept;

  [[nodiscard]] std::vector<Studio::FileOperationResult> FileOperations() const;
  [[nodiscard]] std::vector<Studio::RecipeEditResult> EditResults() const;

private:
  struct FileOperationJournal;
  struct PendingFileOperation;
  struct PendingRecipeEdit;
  std::shared_ptr<FileOperationJournal> fileOperations_;
  [[nodiscard]] std::expected<void, Diagnostic>
  ApplyEdits(const std::string &a_id, const Studio::EditBatch &a_edits);
  void FinishPaint();
  void SaveRecipeNow(const std::string &a_id,
                     const PendingFileOperation &a_operation);
  void ReloadRecipesNow();
  [[nodiscard]] std::optional<std::string>
  RestoreRecipe(const std::string &a_id, bool a_redo);

  Manager &runtime_;
  std::unordered_map<std::string, Studio::History<Recipe>> histories_;
  Studio::View view_{};
  Studio::Isolation paintReturn_{};
  std::optional<Studio::PaintCommitResult> paintCommit_;
  std::optional<Studio::PaintUpdateResult> paintUpdate_;
  std::uint64_t paintSessionID_ = 0;
  std::uint64_t paintRevision_ = 0;
  std::uint64_t paintResetID_ = 0;
};
}
