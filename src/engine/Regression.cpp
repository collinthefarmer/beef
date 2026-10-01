// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Regression.h"

#include "diagnostics/Trace.h"
#include "engine/Manager.h"
#include "engine/RegressionRequest.h"

#include <algorithm>
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
  if (request.id > 0)
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
