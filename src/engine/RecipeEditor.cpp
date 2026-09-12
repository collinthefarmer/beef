#include "engine/RecipeEditor.h"

#include "engine/Manager.h"

#include "engine/RecipeStore.h"
#include "studio/Edits.h"
#include "studio/PaintSession.h"

#include <algorithm>
#include <format>
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

void RecipeEditor::EditRecipe(std::string a_id, Studio::EditBatch a_edits) {
  runtime_.PostTask([this, id = std::move(a_id), edits = std::move(a_edits)] {
    if (const auto applied = ApplyEdits(id, edits); !applied) {
      logger::warn("edit refused: {} ({}: {})", Studio::Describe(edits),
                   applied.error().where, applied.error().message);
    }
  });
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
  runtime_.RebuildRecipeWearersAfterChange(a_id, [&] {
    PushHistory(histories_, a_id, *recipe, *prepared);
    *recipe = std::move(*prepared);
    LogRecipeDiagnostics(a_id, RefreshRecipeDerivedState(a_id));
  });
  return {};
}

void RecipeEditor::RestoreRecipe(const std::string &a_id, bool a_redo) {
  runtime_.RebuildRecipeWearersAfterChange(a_id, [&] {
    Recipe *recipe = MutableRecipe(a_id);
    const auto history = histories_.find(a_id);
    if (!recipe || history == histories_.end()) {
      return;
    }
    std::optional<Recipe> restored =
        a_redo ? history->second.Redo(*recipe) : history->second.Undo(*recipe);
    if (!restored) {
      return;
    }
    *recipe = std::move(*restored);
    recipe->id = a_id;
    RefreshRecipeDerivedState(a_id);
  });
}

void RecipeEditor::UndoRecipe(std::string a_id) {
  runtime_.PostTask([this, id = std::move(a_id)] { RestoreRecipe(id, false); });
}

void RecipeEditor::RedoRecipe(std::string a_id) {
  runtime_.PostTask([this, id = std::move(a_id)] { RestoreRecipe(id, true); });
}

void RecipeEditor::SaveRecipe(std::string a_id) {
  runtime_.PostTask([this, id = std::move(a_id)] {
    runtime_.RebuildRecipeWearersAfterChange(id, [&] {
      const Recipe *recipe = MutableRecipe(id);
      const Recipe before = recipe ? *recipe : Recipe{};
      const std::expected<std::filesystem::path, std::string> saved =
          BetterEnchantmentEffects::SaveRecipe(id);
      if (!saved) {
        logger::error("recipe {}: save failed ({})", id, saved.error());
        return;
      }
      if (recipe) {
        PushHistory(histories_, id, before, *recipe);
      }
    });
  });
}

void RecipeEditor::RevertRecipe(std::string a_id) {
  runtime_.PostTask([this, id = std::move(a_id)] {
    runtime_.RebuildRecipeWearersAfterChange(id, [&] {
      Recipe *recipe = MutableRecipe(id);
      if (!recipe) {
        return;
      }
      const Recipe before = *recipe;
      if (BetterEnchantmentEffects::RevertRecipe(id)) {
        PushHistory(histories_, id, before, *recipe);
        logger::info("recipe {}: reverted to its file", id);
      }
    });
  });
}

void RecipeEditor::ReloadRecipes() {
  runtime_.PostTask([this] {
    runtime_.RebuildAllActorsAfterChange([&] {
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
    });
  });
}

void RecipeEditor::NewRecipe(std::string a_id, RecipeKey a_key,
                             std::string a_geometry) {
  runtime_.PostTask([this, id = std::move(a_id), key = std::move(a_key),
                     geometry = std::move(a_geometry)] {
    runtime_.RebuildAllActorsAfterChange([&] {
      [[maybe_unused]] const bool made =
          BetterEnchantmentEffects::NewRecipe(id, key, geometry);
    });
  });
}

void RecipeEditor::RenameRecipe(std::string a_from, std::string a_to) {
  runtime_.PostTask([this, from = std::move(a_from), to = std::move(a_to)] {
    runtime_.RebuildAllActorsAfterChange([&] {
      if (!BetterEnchantmentEffects::RenameRecipe(from, to)) {
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
    });
  });
}

void RecipeEditor::BeginPaint(std::string a_active, RecipeKey a_key,
                              Surface a_surface, std::uint64_t a_sessionID,
                              std::uint64_t a_resetID) {
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
    runtime_.RebuildAllActorsAfterChange([&] {
      if (IsTransient(Studio::kPaintRecipe)) {
        [[maybe_unused]] const bool dropped =
            DropTransientRecipe(Studio::kPaintRecipe);
      }
      if (!AddTransientRecipe(std::move(paint))) {
        paintUpdate_->problem = MakeDiagnostic(
            Severity::kError, "paint", "the paint recipe could not be started");
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
  runtime_.PostTask([this, a_sessionID] {
    if (a_sessionID == 0 || a_sessionID == paintSessionID_) {
      FinishPaint();
    }
  });
}

void RecipeEditor::CancelPaintForLoad() {
  if (view_.isolation.recipeID == Studio::kPaintRecipe) {
    view_.isolation = paintReturn_;
  }
  paintReturn_ = {};
  view_.ForgetRecipe(Studio::kPaintRecipe);
  [[maybe_unused]] const bool dropped =
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
  runtime_.RebuildAllActorsAfterChange([&] {
    if (view_.isolation.recipeID == Studio::kPaintRecipe) {
      view_.isolation = paintReturn_;
      paintReturn_ = {};
    }
    view_.ForgetRecipe(Studio::kPaintRecipe);
    [[maybe_unused]] const bool dropped =
        DropTransientRecipe(Studio::kPaintRecipe);
    histories_.erase(std::string{Studio::kPaintRecipe});
  });
}

void RecipeEditor::Isolate(Studio::Isolation a_isolation) {
  runtime_.PostTask([this, isolation = std::move(a_isolation)] {
    if (view_.isolation == isolation) {
      return;
    }
    runtime_.RebuildAllActorsAfterChange([&] { view_.isolation = isolation; });
  });
}

void RecipeEditor::ChangeView(Studio::ViewCommand a_command) {
  runtime_.PostTask([this, command = std::move(a_command)] {
    Studio::View next = view_;
    if (!Studio::ApplyViewCommand(next, command)) {
      return;
    }
    runtime_.RebuildAllActorsAfterChange([&] { view_ = std::move(next); });
  });
}

void RecipeEditor::PinRecipe(Studio::PieceRef a_piece, std::string a_recipeID) {
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
    runtime_.RebuildAllActorsAfterChange([&] { view_.pin = pin; });
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
  runtime_.PostTask([this, change = std::move(a_change)] {
    Studio::View next = view_;
    change(next);
    if (next.muted != view_.muted) {
      runtime_.RebuildAllActorsAfterChange([&] { view_ = std::move(next); });
    } else {
      view_ = std::move(next);
    }
  });
}

RecipeEditor::RecipeEditor(Manager &a_runtime) : runtime_(a_runtime) {}

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
