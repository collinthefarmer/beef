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
                   const PlacedOutput &a_output, bool a_rendered) {
  if (!a_output.active) {
    return;
  }
  ++a_result.outputs;
  if (!a_output.problem.empty()) {
    a_result.problem = a_output.problem;
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
    if (p >= a_state.plan.placements.size()) {
      continue;
    }
    const auto i =
        static_cast<std::size_t>(a_state.plan.placements[p].instance);
    if (i >= a_state.instances.size() ||
        !Includes(a_token, a_state.instances[i])) {
      continue;
    }
    if (!a_state.instances[i].signals || !a_state.instances[i].environment) {
      result.problem = "recipe signals could not be prepared";
    }
    for (const PlacedOutput &output : a_state.placements[p].outputs) {
      ObserveOutput(result, output, a_rendered);
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
  for (const auto &[id, state] : applied_)
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

void Manager::ChangeAndRebuildActors(std::string a_reportRecipe,
                                     const std::function<void()> &a_action) {
  const ApplicationToken token =
      applications_.Begin(std::move(a_reportRecipe), ApplicationActors());
  const auto actors = applications_.ActorsFor(token.recipeID);
  for (const RE::FormID actor : actors) {
    Retire(actor);
  }
  a_action();
  TraceRebuildState(token, editor_.CurrentView());
  for (const RE::FormID actor : actors) {
    if (!applications_.Refresh(actor)) {
      applications_.Report(token, actor, ApplicationPhase::kFailed,
                           "actor refresh could not be queued");
    }
  }
}

void Manager::PrepareApplications(
    RE::FormID a_actor, const std::vector<ApplicationToken> &a_tokens) {
  const auto found = applied_.find(a_actor);
  for (const ApplicationToken &token : a_tokens) {
    if (found == applied_.end()) {
      applications_.Report(
          token, a_actor, ApplicationPhase::kUnmatched,
          "no applicable loaded geometry, or rendering is disabled");
      continue;
    }
    const auto observed = ObserveApplication(found->second, token, false);
    applications_.Report(token, a_actor, PhaseOf(observed, false),
                         observed.problem);
  }
  if (found != applied_.end()) {
    found->second.applications = a_tokens;
  }
}

void Manager::FinishApplications(RE::FormID a_actor, LiveActor &a_state) {
  if (a_state.applications.empty()) {
    return;
  }
  for (const ApplicationToken &token : a_state.applications) {
    const auto observed = ObserveApplication(a_state, token, true);
    applications_.Report(token, a_actor, PhaseOf(observed, true),
                         observed.problem);
  }
  a_state.applications = applications_.PendingFor(a_actor);
}

void Manager::AbandonApplications(RE::FormID a_actor) {
  for (const auto &token : applications_.PendingFor(a_actor)) {
    applications_.Report(token, a_actor, ApplicationPhase::kUnmatched,
                         "the actor was retired before rendering completed");
  }
}
}
