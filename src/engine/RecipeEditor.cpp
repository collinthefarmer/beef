#include "engine/RecipeEditor.h"
#include "diagnostics/Trace.h"

#include "engine/Manager.h"

#include "engine/RecipeStore.h"
#include "studio/Edits.h"
#include "studio/PaintSession.h"

#include <algorithm>
#include <format>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
const Recipe *FindLoaded(std::span<const Recipe> a_loaded,
                         std::string_view a_id) {
  const auto it = std::ranges::find(a_loaded, a_id, &Recipe::id);
  return it == a_loaded.end() ? nullptr : &*it;
}

void PushHistory(
    std::unordered_map<std::string, Studio::History<Recipe>> &a_histories,
    const std::string &a_id, const Recipe &a_before, const Recipe &a_after) {
  if (!(a_after == a_before)) {
    a_histories[a_id].Push(a_before);
  }
}

void LogRecipeDiagnostics(std::string_view a_id,
                          std::span<const Diagnostic> a_diagnostics) {
  for (const Diagnostic &d : a_diagnostics) {
    if (d.severity == Severity::kError) {
      logger::error("recipe {} {}: {}", a_id, d.where, d.message);
    } else {
      logger::warn("recipe {} {}: {}", a_id, d.where, d.message);
    }
  }
}
}

namespace {
std::optional<std::string>
MessageOf(const std::optional<Diagnostic> &a_refusal) {
  if (!a_refusal) {
    return std::nullopt;
  }
  return a_refusal->message;
}
}

struct RecipeEditor::FileOperationJournal {
  std::mutex lock;
  std::uint64_t nextID = 0;
  std::vector<Studio::FileOperationResult> results;
  struct EditEntry {
    Studio::RecipeEditResult result;
    bool pending = true;
  };
  std::vector<EditEntry> edits;

  std::uint64_t BeginEdit(const std::string &a_id) {
    std::scoped_lock guard{lock};
    std::erase_if(edits, [&](const EditEntry &a_entry) {
      return a_entry.result.recipeID == a_id;
    });
    if (edits.size() >= 64) {
      edits.erase(edits.begin());
    }
    const std::uint64_t id = ++nextID;
    edits.push_back({{id, a_id, std::nullopt}, true});
    return id;
  }

  void FinishEdit(std::uint64_t a_id, std::optional<std::string> a_error) {
    std::scoped_lock guard{lock};
    for (EditEntry &entry : edits) {
      if (entry.result.requestID == a_id && entry.pending) {
        entry.result.error = std::move(a_error);
        entry.pending = false;
        return;
      }
    }
  }

  std::uint64_t Begin(const std::string &a_id, Studio::FileAction a_action) {
    std::scoped_lock guard{lock};
    std::erase_if(results, [&](const Studio::FileOperationResult &result) {
      return result.recipeID == a_id;
    });
    if (results.size() >= 64) {
      results.erase(results.begin());
    }
    const std::uint64_t id = ++nextID;
    results.push_back(
        {id, a_id, a_action, Studio::FileOperationState::kPending, {}, {}});
    return id;
  }

  void CancelPending() {
    std::scoped_lock guard{lock};
    for (EditEntry &entry : edits) {
      if (entry.pending) {
        entry.result.error = "Recipe edit was canceled by game load.";
        entry.pending = false;
      }
    }
    for (Studio::FileOperationResult &result : results) {
      if (result.state == Studio::FileOperationState::kPending) {
        result.state = Studio::FileOperationState::kFailed;
        result.error = "File operation was canceled by game load.";
      }
    }
  }

  void Finish(std::uint64_t a_id, std::string a_path, std::string a_error) {
    std::scoped_lock guard{lock};
    const auto found = std::ranges::find(
        results, a_id, &Studio::FileOperationResult::requestID);
    if (found == results.end() ||
        found->state != Studio::FileOperationState::kPending) {
      return;
    }
    found->state = a_error.empty() ? Studio::FileOperationState::kSucceeded
                                   : Studio::FileOperationState::kFailed;
    found->path = std::move(a_path);
    found->error = std::move(a_error);
  }
};

struct RecipeEditor::PendingFileOperation {
  std::shared_ptr<FileOperationJournal> journal;
  std::uint64_t id;

  PendingFileOperation(std::shared_ptr<FileOperationJournal> a_journal,
                       const std::string &a_recipe, Studio::FileAction a_action)
      : journal(std::move(a_journal)), id(journal->Begin(a_recipe, a_action)) {}

  PendingFileOperation(const PendingFileOperation &) = delete;
  PendingFileOperation &operator=(const PendingFileOperation &) = delete;
  PendingFileOperation(PendingFileOperation &&) = delete;
  PendingFileOperation &operator=(PendingFileOperation &&) = delete;

  ~PendingFileOperation() {
    journal->Finish(id, {}, "File operation was canceled before completion.");
  }

  void Finish(std::string a_path, std::string a_error = {}) const {
    journal->Finish(id, std::move(a_path), std::move(a_error));
  }
};

struct RecipeEditor::PendingRecipeEdit {
  std::shared_ptr<FileOperationJournal> journal;
  std::uint64_t id;

  PendingRecipeEdit(std::shared_ptr<FileOperationJournal> a_journal,
                    const std::string &a_recipe)
      : journal(std::move(a_journal)), id(journal->BeginEdit(a_recipe)) {}

  PendingRecipeEdit(const PendingRecipeEdit &) = delete;
  PendingRecipeEdit &operator=(const PendingRecipeEdit &) = delete;
  PendingRecipeEdit(PendingRecipeEdit &&) = delete;
  PendingRecipeEdit &operator=(PendingRecipeEdit &&) = delete;

  ~PendingRecipeEdit() {
    journal->FinishEdit(id, "Recipe edit was canceled before completion.");
  }

  void Finish(std::optional<std::string> a_error = std::nullopt) const {
    journal->FinishEdit(id, std::move(a_error));
  }
};

std::vector<Studio::RecipeEditResult> RecipeEditor::EditResults() const {
  std::scoped_lock guard{fileOperations_->lock};
  std::vector<Studio::RecipeEditResult> results;
  for (const FileOperationJournal::EditEntry &entry : fileOperations_->edits) {
    if (!entry.pending) {
      results.push_back(entry.result);
    }
  }
  return results;
}

std::vector<Studio::FileOperationResult> RecipeEditor::FileOperations() const {
  std::scoped_lock guard{fileOperations_->lock};
  return fileOperations_->results;
}

std::uint64_t RecipeEditor::EditRecipe(std::string a_id,
                                       Studio::EditBatch a_edits) {
  const Trace::Scope trace{Trace::Command("editor.EditRecipe")};
  const auto operation =
      std::make_shared<PendingRecipeEdit>(fileOperations_, a_id);
  runtime_.PostTask(
      [this, id = std::move(a_id), edits = std::move(a_edits), operation] {
        if (const auto applied = ApplyEdits(id, edits); !applied) {
          logger::warn("edit refused: {} ({}: {})", Studio::Describe(edits),
                       applied.error().where, applied.error().message);
          operation->Finish(std::format("{}: {}", applied.error().where,
                                        applied.error().message));
          return;
        }
        operation->Finish();
      });
  return operation->id;
}

std::expected<void, Diagnostic>
RecipeEditor::ApplyEdits(const std::string &a_id,
                         const Studio::EditBatch &a_edits) {
  Recipe *recipe = MutableRecipe(a_id);
  if (!recipe) {
    return std::unexpected(
        MakeDiagnostic(Severity::kError, a_id, "recipe is not loaded"));
  }
  std::expected<Recipe, Diagnostic> prepared =
      Studio::PrepareEdits(*recipe, a_edits);
  if (!prepared) {
    return std::unexpected(prepared.error());
  }
  if (*prepared == *recipe) {
    return {};
  }
  runtime_.ChangeAndRebuildActors(a_id, [&] {
    PushHistory(histories_, a_id, *recipe, *prepared);
    *recipe = std::move(*prepared);
    LogRecipeDiagnostics(a_id, RefreshRecipeDerivedState(a_id));
  });
  return {};
}

std::optional<std::string> RecipeEditor::RestoreRecipe(const std::string &a_id,
                                                       bool a_redo) {
  Recipe *recipe = MutableRecipe(a_id);
  const auto history = histories_.find(a_id);
  if (!recipe)
    return "Recipe is not loaded.";
  if (history == histories_.end())
    return "No recipe history is available.";
  auto restored =
      a_redo ? history->second.Redo(*recipe) : history->second.Undo(*recipe);
  if (!restored)
    return a_redo ? "Nothing to redo." : "Nothing to undo.";
  runtime_.ChangeAndRebuildActors(a_id, [&] {
    *recipe = std::move(*restored);
    recipe->id = a_id;
    RefreshRecipeDerivedState(a_id);
  });
  return std::nullopt;
}

std::uint64_t RecipeEditor::UndoRecipe(std::string a_id) {
  const Trace::Scope trace{Trace::Command("editor.UndoRecipe")};
  const auto operation =
      std::make_shared<PendingRecipeEdit>(fileOperations_, a_id);
  runtime_.PostTask([this, id = std::move(a_id), operation] {
    operation->Finish(RestoreRecipe(id, false));
  });
  return operation->id;
}

std::uint64_t RecipeEditor::RedoRecipe(std::string a_id) {
  const Trace::Scope trace{Trace::Command("editor.RedoRecipe")};
  const auto operation =
      std::make_shared<PendingRecipeEdit>(fileOperations_, a_id);
  runtime_.PostTask([this, id = std::move(a_id), operation] {
    operation->Finish(RestoreRecipe(id, true));
  });
  return operation->id;
}

std::uint64_t RecipeEditor::SaveRecipe(std::string a_id) {
  const Trace::Scope trace{Trace::Command("editor.SaveRecipe")};
  const auto operation = std::make_shared<PendingFileOperation>(
      fileOperations_, a_id, Studio::FileAction::kSave);
  runtime_.PostTask([this, id = std::move(a_id), operation] {
    runtime_.ChangeAndRebuildActors(id, [&] { SaveRecipeNow(id, *operation); });
  });
  return operation->id;
}

void RecipeEditor::SaveRecipeNow(const std::string &a_id,
                                 const PendingFileOperation &a_operation) {
  Recipe *recipe = MutableRecipe(a_id);
  const Recipe before = recipe ? *recipe : Recipe{};
  const std::expected<std::filesystem::path, Diagnostic> saved =
      BetterEnchantmentEffects::SaveRecipe(a_id);
  if (!saved) {
    if (recipe && *recipe != before) {
      *recipe = before;
      RefreshRecipeDerivedState(a_id);
    }
    a_operation.Finish({}, saved.error().message);
    logger::error("recipe {}: save failed ({})", a_id, saved.error().message);
    return;
  }
  if (recipe) {
    PushHistory(histories_, a_id, before, *recipe);
  }
  a_operation.Finish(saved->string());
}

std::uint64_t RecipeEditor::RevertRecipe(std::string a_id) {
  const Trace::Scope trace{Trace::Command("editor.RevertRecipe")};
  const auto operation = std::make_shared<PendingFileOperation>(
      fileOperations_, a_id, Studio::FileAction::kRevert);
  runtime_.PostTask([this, id = std::move(a_id), operation] {
    runtime_.ChangeAndRebuildActors(id, [&] {
      Recipe *recipe = MutableRecipe(id);
      if (!recipe) {
        operation->Finish({}, "Recipe is not loaded.");
        return;
      }
      if (IsTransient(id)) {
        operation->Finish({}, "The paint draft has no recipe file to revert.");
        return;
      }
      const std::optional<RecipeOrigin> origin = OriginOf(*recipe);
      const std::string path = origin ? origin->path.string() : std::string{};
      const Recipe before = *recipe;
      if (const std::optional<Diagnostic> refused =
              BetterEnchantmentEffects::RevertRecipe(id)) {
        operation->Finish(path, refused->message);
        return;
      }
      PushHistory(histories_, id, before, *recipe);
      operation->Finish(path);
      logger::info("recipe {}: reverted to its file", id);
    });
  });
  return operation->id;
}

std::uint64_t RecipeEditor::ReloadRecipes() {
  const Trace::Scope trace{Trace::Command("editor.ReloadRecipes")};
  const auto operation =
      std::make_shared<PendingRecipeEdit>(fileOperations_, std::string{});
  runtime_.PostTask([this, operation] {
    runtime_.ChangeAndRebuildActors({}, [this] { ReloadRecipesNow(); });
    operation->Finish();
  });
  return operation->id;
}

void RecipeEditor::ReloadRecipesNow() {
  CancelPaintForLoad();
  histories_.clear();
  LoadRecipes();
  paintReturn_ = {};
  const std::span<const Recipe> loaded = LoadedRecipes();
  for (const std::string &id : view_.RecipeIDs()) {
    if (!FindLoaded(loaded, id)) {
      view_.ForgetRecipe(id);
    }
  }
}

std::uint64_t RecipeEditor::NewRecipe(std::string a_id, RecipeKey a_key,
                                      std::string a_geometry) {
  const Trace::Scope trace{Trace::Command("editor.NewRecipe")};
  const auto operation =
      std::make_shared<PendingRecipeEdit>(fileOperations_, a_id);
  runtime_.PostTask([this, id = std::move(a_id), key = std::move(a_key),
                     geometry = std::move(a_geometry), operation] {
    runtime_.ChangeAndRebuildActors({}, [&] {
      operation->Finish(
          MessageOf(BetterEnchantmentEffects::NewRecipe(id, key, geometry)));
    });
  });
  return operation->id;
}

std::uint64_t RecipeEditor::RenameRecipe(std::string a_from, std::string a_to) {
  const Trace::Scope trace{Trace::Command("editor.RenameRecipe")};
  const auto operation =
      std::make_shared<PendingRecipeEdit>(fileOperations_, a_from);
  runtime_.PostTask(
      [this, from = std::move(a_from), to = std::move(a_to), operation] {
        runtime_.ChangeAndRebuildActors({}, [&] {
          if (const std::optional<Diagnostic> refused =
                  BetterEnchantmentEffects::RenameRecipe(from, to)) {
            operation->Finish(refused->message);
            return;
          }
          if (auto node = histories_.extract(from)) {
            node.key() = to;
            node.mapped().Rename(to);
            histories_.insert(std::move(node));
          }
          view_.RenameRecipe(from, to);
          if (paintReturn_.recipeID == from) {
            paintReturn_.recipeID = to;
          }
          operation->Finish();
        });
      });
  return operation->id;
}

void RecipeEditor::BeginPaint(std::string a_active, RecipeKey a_key,
                              Surface a_surface, std::uint64_t a_sessionID,
                              std::uint64_t a_resetID) {
  const Trace::Scope trace{Trace::Command("editor.BeginPaint")};
  runtime_.PostTask([this, active = std::move(a_active), key = std::move(a_key),
                     a_surface, a_sessionID, a_resetID] {
    if (a_resetID != paintResetID_) {
      return;
    }
    paintSessionID_ = a_sessionID;
    paintRevision_ = 0;
    paintUpdate_ = Studio::PaintUpdateResult{a_sessionID, 0, {}};
    const std::span<const Recipe> loaded = LoadedRecipes();
    const Recipe *source = FindLoaded(loaded, active);
    if (!source || active == Studio::kPaintRecipe) {
      paintUpdate_->problem = MakeDiagnostic(Severity::kError, "paint",
                                             "the target recipe is not loaded");
      logger::warn("paint: recipe {} is not loaded", active);
      return;
    }
    Recipe paint = Studio::PaintRecipe(*source, key, a_surface);
    runtime_.ChangeAndRebuildActors({}, [&] {
      if (IsTransient(Studio::kPaintRecipe)) {
        [[maybe_unused]] const std::optional<Diagnostic> dropped =
            DropTransientRecipe(Studio::kPaintRecipe);
      }
      if (const std::optional<Diagnostic> refused =
              AddTransientRecipe(std::move(paint))) {
        paintUpdate_->problem = MakeDiagnostic(
            Severity::kError, "paint",
            std::format("the paint recipe could not be started: {}",
                        refused->message));
        if (view_.isolation.recipeID == Studio::kPaintRecipe) {
          view_.isolation = paintReturn_;
          paintReturn_ = {};
        }
        return;
      }
      histories_.erase(std::string{Studio::kPaintRecipe});
      logger::info("paint: previewing {} on the {} through the paint recipe, "
                   "keyed by {}",
                   active, SurfaceName(a_surface), key.ToString());
      if (view_.isolation.recipeID != Studio::kPaintRecipe) {
        paintReturn_ = view_.isolation;
      }
      view_.isolation =
          Studio::Isolation::ForRecipe(std::string{Studio::kPaintRecipe});
    });
  });
}

void RecipeEditor::UpdatePaint(Studio::PaintUpdateRequest a_request) {
  const Trace::Scope trace{Trace::Command("editor.UpdatePaint")};
  runtime_.PostTask([this, request = std::move(a_request)] {
    if (request.sessionID != paintSessionID_ ||
        request.revision <= paintRevision_) {
      return;
    }
    const auto edits = Studio::PreparePaintUpdate(
        FindLoaded(LoadedRecipes(), Studio::kPaintRecipe), request);
    const std::expected<void, Diagnostic> applied =
        edits ? ApplyEdits(std::string{Studio::kPaintRecipe}, *edits)
              : std::unexpected(edits.error());
    paintUpdate_ =
        Studio::PaintUpdateResult{request.sessionID, request.revision, {}};
    if (!applied) {
      paintUpdate_->problem = applied.error();
      return;
    }
    paintRevision_ = request.revision;
  });
}

void RecipeEditor::KeepPaint(Studio::PaintCommitRequest a_request) {
  const Trace::Scope trace{Trace::Command("editor.KeepPaint")};
  runtime_.PostTask([this, request = std::move(a_request)] {
    if (request.sessionID != paintSessionID_ ||
        (paintCommit_ && paintCommit_->requestID == request.id)) {
      return;
    }
    const std::span<const Recipe> loaded = LoadedRecipes();
    const auto edits = Studio::PreparePaintCommit(
        FindLoaded(loaded, Studio::kPaintRecipe),
        FindLoaded(loaded, request.recipeID), request);
    const std::expected<void, Diagnostic> applied =
        edits ? ApplyEdits(request.recipeID, *edits)
              : std::unexpected(edits.error());
    paintCommit_ = Studio::PaintCommitResult{request.id, std::nullopt};
    if (!applied) {
      paintCommit_->problem = applied.error();
      logger::warn("keep refused: {} ({}: {})", request.maskName,
                   applied.error().where, applied.error().message);
      return;
    }
    logger::info("keep: mask {} written into {} ({} edit(s))", request.maskName,
                 request.recipeID, edits->edits.size());
    FinishPaint();
  });
}

void RecipeEditor::EndPaint(std::uint64_t a_sessionID) {
  const Trace::Scope trace{Trace::Command("editor.EndPaint")};
  runtime_.PostTask([this, a_sessionID] {
    if (a_sessionID == 0 || a_sessionID == paintSessionID_) {
      FinishPaint();
    }
  });
}

void RecipeEditor::CancelFileOperationsForLoad() {
  fileOperations_->CancelPending();
}

void RecipeEditor::CancelPaintForLoad() {
  if (view_.isolation.recipeID == Studio::kPaintRecipe) {
    view_.isolation = paintReturn_;
  }
  paintReturn_ = {};
  view_.ForgetRecipe(Studio::kPaintRecipe);
  [[maybe_unused]] const std::optional<Diagnostic> dropped =
      DropTransientRecipe(Studio::kPaintRecipe);
  histories_.erase(std::string{Studio::kPaintRecipe});
  paintSessionID_ = 0;
  paintRevision_ = 0;
  paintCommit_.reset();
  paintUpdate_ = Studio::PaintUpdateResult{0, ++paintResetID_, {}, true};
}

void RecipeEditor::FinishPaint() {
  paintSessionID_ = 0;
  paintRevision_ = 0;
  runtime_.ChangeAndRebuildActors({}, [&] {
    if (view_.isolation.recipeID == Studio::kPaintRecipe) {
      view_.isolation = paintReturn_;
      paintReturn_ = {};
    }
    view_.ForgetRecipe(Studio::kPaintRecipe);
    [[maybe_unused]] const std::optional<Diagnostic> dropped =
        DropTransientRecipe(Studio::kPaintRecipe);
    histories_.erase(std::string{Studio::kPaintRecipe});
  });
}

void RecipeEditor::Isolate(Studio::Isolation a_isolation) {
  const Trace::Scope trace{Trace::Command("editor.Isolate")};
  runtime_.PostTask([this, isolation = std::move(a_isolation)] {
    if (view_.isolation == isolation) {
      return;
    }
    runtime_.ChangeAndRebuildActors({}, [&] { view_.isolation = isolation; });
  });
}

void RecipeEditor::ChangeView(Studio::ViewCommand a_command) {
  const Trace::Scope trace{Trace::Command("editor.ChangeView")};
  runtime_.PostTask([this, command = std::move(a_command)] {
    Studio::View next = view_;
    if (!Studio::ApplyViewCommand(next, command)) {
      return;
    }
    runtime_.ChangeAndRebuildActors({}, [&] { view_ = std::move(next); });
  });
}

void RecipeEditor::PinRecipe(Studio::PieceRef a_piece, std::string a_recipeID) {
  const Trace::Scope trace{Trace::Command("editor.PinRecipe")};
  runtime_.PostTask([this, a_piece, id = std::move(a_recipeID)] {
    std::optional<Studio::Pin> pin;
    if (!id.empty()) {
      const std::span<const Recipe> loaded = LoadedRecipes();
      if (std::ranges::find(loaded, id, &Recipe::id) == loaded.end()) {
        logger::warn("pin: recipe {} is not loaded", id);
        return;
      }
      pin = Studio::Pin{a_piece, id};
    }
    if (view_.pin == pin) {
      return;
    }
    runtime_.ChangeAndRebuildActors({}, [&] { view_.pin = pin; });
    if (pin) {
      logger::info("pin: {} shown on armor {:08X} of actor {:08X} ({}) while "
                   "viewed",
                   id, a_piece.armorID, a_piece.actorID,
                   a_piece.firstPerson ? "1st" : "3rd");
    } else {
      logger::info("pin: cleared");
    }
  });
}

void RecipeEditor::UpdateView(std::function<void(Studio::View &)> a_change) {
  const Trace::Scope trace{Trace::Command("editor.UpdateView")};
  runtime_.PostTask([this, change = std::move(a_change)] {
    Studio::View next = view_;
    change(next);
    if (next.muted != view_.muted || next.isolation != view_.isolation) {
      runtime_.ChangeAndRebuildActors({}, [&] { view_ = std::move(next); });
    } else {
      view_ = std::move(next);
    }
  });
}

RecipeEditor::RecipeEditor(Manager &a_runtime)
    : fileOperations_(std::make_shared<FileOperationJournal>()),
      runtime_(a_runtime) {}

const Studio::View &RecipeEditor::CurrentView() const noexcept { return view_; }

const Studio::History<Recipe> *
RecipeEditor::HistoryOf(const std::string &a_id) const noexcept {
  const auto history = histories_.find(a_id);
  return history == histories_.end() ? nullptr : &history->second;
}

const std::optional<Studio::PaintCommitResult> &
RecipeEditor::LastPaintCommit() const noexcept {
  return paintCommit_;
}
const std::optional<Studio::PaintUpdateResult> &
RecipeEditor::LastPaintUpdate() const noexcept {
  return paintUpdate_;
}

}
