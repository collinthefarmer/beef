// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Regression.h"

#include "diagnostics/Trace.h"
#include "engine/Manager.h"
#include "engine/RegressionRequest.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <optional>

namespace BetterEnchantmentEffects {
namespace {
std::mutex requestLock;
RegressionRequest request;
const std::string processSession =
    std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
std::uint64_t sessionGeneration = 0;
std::string recipeUnderTest;
std::optional<Studio::Isolation> isolationBeforeRun;

std::string Session(RE::StaticFunctionTag *) {
  const std::lock_guard lock{requestLock};
  return processSession + ":" + std::to_string(sessionGeneration);
}

void Finish(std::string a_result) {
  request.result = std::move(a_result);
  Trace::EmitSafely(Trace::Event::kCommand,
                    {{"action", "regression.result"},
                     {"request", std::to_string(request.id)},
                     {"actor", std::to_string(request.actor)},
                     {"operation", request.retire ? "retire" : "apply"},
                     {"result", request.result}});
}

std::int32_t Submit(RE::StaticFunctionTag *, RE::Actor *a_actor,
                    bool a_retire) {
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

std::string Result(RE::StaticFunctionTag *, std::int32_t a_request) {
  const std::lock_guard lock{requestLock};
  return request.Result(a_request);
}

void Abort(RE::StaticFunctionTag *, std::int32_t a_request) {
  const std::lock_guard lock{requestLock};
  if (request.id == a_request)
    Finish("ABORTED");
}

void Mark(RE::StaticFunctionTag *, std::int32_t a_request,
          std::string a_checkpoint, bool a_pass) {
  const std::lock_guard lock{requestLock};
  if (a_request != request.id || request.result != "PASS" ||
      a_checkpoint.size() > 64)
    return;
  Trace::EmitSafely(Trace::Event::kCommand,
                    {{"action", "regression.visual"},
                     {"request", std::to_string(a_request)},
                     {"checkpoint", a_checkpoint},
                     {"result", a_pass ? "PASS" : "FAIL"}});
}

void Solo(RE::StaticFunctionTag *, std::string a_recipe) {
  {
    const std::lock_guard lock{requestLock};
    recipeUnderTest = a_recipe;
  }
  Manager::GetSingleton()->SoloRegressionRecipe(std::move(a_recipe));
}

void RestoreView(RE::StaticFunctionTag *) {
  {
    const std::lock_guard lock{requestLock};
    recipeUnderTest.clear();
  }
  Manager::GetSingleton()->RestoreRegressionView();
}

bool Bind(RE::BSScript::IVirtualMachine *a_vm) {
  if (!a_vm)
    return false;
  a_vm->RegisterFunction("Session", "BEEFRegressionNative", Session);
  a_vm->RegisterFunction("Submit", "BEEFRegressionNative", Submit);
  a_vm->RegisterFunction("Result", "BEEFRegressionNative", Result);
  a_vm->RegisterFunction("Abort", "BEEFRegressionNative", Abort);
  a_vm->RegisterFunction("Mark", "BEEFRegressionNative", Mark);
  a_vm->RegisterFunction("Solo", "BEEFRegressionNative", Solo);
  a_vm->RegisterFunction("RestoreView", "BEEFRegressionNative", RestoreView);
  return true;
}
}

bool RegisterRegression() {
  const auto *papyrus = SKSE::GetPapyrusInterface();
  return papyrus && papyrus->Register(Bind);
}

void CancelRegression() {
  const std::lock_guard lock{requestLock};
  ++sessionGeneration;
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
      auto *data = RE::TESDataHandler::GetSingleton();
      const auto *armor = data ? data->LookupForm<RE::TESObjectARMO>(
                                     0x803, "BetterEnchantmentEffectsDemo.esp")
                               : nullptr;
      bool rendered = false;
      if (found != applied_.end() && armor) {
        const LiveActor &state = found->second;
        for (const LivePiece &piece : state.pieces) {
          if (piece.armor != armor->GetFormID())
            continue;
          for (const LiveGeometry &geometry : piece.geometries) {
            for (const PlacementId placement : geometry.placements) {
              const auto resolved = ResolvePlacement(state, placement);
              if (!resolved)
                continue;
              if (!recipeUnderTest.empty() &&
                  (resolved->instance >= state.instances.size() ||
                   !state.instances[resolved->instance].recipe ||
                   state.instances[resolved->instance].recipe->id !=
                       recipeUnderTest))
                continue;
              for (const PlacedOutput &output :
                   state.placements[resolved->placement].outputs)
                rendered |=
                    output.stack && output.rendered && !output.renderFailed;
            }
          }
        }
      }
      Finish(rendered ? "PASS" : "BLOCKED");
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
