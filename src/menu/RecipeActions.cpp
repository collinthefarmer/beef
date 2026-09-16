#include "menu/RecipeActions.h"

#include "engine/Manager.h"
#include "menu/MenuWidgets.h"
#include "recipe/Recipe.h"
#include "studio/FileOperation.h"
#include "studio/Navigation.h"
#include "studio/PaintSession.h"

#include <algorithm>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include <SKSEMenuFramework.h>
#pragma clang diagnostic pop

namespace ImGui = ImGuiMCP;

namespace BetterEnchantmentEffects::Menu {
namespace {
const Studio::FileOperationResult *
FileResult(const Studio::Snapshot &a_snapshot, const std::string &a_recipeID) {
  const auto found = std::ranges::find(a_snapshot.fileOperations, a_recipeID,
                                       &Studio::FileOperationResult::recipeID);
  return found == a_snapshot.fileOperations.end() ? nullptr : &*found;
}

void DrawFileResult(const Studio::FileOperationResult &a_result) {
  const std::string_view action =
      a_result.action == Studio::FileAction::kSave ? "Save" : "Revert";
  if (a_result.state == Studio::FileOperationState::kPending) {
    Dim(std::format("{} pending", action));
  } else if (a_result.state == Studio::FileOperationState::kFailed) {
    Problem(std::format("{} failed: {}", action, ProblemText(a_result.error)));
  } else {
    Dim(std::format("{} completed: {}", action, a_result.path));
  }
}

std::vector<std::string>
RecipeOperationErrors(const Studio::Snapshot &a_snapshot,
                      const std::string &a_recipeID) {
  std::vector<std::string> out;
  if (a_snapshot.gesture && a_snapshot.gesture->recipeID == a_recipeID &&
      a_snapshot.gesture->error) {
    out.push_back(*a_snapshot.gesture->error);
  }
  const auto edit = std::ranges::find(a_snapshot.editResults, a_recipeID,
                                      &Studio::RecipeEditResult::recipeID);
  if (edit != a_snapshot.editResults.end() && edit->error) {
    out.emplace_back(ProblemText(edit->error));
  }
  return out;
}

void DrawRecipeErrors(const Studio::Snapshot &a_snapshot,
                      const std::string &a_recipeID) {
  for (const std::string &error :
       RecipeOperationErrors(a_snapshot, a_recipeID)) {
    Problem(error);
  }
}

void DrawRecipeResults(const Studio::Snapshot &a_snapshot,
                       const std::string &a_recipeID) {
  if (const auto *result = FileResult(a_snapshot, a_recipeID)) {
    DrawFileResult(*result);
  }
  DrawRecipeErrors(a_snapshot, a_recipeID);
}

void AppendLine(std::string &a_bucket, std::string_view a_line) {
  if (!a_bucket.empty()) {
    a_bucket += '\n';
  }
  a_bucket += a_line;
}

struct RecipeStatus {
  std::string errors;
  std::string warnings;
  std::string saveState;
  std::string transient;
};

void CollectRecipeProblems(const Frame &a_frame, RecipeStatus &a_status) {
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  if (recipe.heldBack) {
    AppendLine(a_status.errors,
               "Held back: recipe-level errors keep it out of the applied "
               "set until they are fixed");
  }
  for (const Diagnostic &diagnostic : recipe.problems) {
    AppendLine(diagnostic.severity == Severity::kError ? a_status.errors
                                                       : a_status.warnings,
               std::format("{}: {}", diagnostic.where, diagnostic.message));
  }
  if (!a_frame.geometry) {
    AppendLine(a_status.warnings,
               "no geometry: the recipe's keys or selectors match no "
               "geometry of the selected piece");
  }
  if (recipe.dirty) {
    AppendLine(a_status.saveState,
               "unsaved changes: edits are not saved to the recipe file yet");
  }
}

bool CollectFileStatus(const Frame &a_frame, RecipeStatus &a_status) {
  if (!a_frame.snapshot) {
    return false;
  }
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  bool pendingShown = false;
  if (const auto *result = FileResult(*a_frame.snapshot, recipe.id)) {
    const bool save = result->action == Studio::FileAction::kSave;
    if (result->state == Studio::FileOperationState::kPending) {
      a_status.transient = save ? "saving..." : "reverting...";
      pendingShown = true;
    } else if (result->state == Studio::FileOperationState::kSucceeded &&
               !recipe.dirty) {
      AppendLine(
          a_status.saveState,
          std::format("{} {}", save ? "saved" : "reverted", result->path));
    } else if (result->state == Studio::FileOperationState::kFailed) {
      AppendLine(a_status.errors,
                 std::format("{} failed: {}", save ? "Save" : "Revert",
                             ProblemText(result->error)));
    }
  }
  for (const std::string &error :
       RecipeOperationErrors(*a_frame.snapshot, recipe.id)) {
    AppendLine(a_status.errors, error);
  }
  return pendingShown;
}

RecipeStatus CollectRecipeStatus(const Frame &a_frame) {
  RecipeStatus status;
  CollectRecipeProblems(a_frame, status);
  const bool pendingShown = CollectFileStatus(a_frame, status);
  const bool busy = RecipeFilePending(a_frame) ||
                    (a_frame.state && Studio::IndexedEditPendingFor(
                                          a_frame.state->pendingIndexedEdit,
                                          a_frame.recipe->id));
  if (busy && !pendingShown) {
    status.transient = "applying...";
  }
  return status;
}

}

bool RecipeFilePending(const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.snapshot || !a_frame.state) {
    return false;
  }
  if (Studio::IndexedEditPendingFor(a_frame.state->pendingRecipeFile,
                                    a_frame.recipe->id)) {
    return true;
  }
  const auto *result = FileResult(*a_frame.snapshot, a_frame.recipe->id);
  return result && result->state == Studio::FileOperationState::kPending;
}

void DrawRecipeSaveRevert(const Frame &a_frame) {
  Manager *manager = Manager::GetSingleton();
  if (!manager || !a_frame.recipe || !a_frame.state) {
    return;
  }
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const bool transient = recipe.id == Studio::kPaintRecipe;
  ImGui::PushID(recipe.id.c_str());
  Disabled(transient || RecipeFilePending(a_frame) ||
               (a_frame.state->paint && a_frame.state->paint->pendingCommit),
           [&] {
             if (ImGui::Button("Save")) {
               a_frame.state->pendingRecipeFile = Studio::PendingIndexedEdit{
                   manager->Editor().SaveRecipe(recipe.id), recipe.id};
             }
             ImGui::SameLine();
             Disabled(!recipe.dirty, [&] {
               if (ImGui::Button("Revert")) {
                 ImGui::OpenPopup("Revert to file###revert-file");
               }
             });
             DetailModal("Revert to file###revert-file", [&] {
               ImGui::TextWrapped(
                   "Replace the current edits to %s with its saved file?",
                   recipe.id.c_str());
               if (ImGui::Button("Revert edits")) {
                 Studio::InvalidateIndexedSubjects(a_frame.state->navigation,
                                                   a_frame.state->selection,
                                                   recipe.id);
                 a_frame.state->pendingRecipeFile = Studio::PendingIndexedEdit{
                     manager->Editor().RevertRecipe(recipe.id), recipe.id};
                 ImGui::CloseCurrentPopup();
               }
               ImGui::SameLine();
               if (ImGui::Button("Cancel")) {
                 ImGui::CloseCurrentPopup();
               }
             });
           });
  ImGui::PopID();
}

void DrawRecipeRuleStatus(const Frame &a_frame) {
  if (!a_frame.recipe) {
    return;
  }
  const RecipeStatus status = CollectRecipeStatus(a_frame);

  bool shown = false;
  const auto next = [&]() {
    if (shown) {
      ImGui::SameLine();
    }
    shown = true;
  };
  const auto glyph = [&](const char *a_glyph, void (*a_draw)(const char *),
                         const std::string &a_tip) {
    if (a_tip.empty()) {
      return;
    }
    next();
    a_draw(a_glyph);
    Tooltip(a_tip);
  };
  glyph("!!!", ProblemBadge, status.errors);
  glyph("!", WarnBadge, status.warnings);
  glyph("*", DimBadge, status.saveState);
  if (!status.transient.empty()) {
    next();
    ImGui::AlignTextToFramePadding();
    Dim(status.transient);
  }
}

void DrawMaskDraftHints(const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.state) {
    return;
  }
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  if (a_frame.state->paint && a_frame.state->paint->recipeID == recipe.id) {
    Dim("Save excludes the suspended mask draft.");
  }
  if (recipe.id == Studio::kPaintRecipe) {
    Dim("Keep the mask before saving its destination recipe.");
  }
}

void DrawRecipeFileActions(const Frame &a_frame) {
  if (!a_frame.recipe || !a_frame.snapshot || !a_frame.state) {
    return;
  }
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  if (a_frame.state->paint && a_frame.state->paint->recipeID == recipe.id) {
    Dim("Save excludes the suspended mask draft.");
  }
  if (recipe.dirty) {
    Warn("Unsaved changes");
  } else {
    Dim("No unsaved changes");
  }
  ImGui::SameLine();
  DrawRecipeSaveRevert(a_frame);
  if (recipe.id == Studio::kPaintRecipe) {
    Dim("Keep the mask before saving its destination recipe.");
  }
  DrawRecipeResults(*a_frame.snapshot, recipe.id);
}
}
