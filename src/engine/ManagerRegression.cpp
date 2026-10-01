// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include "engine/RecipeOperations.h"
#include "engine/RecipeStore.h"

#include <algorithm>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
constexpr float kGestureOpacity = 0.5f;
constexpr std::string_view kGestureProperty = "regression";
constexpr std::string_view kPaintExpression = "1";

Regression::WorkOutcome
EditOutcome(const std::vector<Studio::RecipeEditResult> &a_results,
            std::uint64_t a_request) {
  if (a_request == 0) {
    return Regression::WorkOutcome::kNone;
  }
  const auto found = std::ranges::find(a_results, a_request,
                                       &Studio::RecipeEditResult::requestID);
  if (found == a_results.end()) {
    return Regression::WorkOutcome::kPending;
  }
  if (!found->error) {
    return Regression::WorkOutcome::kApplied;
  }
  return found->error->message == kEditCanceledByLoad
             ? Regression::WorkOutcome::kCancelledByLoad
             : Regression::WorkOutcome::kFailed;
}

Regression::WorkOutcome
GestureOutcome(const std::optional<Studio::GestureResult> &a_gesture,
               std::uint64_t a_id) {
  if (a_id == 0 || !a_gesture || a_gesture->gestureID != a_id) {
    return Regression::WorkOutcome::kNone;
  }
  switch (a_gesture->state) {
  case Studio::GesturePhase::kActive:
    return Regression::WorkOutcome::kPending;
  case Studio::GesturePhase::kCommitted:
    return Regression::WorkOutcome::kApplied;
  case Studio::GesturePhase::kCanceled:
  case Studio::GesturePhase::kRefused:
  case Studio::GesturePhase::kCount:
    break;
  }
  return a_gesture->error == kTuningCanceledByLoad
             ? Regression::WorkOutcome::kCancelledByLoad
             : Regression::WorkOutcome::kFailed;
}

Regression::WorkOutcome
FileOutcome(const std::vector<Studio::FileOperationResult> &a_files,
            std::uint64_t a_request) {
  if (a_request == 0) {
    return Regression::WorkOutcome::kNone;
  }
  const auto found = std::ranges::find(a_files, a_request,
                                       &Studio::FileOperationResult::requestID);
  if (found == a_files.end()) {
    return Regression::WorkOutcome::kPending;
  }
  switch (found->state) {
  case Studio::FileOperationState::kPending:
    return Regression::WorkOutcome::kPending;
  case Studio::FileOperationState::kSucceeded:
    return Regression::WorkOutcome::kApplied;
  case Studio::FileOperationState::kFailed:
  case Studio::FileOperationState::kCount:
    break;
  }
  return found->error && found->error->message == kFileCanceledByLoad
             ? Regression::WorkOutcome::kCancelledByLoad
             : Regression::WorkOutcome::kFailed;
}

std::string_view GesturePhaseText(Studio::GesturePhase a_phase) {
  const auto index = static_cast<std::size_t>(a_phase);
  return index < Studio::kGesturePhaseNames.size()
             ? Studio::kGesturePhaseNames[index]
             : std::string_view{"unknown"};
}

std::string WorkDetail(const std::vector<Studio::RecipeEditResult> &a_edits,
                       std::uint64_t a_edit,
                       const std::optional<Studio::GestureResult> &a_gesture,
                       std::uint64_t a_gestureID) {
  std::string detail;
  const auto found =
      std::ranges::find(a_edits, a_edit, &Studio::RecipeEditResult::requestID);
  const std::optional<Diagnostic> error =
      a_edit != 0 && found != a_edits.end() ? found->error : std::nullopt;
  if (error) {
    detail = std::format("edit: {}", error->message);
  }
  if (a_gestureID != 0 && a_gesture && a_gesture->gestureID == a_gestureID) {
    detail +=
        std::format("{}gesture {}: {}", detail.empty() ? "" : "; ", a_gestureID,
                    a_gesture->error.value_or(
                        std::string{GesturePhaseText(a_gesture->state)}));
  }
  return detail;
}

std::optional<float> FirstOpacity(const Recipe &a_recipe) {
  for (const Output &output : a_recipe.outputs) {
    const auto *surface = std::get_if<SurfaceOutput>(&output);
    if (!surface || surface->stack.empty()) {
      continue;
    }
    const auto *value = std::get_if<float>(&surface->stack.front().opacity);
    return value ? std::optional<float>{*value} : std::nullopt;
  }
  return std::nullopt;
}

Studio::EditBatch FirstLayerOpacityEdit(float a_opacity) {
  return Studio::EditBatch{{Studio::SetLayerOpacity{0, 0, a_opacity}}};
}
}

Regression::Activity Manager::RegressionActivity(std::uint64_t a_edit,
                                                 std::uint64_t a_gesture,
                                                 std::uint64_t a_file) const {
  Regression::Activity activity;
  const std::vector<Studio::RecipeEditResult> edits = editor_.EditResults();
  const std::vector<Studio::FileOperationResult> files =
      editor_.FileOperations();
  const std::optional<Studio::GestureResult> gesture = editor_.LastGesture();
  activity.editOutcome = EditOutcome(edits, a_edit);
  activity.gestureOutcome = GestureOutcome(gesture, a_gesture);
  activity.fileOutcome = FileOutcome(files, a_file);
  activity.detail = WorkDetail(edits, a_edit, gesture, a_gesture);
  for (const auto &record : applications_.Snapshot()) {
    if (record.phase == ApplicationPhase::kQueued ||
        record.phase == ApplicationPhase::kPrepared) {
      ++activity.pendingApplications;
    }
  }
  const auto &paint = editor_.LastPaintUpdate();
  activity.paintActive = paint && !paint->ended && paint->sessionID != 0;
  activity.gestureActive =
      gesture && gesture->state == Studio::GesturePhase::kActive;
  activity.fileOperationPending =
      std::ranges::any_of(files, [](const Studio::FileOperationResult &a_file) {
        return a_file.state == Studio::FileOperationState::kPending;
      });
  return activity;
}

Regression::RecipeFacts
Manager::RegressionRecipe(std::string_view a_recipe) const {
  Regression::RecipeFacts view;
  const std::span<const Recipe> loaded = LoadedRecipes();
  const Recipe *recipe = FindById(loaded, a_recipe);
  if (!recipe) {
    return view;
  }
  view.loaded = true;
  view.dirty = IsDirty(a_recipe);
  view.firstOpacity = FirstOpacity(*recipe);
  return view;
}

std::uint64_t Manager::StartRegressionEdit(const std::string &a_recipe,
                                           float a_opacity) {
  return editor_.EditRecipe(a_recipe, FirstLayerOpacityEdit(a_opacity));
}

std::uint64_t Manager::StartRegressionGesture(const std::string &a_recipe) {
  const std::string property{kGestureProperty};
  const std::uint64_t gesture = editor_.BeginGesture(
      a_recipe, editor_.DocumentRevisionOf(a_recipe), property);
  editor_.UpdateGesture(gesture, property,
                        FirstLayerOpacityEdit(kGestureOpacity));
  return gesture;
}

std::uint64_t Manager::StartRegressionDuplicate(const std::string &a_from,
                                                const std::string &a_to) {
  return editor_.DuplicateRecipe(a_from, a_to);
}

std::uint64_t Manager::StartRegressionSave(const std::string &a_recipe) {
  return editor_.SaveRecipe(a_recipe);
}

std::uint64_t Manager::StartRegressionDelete(const std::string &a_recipe) {
  return editor_.DeleteRecipe(a_recipe);
}

void Manager::StartRegressionPaint(const std::string &a_recipe) {
  const std::span<const Recipe> loaded = LoadedRecipes();
  const Recipe *recipe = FindById(loaded, a_recipe);
  if (!recipe || recipe->keys.empty()) {
    logger::warn("regression: no loaded recipe '{}' to paint", a_recipe);
    return;
  }
  const std::uint64_t session = ++regressionPaintSession_;
  const auto &last = editor_.LastPaintUpdate();
  const std::uint64_t reset = last && last->ended ? last->revision : 0;
  editor_.BeginPaint(Studio::PaintStartRequest{
      a_recipe, recipe->keys.front(), Surface::kMaterial, session, reset});
  Studio::PaintUpdateRequest update;
  update.sessionID = session;
  update.revision = 1;
  update.expression = std::string{kPaintExpression};
  editor_.UpdatePaint(std::move(update));
}
}
