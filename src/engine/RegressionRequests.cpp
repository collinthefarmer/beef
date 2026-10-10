// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RegressionRequests.h"

#include "diagnostics/Trace.h"
#include "engine/Manager.h"
#include "engine/RecipeStore.h"
#include "engine/RegressionRequest.h"
#include "orchestration/World.h"

#include <algorithm>
#include <format>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
constexpr std::uint32_t kRetireTicks = 10;

std::mutex requestLock;
RegressionRequest request;
std::string recipeUnderTest;
std::optional<Studio::Isolation> isolationBeforeRun;
std::optional<std::vector<std::string>> recipeSetBeforeRun;

void TraceRecipeFingerprints(std::span<const std::string> a_recipes) {
  const std::span<const Recipe> loaded = LoadedRecipes();
  std::vector<Trace::Field> fields{{"action", "regression.recipes"}};
  for (const std::string &id : a_recipes) {
    const Recipe *recipe = FindById(loaded, id);
    std::string print = recipe ? Trace::Fingerprint(SerializeRecipe(*recipe))
                               : std::string{"missing"};
    logger::info("regression: recipe set member {} {}", id, print);
    fields.push_back({id, std::move(print)});
  }
  Trace::EmitSafely(Trace::Event::kOperation, fields);
}

void Finish(Regression::Outcome a_outcome) {
  request.state = a_outcome;
  Trace::EmitSafely(
      Trace::Event::kOperation,
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
      forgets_.insert(request.actor);
      return;
    }
    for (const auto &record : applications_.Snapshot()) {
      if (record.token.actorID == request.actor)
        request.previousAttempt =
            std::max(request.previousAttempt, record.token.revision);
    }
    if (!applications_.MarkStale(request.actor))
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
    if (isolationBeforeRun) {
      editor_.Isolate(*isolationBeforeRun);
      isolationBeforeRun.reset();
    }
    if (recipeSetBeforeRun) {
      editor_.ShowRecipeSet(*recipeSetBeforeRun);
      recipeSetBeforeRun.reset();
    }
  });
}

void Manager::ShowRecipeSetInEditor(std::vector<std::string> a_recipes) {
  PostTask([this, recipes = std::move(a_recipes)]() mutable {
    TraceRecipeFingerprints(recipes);
    if (!recipeSetBeforeRun)
      recipeSetBeforeRun = editor_.CurrentView().recipeSet;
    editor_.ShowRecipeSet(std::move(recipes));
  });
}

void ShowRegressionRecipeSet(std::vector<std::string> a_recipes) {
  Manager::GetSingleton()->ShowRecipeSetInEditor(std::move(a_recipes));
}

std::vector<WearerFacts>
Manager::RegressionActors(std::span<const RegressionWearer> a_wearers) const {
  std::vector<WearerFacts> facts(a_wearers.size());
  const std::vector<ApplicationRecord> records = applications_.Snapshot();
  const std::lock_guard lock{requestLock};
  for (std::size_t i = 0; i < a_wearers.size(); ++i) {
    const auto found = live_.find(a_wearers[i].actor);
    facts[i].live = found != live_.end();
    const auto record =
        world_.actors.find(Orchestration::ActorOf(a_wearers[i].actor));
    facts[i].inView = !facts[i].live || (record != world_.actors.end() &&
                                         record->second.sight.inView);
    if (facts[i].live) {
      for (const LivePiece &piece : found->second.pieces) {
        if (!std::ranges::contains(facts[i].liveItems, piece.armor)) {
          facts[i].liveItems.push_back(piece.armor);
        }
      }
    }
    const bool rendersFixture =
        facts[i].live && a_wearers[i].armor &&
        FixtureRendered(found->second, a_wearers[i].armor);
    const ApplicationRecord *latest = nullptr;
    for (const ApplicationRecord &record : records) {
      if (record.token.actorID == a_wearers[i].actor &&
          (!latest || record.token.revision > latest->token.revision)) {
        latest = &record;
      }
    }
    if (!latest) {
      continue;
    }
    facts[i].awaitingView = latest->phase == ApplicationPhase::kOutOfView;
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
  if (!Pending(request) || !request.dispatched)
    return;
  if (request.kind == RequestKind::kRetire) {
    if (!forgets_.contains(request.actor) && !live_.contains(request.actor) &&
        !world_.actors.contains(Orchestration::ActorOf(request.actor))) {
      Finish(Regression::Outcome::kPass);
    } else if (++request.retireTicks > kRetireTicks) {
      Finish(Regression::Outcome::kFail);
    }
    return;
  }
  for (const auto &record : applications_.Snapshot()) {
    if (record.token.actorID != request.actor ||
        record.token.revision <= request.previousAttempt)
      continue;
    switch (record.phase) {
    case ApplicationPhase::kRendered: {
      const auto found = live_.find(request.actor);
      Finish(RenderedVerdict(found != live_.end() ? &found->second : nullptr));
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
    case ApplicationPhase::kOutOfView:
      break;
    }
  }
}
}
