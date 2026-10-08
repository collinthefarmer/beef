// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/Trace.h"
#include "engine/Manager.h"

#include "SettingsFile.h"
#include "engine/RecipeStore.h"

#include <algorithm>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
void TraceRebuildState(const ApplicationToken &a_token,
                       const Studio::View &a_view) {
  Trace::Safely([&] {
    Trace::Emit(
        Trace::Event::kApplication,
        {{"action", "rebuild_state"},
         {"recipe", a_token.recipeID},
         {"revision", std::to_string(a_token.revision)},
         {"isolation", a_view.isolation.recipeID},
         {"output", a_view.isolation.output
                        ? std::to_string(*a_view.isolation.output)
                        : "none"},
         {"layer", a_view.isolation.layer
                       ? std::to_string(*a_view.isolation.layer)
                       : "none"},
         {"pin", a_view.pin ? a_view.pin->recipeID : "none"},
         {"pin_actor",
          a_view.pin ? std::to_string(a_view.pin->piece.actorID) : "none"},
         {"pin_armor",
          a_view.pin ? std::to_string(a_view.pin->piece.armorID) : "none"},
         {"muted", std::to_string(a_view.muted.size())}});
    for (const Recipe &recipe : LoadedRecipes()) {
      if (!a_token.recipeID.empty() && recipe.id != a_token.recipeID) {
        continue;
      }
      Trace::Emit(Trace::Event::kRecipe,
                  {{"id", recipe.id},
                   {"fingerprint_fnv1a64",
                    Trace::Fingerprint(SerializeRecipe(recipe))}});
    }
  });
}

struct ApplicationObservation {
  std::size_t outputs = 0;
  bool waiting = false;
  std::string problem;
};

bool Includes(const ApplicationToken &a_token, const LiveInstance &a_instance) {
  return a_instance.recipe && (a_token.recipeID.empty() ||
                               a_instance.recipe->id == a_token.recipeID);
}

void ObserveOutput(ApplicationObservation &a_result,
                   const PlacedOutput &a_output, bool a_rendered,
                   bool a_planAwaited) {
  if (!a_output.active) {
    return;
  }
  ++a_result.outputs;
  if (!a_output.problem.empty()) {
    a_result.problem = a_output.problem;
  } else if (!a_output.stack && a_planAwaited) {
    a_result.waiting = true;
  } else if (!a_output.stack) {
    a_result.problem = "the output was not prepared";
  } else if (a_rendered && a_output.renderFailed) {
    a_result.problem = "the output could not be rendered; reapply to retry";
  } else if (a_rendered && !a_output.rendered) {
    a_result.waiting = true;
  }
}

ApplicationObservation ObserveApplication(const LiveActor &a_state,
                                          const ApplicationToken &a_token,
                                          bool a_rendered) {
  ApplicationObservation result;
  for (std::size_t p = 0; p < a_state.placements.size(); ++p) {
    const std::optional<ResolvedPlacement> resolved =
        ResolvePlacement(a_state, PlacementId{p});
    if (!resolved) {
      continue;
    }
    const LiveInstance &instance = a_state.instances[resolved->instance];
    if (!Includes(a_token, instance)) {
      continue;
    }
    if (!instance.signals || !instance.environment) {
      result.problem = "recipe signals could not be prepared";
    }
    for (const PlacedOutput &output :
         a_state.placements[resolved->placement].outputs) {
      ObserveOutput(result, output, a_rendered,
                    a_state.awaitedPlan.has_value());
    }
  }
  for (const LiveInstance &instance : a_state.instances) {
    if (!Includes(a_token, instance) || !instance.lightOutput) {
      continue;
    }
    ++result.outputs;
    if (!instance.light) {
      result.problem = "the light could not be bound";
    }
  }
  return result;
}

ApplicationPhase PhaseOf(const ApplicationObservation &a_result,
                         bool a_rendered) {
  if (!a_result.problem.empty()) {
    return ApplicationPhase::kFailed;
  }
  if (a_result.outputs == 0) {
    return ApplicationPhase::kUnmatched;
  }
  return a_rendered && !a_result.waiting ? ApplicationPhase::kRendered
                                         : ApplicationPhase::kPrepared;
}
}

std::vector<RE::FormID> Manager::ApplicationActors() const {
  auto ids = LoadedActorIDs();
  for (const auto &[id, state] : live_)
    ids.push_back(id);
  std::ranges::sort(ids);
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

std::vector<RE::FormID> Manager::LoadedActorIDs() const {
  std::vector<RE::FormID> ids;
  if (const auto *player = RE::PlayerCharacter::GetSingleton()) {
    ids.push_back(player->GetFormID());
  }
  if (GetSettings().playerOnly) {
    return ids;
  }
  RE::ProcessLists *lists = RE::ProcessLists::GetSingleton();
  if (!lists) {
    return ids;
  }
  lists->ForEachHighActor([&](RE::Actor *a_actor) {
    if (a_actor) {
      ids.push_back(a_actor->GetFormID());
    }
    return RE::BSContainer::ForEachResult::kContinue;
  });
  return ids;
}

std::vector<RE::FormID>
Manager::ActorsApplying(std::string_view a_recipeID) const {
  std::vector<RE::FormID> ids = applications_.FailedActorsFor(a_recipeID);
  for (const auto &[id, state] : live_) {
    if (std::ranges::any_of(state.instances, [&](const LiveInstance &a_live) {
          return a_live.recipe && a_live.recipe->id == a_recipeID;
        })) {
      ids.push_back(id);
    }
  }
  std::ranges::sort(ids);
  ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
  return ids;
}

void Manager::ChangeAndRebuildActors(std::string a_reportRecipe,
                                     const std::function<void()> &a_action) {
  const std::vector<MatchFacts> matchingBefore = MatchFactsOf(LoadedRecipes());
  a_action();
  const RebuildScope rebuild =
      ScopeOf(matchingBefore, MatchFactsOf(LoadedRecipes()), a_reportRecipe);
  std::vector<RE::FormID> scope = Match(
      rebuild,
      [&](const RebuildAll &) {
        renderPlans_.Clear();
        planJobs_ = {};
        return ApplicationActors();
      },
      [&](const RebuildApplying &a_applying) {
        return ActorsApplying(a_applying.recipe);
      });
  const ApplicationToken token =
      applications_.Begin(std::move(a_reportRecipe), std::move(scope));
  const auto actors = applications_.ActorsFor(token.recipeID);
  TraceRebuildState(token, editor_.CurrentView());
  for (const RE::FormID actor : actors) {
    if (!applications_.MarkReinstall(actor)) {
      ForgetActor(actor);
      applications_.Report(token, actor, ApplicationPhase::kFailed,
                           "actor sync could not be queued");
    }
  }
}

void Manager::PrepareApplications(
    RE::FormID a_actor, const std::vector<ApplicationToken> &a_tokens) {
  const auto found = live_.find(a_actor);
  for (const ApplicationToken &token : a_tokens) {
    if (found == live_.end()) {
      applications_.Report(
          token, a_actor, ApplicationPhase::kUnmatched,
          "no applicable loaded geometry, or rendering is disabled");
      continue;
    }
    const auto observed = ObserveApplication(found->second, token, false);
    applications_.Report(token, a_actor, PhaseOf(observed, false),
                         observed.problem);
  }
  if (found != live_.end()) {
    found->second.applications = a_tokens;
  }
}

void Manager::FinishApplications(RE::FormID a_actor, LiveActor &a_state) {
  if (a_state.applications.empty()) {
    return;
  }
  for (const ApplicationToken &token : a_state.applications) {
    const auto observed = ObserveApplication(a_state, token, true);
    const ApplicationPhase phase = PhaseOf(observed, true);
    applications_.Report(token, a_actor,
                         phase == ApplicationPhase::kPrepared && !a_state.inView
                             ? ApplicationPhase::kOutOfView
                             : phase,
                         observed.problem);
  }
  a_state.applications = applications_.PendingFor(a_actor);
}

void Manager::AbandonApplications(RE::FormID a_actor) {
  applications_.Retire(a_actor, applications_.PendingFor(a_actor));
}
}
