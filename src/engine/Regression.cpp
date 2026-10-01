// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Regression.h"

#include "diagnostics/Trace.h"
#include "engine/Manager.h"
#include "engine/RecipeOperations.h"
#include "engine/RecipeStore.h"
#include "engine/RegressionRequest.h"

#include <algorithm>
#include <format>
#include <mutex>
#include <optional>
#include <string>

namespace BetterEnchantmentEffects {
namespace {
std::mutex requestLock;
RegressionRequest request;
std::string recipeUnderTest;
std::optional<Studio::Isolation> isolationBeforeRun;

void Finish(std::string a_result) {
  request.result = std::move(a_result);
  Trace::EmitSafely(Trace::Event::kCommand,
                    {{"action", "regression.result"},
                     {"request", std::to_string(request.id)},
                     {"actor", std::to_string(request.actor)},
                     {"operation", request.retire ? "retire" : "apply"},
                     {"result", request.result}});
}

bool InstanceUnderTest(const LiveActor &a_state, std::size_t a_instance) {
  if (recipeUnderTest.empty()) {
    return true;
  }
  return a_instance < a_state.instances.size() &&
         a_state.instances[a_instance].recipe &&
         a_state.instances[a_instance].recipe->id == recipeUnderTest;
}

bool PlacementRendered(const LiveActor &a_state, PlacementId a_placement) {
  const std::optional<ResolvedPlacement> resolved =
      ResolvePlacement(a_state, a_placement);
  if (!resolved || !InstanceUnderTest(a_state, resolved->instance) ||
      resolved->placement >= a_state.placements.size()) {
    return false;
  }
  return std::ranges::any_of(a_state.placements[resolved->placement].outputs,
                             [](const PlacedOutput &a_output) {
                               return a_output.stack && a_output.rendered &&
                                      !a_output.renderFailed;
                             });
}

bool GeometryRendered(const LiveActor &a_state,
                      const LiveGeometry &a_geometry) {
  return std::ranges::any_of(a_geometry.placements,
                             [&](PlacementId a_placement) {
                               return PlacementRendered(a_state, a_placement);
                             });
}

bool DemoArmorRendered(const LiveActor &a_state, RE::FormID a_armor) {
  for (const LivePiece &piece : a_state.pieces) {
    if (piece.armor != a_armor) {
      continue;
    }
    for (const LiveGeometry &geometry : piece.geometries) {
      if (GeometryRendered(a_state, geometry)) {
        return true;
      }
    }
  }
  return false;
}

std::string RenderedVerdict(const LiveActor *a_state) {
  const RE::TESObjectARMO *armor = RegressionFixture();
  if (!a_state || !armor) {
    return "BLOCKED";
  }
  return DemoArmorRendered(*a_state, armor->GetFormID()) ? "PASS" : "BLOCKED";
}

}

std::int32_t SubmitRegressionRequest(RE::Actor *a_actor, bool a_retire) {
  if (!a_actor)
    return 0;
  std::int32_t id = 0;
  {
    const std::lock_guard lock{requestLock};
    id = request.Begin(a_actor->GetFormID(), a_retire);
  }
  if (id)
    Manager::GetSingleton()->QueueRegression(id);
  return id;
}

std::string RegressionResult(std::int32_t a_request) {
  const std::lock_guard lock{requestLock};
  return request.Result(a_request);
}

void AbortRegressionRequest(std::int32_t a_request) {
  const std::lock_guard lock{requestLock};
  if (request.id == a_request)
    Finish("ABORTED");
}

void SoloRecipeUnderTest(std::string a_recipe) {
  {
    const std::lock_guard lock{requestLock};
    recipeUnderTest = a_recipe;
  }
  Manager::GetSingleton()->SoloRegressionRecipe(std::move(a_recipe));
}

void RestoreRecipeView() {
  {
    const std::lock_guard lock{requestLock};
    recipeUnderTest.clear();
  }
  Manager::GetSingleton()->RestoreRegressionView();
}

RE::TESObjectARMO *RegressionFixture() {
  RE::TESDataHandler *data = RE::TESDataHandler::GetSingleton();
  return data ? data->LookupForm<RE::TESObjectARMO>(
                    0x803, "BetterEnchantmentEffectsDemo.esp")
              : nullptr;
}

void CancelRegression() {
  const std::lock_guard lock{requestLock};
  if (request.Pending(request.id))
    Finish("ABORTED");
}

void Manager::QueueRegression(std::int32_t a_request) {
  PostTask([this, a_request] {
    const std::lock_guard lock{requestLock};
    if (!request.Pending(a_request))
      return;
    if (applications_.Loading() || !emissivePathEnabled_) {
      Finish("BLOCKED");
      return;
    }
    const auto *actor = RE::TESForm::LookupByID<RE::Actor>(request.actor);
    if (!actor || !actor->Is3DLoaded()) {
      Finish("BLOCKED");
      return;
    }
    request.dispatched = true;
    if (request.retire) {
      AbandonApplications(request.actor);
      Retire(request.actor);
      Finish(applied_.contains(request.actor) ? "FAIL" : "PASS");
      return;
    }
    for (const auto &record : applications_.Snapshot()) {
      if (record.token.actorID == request.actor)
        request.previousAttempt =
            std::max(request.previousAttempt, record.token.revision);
    }
    if (!applications_.Refresh(request.actor))
      Finish("FAIL");
  });
}

void Manager::SoloRegressionRecipe(std::string a_recipe) {
  PostTask([this, recipe = std::move(a_recipe)] {
    if (!isolationBeforeRun)
      isolationBeforeRun = editor_.CurrentView().isolation;
    editor_.Isolate(Studio::Isolation::ForRecipe(recipe));
  });
}

void Manager::RestoreRegressionView() {
  PostTask([this] {
    if (!isolationBeforeRun)
      return;
    editor_.Isolate(*isolationBeforeRun);
    isolationBeforeRun.reset();
  });
}

RegressionActorFacts Manager::RegressionActor(RE::FormID a_actor) const {
  RegressionActorFacts facts;
  const auto found = applied_.find(a_actor);
  facts.live = found != applied_.end();
  const RE::TESObjectARMO *armor = RegressionFixture();
  const bool rendersFixture =
      facts.live && armor &&
      DemoArmorRendered(found->second, armor->GetFormID());
  const std::lock_guard lock{requestLock};
  std::uint64_t latest = 0;
  for (const auto &record : applications_.Snapshot()) {
    if (record.token.actorID != a_actor || record.token.revision < latest) {
      continue;
    }
    latest = record.token.revision;
    facts.application = std::format(
        "{} {}{}{}", record.token.revision, ApplicationPhaseName(record.phase),
        record.problem.empty() ? "" : ": ", record.problem);
    if (record.phase == ApplicationPhase::kRendered && rendersFixture) {
      facts.renderedAttempt = record.token.revision;
    }
  }
  return facts;
}

namespace {
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
             : Regression::WorkOutcome::kOther;
}

Regression::WorkOutcome
TuningOutcome(const std::optional<Studio::GestureResult> &a_gesture,
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
             : Regression::WorkOutcome::kOther;
}
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

Regression::WorkOutcome
FileOutcome(const std::vector<Studio::FileOperationResult> &a_files,
            std::uint64_t a_request) {
  if (a_request == 0) {
    return Regression::WorkOutcome::kNone;
  }
  const auto found = std::ranges::find(a_files, a_request,
                                       &Studio::FileOperationResult::requestID);
  if (found == a_files.end()) {
    return Regression::WorkOutcome::kNone;
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
             : Regression::WorkOutcome::kOther;
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

Regression::Activity Manager::RegressionActivity(std::uint64_t a_edit,
                                                 std::uint64_t a_gesture,
                                                 std::uint64_t a_file) const {
  Regression::Activity facts;
  facts.file = FileOutcome(editor_.FileOperations(), a_file);
  facts.edit = EditOutcome(editor_.EditResults(), a_edit);
  facts.tuning = TuningOutcome(editor_.LastGesture(), a_gesture);
  facts.detail = WorkDetail(editor_.EditResults(), a_edit,
                            editor_.LastGesture(), a_gesture);
  for (const auto &record : applications_.Snapshot()) {
    if (record.phase == ApplicationPhase::kQueued ||
        record.phase == ApplicationPhase::kPrepared) {
      ++facts.applications;
    }
  }
  const auto &paint = editor_.LastPaintUpdate();
  facts.paint = paint && !paint->ended && paint->sessionID != 0;
  const auto gesture = editor_.LastGesture();
  facts.gesture = gesture && gesture->state == Studio::GesturePhase::kActive;
  facts.fileOperations = std::ranges::any_of(
      editor_.FileOperations(), [](const Studio::FileOperationResult &a_file) {
        return a_file.state == Studio::FileOperationState::kPending;
      });
  return facts;
}

namespace {
constexpr float kRegressionOpacity = 0.5f;
constexpr std::string_view kRegressionProperty = "regression";
constexpr std::string_view kRegressionPaint = "1";

Studio::EditBatch RegressionOpacityEdit(float a_opacity) {
  return Studio::EditBatch{{Studio::SetLayerOpacity{0, 0, a_opacity}}};
}
}

std::uint64_t Manager::StartRegressionEdit(const std::string &a_recipe,
                                           float a_opacity) {
  return editor_.EditRecipe(a_recipe, RegressionOpacityEdit(a_opacity));
}

std::uint64_t Manager::StartRegressionGesture(const std::string &a_recipe) {
  const std::string property{kRegressionProperty};
  const std::uint64_t gesture = editor_.BeginGesture(
      a_recipe, editor_.DocumentRevisionOf(a_recipe), property);
  editor_.UpdateGesture(gesture, property,
                        RegressionOpacityEdit(kRegressionOpacity));
  return gesture;
}

Regression::RecipeView
Manager::RegressionRecipe(std::string_view a_recipe) const {
  Regression::RecipeView view;
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
  static std::uint64_t nextSession = 1;
  const std::uint64_t session = nextSession++;
  const auto &last = editor_.LastPaintUpdate();
  const std::uint64_t reset = last && last->ended ? last->revision : 0;
  editor_.BeginPaint(Studio::PaintStartRequest{
      a_recipe, recipe->keys.front(), Surface::kMaterial, session, reset});
  Studio::PaintUpdateRequest update;
  update.sessionID = session;
  update.revision = 1;
  update.expression = std::string{kRegressionPaint};
  editor_.UpdatePaint(std::move(update));
}

void Manager::ObserveRegression() {
  const std::lock_guard lock{requestLock};
  if (!request.Pending(request.id) || !request.dispatched || request.retire)
    return;
  for (const auto &record : applications_.Snapshot()) {
    if (record.token.actorID != request.actor ||
        record.token.revision <= request.previousAttempt)
      continue;
    switch (record.phase) {
    case ApplicationPhase::kRendered: {
      const auto found = applied_.find(request.actor);
      Finish(
          RenderedVerdict(found != applied_.end() ? &found->second : nullptr));
      return;
    }
    case ApplicationPhase::kFailed:
      Finish("FAIL");
      return;
    case ApplicationPhase::kUnmatched:
      Finish("BLOCKED");
      return;
    case ApplicationPhase::kCancelled:
      Finish("ABORTED");
      return;
    case ApplicationPhase::kQueued:
    case ApplicationPhase::kPrepared:
      break;
    }
  }
}
}
