#include "menu/RecipeActions.h"

#include "engine/Manager.h"
#include "menu/MenuWidgets.h"
#include "studio/FileOperation.h"
#include "studio/Navigation.h"
#include "studio/PaintSession.h"

#include <algorithm>
#include <format>

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
    Problem(std::format("{} failed: {}", action, a_result.error));
  } else {
    Dim(std::format("{} completed: {}", action, a_result.path));
  }
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

void DrawRecipeFileActions(const Frame &a_frame) {
  Manager *manager = Manager::GetSingleton();
  if (!manager || !a_frame.recipe || !a_frame.snapshot || !a_frame.state) {
    return;
  }
  const Studio::RecipeRow &recipe = *a_frame.recipe;
  const bool transient = recipe.id == Studio::kPaintRecipe;
  ImGui::PushID(recipe.id.c_str());
  if (recipe.dirty) {
    Warn("Unsaved changes");
  } else {
    Dim("No unsaved changes");
  }
  ImGui::SameLine();
  Disabled(transient || RecipeFilePending(a_frame), [&] {
    if (ImGui::Button("Save")) {
      a_frame.state->pendingRecipeFile = Studio::PendingIndexedEdit{
          manager->Editor().SaveRecipe(recipe.id), recipe.id};
    }
    ImGui::SameLine();
    if (ImGui::Button("Revert to file")) {
      ImGui::OpenPopup("revert-file");
    }
    DetailModal("revert-file", [&] {
      ImGui::TextWrapped("Replace the current edits to %s with its saved file?",
                         recipe.id.c_str());
      if (ImGui::Button("Revert edits")) {
        Studio::InvalidateIndexedSubjects(a_frame.state->navigation,
                                          a_frame.state->selection, recipe.id);
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
  if (transient) {
    Dim("Keep the mask before saving its destination recipe.");
  }
  if (const auto *result = FileResult(*a_frame.snapshot, recipe.id)) {
    DrawFileResult(*result);
  }
  const auto edit = std::ranges::find(a_frame.snapshot->editResults, recipe.id,
                                      &Studio::RecipeEditResult::recipeID);
  if (edit != a_frame.snapshot->editResults.end() && edit->error) {
    Problem(*edit->error);
  }
  ImGui::PopID();
}
}
