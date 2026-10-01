// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RegressionRequests.h"

#include "diagnostics/Trace.h"
#include "engine/Manager.h"
#include "engine/RegressionRequest.h"

#include <algorithm>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
std::mutex requestLock;
RegressionRequest request;
std::string recipeUnderTest;
std::optional<Studio::Isolation> isolationBeforeRun;

void Finish(Regression::Outcome a_outcome) {
  request.state = a_outcome;
  Trace::EmitSafely(
      Trace::Event::kCommand,
      {{"action", "regression.result"},
       {"request", std::to_string(request.id)},
       {"actor", std::to_string(request.actor)},
       {"operation", request.kind == RequestKind::kRetire ? "retire" : "apply"},
       {"result", std::string{Regression::OutcomeName(a_outcome)}}});
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

bool FixtureRendered(const LiveActor &a_state, RE::FormID a_armor) {
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

Regression::Outcome RenderedVerdict(const LiveActor *a_state) {
  const RE::TESObjectARMO *armor = RegressionFixture();
  return a_state && armor && FixtureRendered(*a_state, armor->GetFormID())
             ? Regression::Outcome::kPass
             : Regression::Outcome::kBlocked;
}

}

std::optional<std::uint64_t> SubmitRegressionRequest(RE::Actor *a_actor,
                                                     RequestKind a_kind) {
  if (!a_actor)
    return std::nullopt;
  std::optional<std::uint64_t> id;
  {
    const std::lock_guard lock{requestLock};
    id = BeginRequest(request, a_actor->GetFormID(), a_kind);
  }
  if (id)
    Manager::GetSingleton()->QueueRegression(*id);
  return id;
}

Regression::RequestState
RegressionRequestState(std::optional<std::uint64_t> a_request) {
  if (!a_request)
    return Regression::NoRequest{};
  const std::lock_guard lock{requestLock};
  return StateOf(request, *a_request);
}

void AbortRegressionRequest(std::optional<std::uint64_t> a_request) {
  const std::lock_guard lock{requestLock};
  if (a_request && Pending(request, *a_request))
    Finish(Regression::Outcome::kAborted);
}

void SoloRegressionRecipe(std::string a_recipe) {
  {
    const std::lock_guard lock{requestLock};
    recipeUnderTest = a_recipe;
  }
  Manager::GetSingleton()->SoloInEditor(std::move(a_recipe));
}

void EndRegressionSolo() {
  {
    const std::lock_guard lock{requestLock};
    recipeUnderTest.clear();
  }
  Manager::GetSingleton()->EndSoloInEditor();
}

RE::TESObjectARMO *RegressionFixture() {
  RE::TESDataHandler *data = RE::TESDataHandler::GetSingleton();
  return data ? data->LookupForm<RE::TESObjectARMO>(
                    0x803, "BetterEnchantmentEffectsDemo.esp")
              : nullptr;
}

void AbortWaitingRegressionRequest() {
  const std::lock_guard lock{requestLock};
  if (Pending(request))
    Finish(Regression::Outcome::kAborted);
}

void Manager::QueueRegression(std::uint64_t a_request) {
  PostTask([this, a_request] {
    const std::lock_guard lock{requestLock};
    if (!Pending(request, a_request))
      return;
    if (applications_.Loading() || !emissivePathEnabled_) {
      Finish(Regression::Outcome::kBlocked);
      return;
    }
    const auto *actor = RE::TESForm::LookupByID<RE::Actor>(request.actor);
    if (!actor || !actor->Is3DLoaded()) {
      Finish(Regression::Outcome::kBlocked);
      return;
    }
    request.dispatched = true;
    if (request.kind == RequestKind::kRetire) {
      AbandonApplications(request.actor);
      Retire(request.actor);
      Finish(applied_.contains(request.actor) ? Regression::Outcome::kFail
                                              : Regression::Outcome::kPass);
      return;
    }
    for (const auto &record : applications_.Snapshot()) {
      if (record.token.actorID == request.actor)
        request.previousAttempt =
            std::max(request.previousAttempt, record.token.revision);
    }
    if (!applications_.Refresh(request.actor))
      Finish(Regression::Outcome::kFail);
  });
}

void Manager::SoloInEditor(std::string a_recipe) {
  PostTask([this, recipe = std::move(a_recipe)] {
    if (!isolationBeforeRun)
      isolationBeforeRun = editor_.CurrentView().isolation;
    editor_.Isolate(Studio::Isolation::ForRecipe(recipe));
  });
}

void Manager::EndSoloInEditor() {
  PostTask([this] {
    if (!isolationBeforeRun)
      return;
    editor_.Isolate(*isolationBeforeRun);
    isolationBeforeRun.reset();
  });
}

std::vector<RegressionActorFacts>
Manager::RegressionActors(std::span<const RE::FormID> a_actors) const {
  std::vector<RegressionActorFacts> facts(a_actors.size());
  const RE::TESObjectARMO *armor = RegressionFixture();
  const std::vector<ApplicationRecord> records = applications_.Snapshot();
  const std::lock_guard lock{requestLock};
  for (std::size_t i = 0; i < a_actors.size(); ++i) {
    const auto found = applied_.find(a_actors[i]);
    facts[i].live = found != applied_.end();
    const bool rendersFixture =
        facts[i].live && armor &&
        FixtureRendered(found->second, armor->GetFormID());
    const ApplicationRecord *latest = nullptr;
    for (const ApplicationRecord &record : records) {
      if (record.token.actorID == a_actors[i] &&
          (!latest || record.token.revision > latest->token.revision)) {
        latest = &record;
      }
    }
    if (!latest) {
      continue;
    }
    facts[i].application =
        std::format("{} {}{}{}", latest->token.revision,
                    ApplicationPhaseName(latest->phase),
                    latest->problem.empty() ? "" : ": ", latest->problem);
    if (latest->phase == ApplicationPhase::kRendered && rendersFixture) {
      facts[i].renderedAttempt = latest->token.revision;
    }
  }
  return facts;
}

void Manager::ObserveRegression() {
  const std::lock_guard lock{requestLock};
  if (!Pending(request) || !request.dispatched ||
      request.kind == RequestKind::kRetire)
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
      Finish(Regression::Outcome::kFail);
      return;
    case ApplicationPhase::kUnmatched:
      Finish(Regression::Outcome::kBlocked);
      return;
    case ApplicationPhase::kCancelled:
      Finish(Regression::Outcome::kAborted);
      return;
    case ApplicationPhase::kQueued:
    case ApplicationPhase::kPrepared:
      break;
    }
  }
}
}
