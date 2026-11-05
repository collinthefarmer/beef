// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include "SettingsFile.h"
#include "actors/Lifecycle.h"
#include "actors/Planning.h"
#include "diagnostics/Metrics.h"
#include "diagnostics/Trace.h"
#include "engine/ActorObservation.h"
#include "engine/ActorResize.h"
#include "engine/Clock.h"
#include "engine/LogOnce.h"
#include "engine/PhaseTrace.h"
#include "engine/RecipeStore.h"
#include "engine/RegressionRun.h"
#include "engine/SectionTimer.h"
#include "orchestration/Agreement.h"
#include "planners/ResolutionLevel.h"
#include "planners/ViewFacts.h"

#include <algorithm>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
using Orchestration::ActorOf;
using Orchestration::FormOf;

Orchestration::HeldRestage HeldRestageOf(const LiveActor &a_staged);

bool TakenByAnotherSystem(const LiveGeometry &a_bound) {
  if (a_bound.lost) {
    return false;
  }
  return (a_bound.material && !a_bound.material->StillOwned()) ||
         (a_bound.shell && !a_bound.shell->StillOwned());
}

ActorRole RoleOf(const RE::Actor &a_actor) {
  return a_actor.IsPlayerRef() ? ActorRole::kPlayer : ActorRole::kOther;
}

std::optional<bool> CameraViewOf(RE::Actor &a_actor, ActorRole a_role,
                                 const Settings &a_settings) {
  if (a_role == ActorRole::kPlayer || !a_settings.cullOutOfView) {
    return std::nullopt;
  }
  const SectionTimer cullTimer{Metrics::Section::kCull};
  const bool inView = InCameraView(a_actor);
  Metrics::CountActorView(inView);
  return inView;
}

bool DefersASync(const Orchestration::TraceRecord &a_record) {
  return std::ranges::any_of(a_record.fields, [](const Trace::Field &a_field) {
    return a_field.name == "action" && a_field.value == "sync_deferred";
  });
}

std::string NameOf(const RE::Actor &a_actor) {
  const char *name = a_actor.GetName();
  return name ? std::string{name} : std::string{};
}

Orchestration::TickSettings TickSettingsOf(const Settings &a_settings) {
  Orchestration::TickSettings settings;
  settings.evictDistance = a_settings.evictDistance;
  settings.sizeByScreen =
      RegressionScreenResolution().value_or(a_settings.sizeByScreen);
  settings.publishEffects = a_settings.publishEffects;
  settings.verboseLogging = a_settings.verboseLogging;
  settings.unpublishedDrawBudgetMs = a_settings.unpublishedDrawBudgetMs;
  settings.screenStepDownHoldMs = a_settings.StepDownHoldMS();
  settings.outOfViewReleaseDelayMs = a_settings.HiddenReleaseMS();
  return settings;
}

std::string Joined(const std::vector<std::string> &a_parts) {
  std::string text;
  for (const std::string &part : a_parts) {
    text += text.empty() ? part : "; " + part;
  }
  return text;
}

std::string PhaseText(const Orchestration::World &a_world,
                      Orchestration::ActorId a_actor) {
  const auto found = a_world.actors.find(a_actor);
  if (found == a_world.actors.end()) {
    return "none";
  }
  const std::optional<Orchestration::InstalledRender> &render =
      found->second.render;
  if (!render) {
    return "none";
  }
  return std::string{Orchestration::PhaseName(render->phase)};
}

Orchestration::HeldResources HeldOf(const LiveActor &a_state) {
  Orchestration::HeldResources held;
  if (AttachedRender(a_state)) {
    held.render =
        Orchestration::HeldRender{a_state.renderLevel, a_state.holding};
  }
  if (a_state.firstDraw) {
    held.firstDraw = Orchestration::UnpublishedCount{
        a_state.firstDraw->slots.size(), DrawnSlots(*a_state.firstDraw)};
  }
  if (const ResizeInFlight *resize = InFlightOf(a_state)) {
    held.resize = Orchestration::HeldResize{
        resize->to, {resize->stacks.size(), DrawnStacks(*resize)}};
  }
  if (a_state.awaitedPlan) {
    held.awaited = PlanJobIdOf(*a_state.awaitedPlan);
  }
  if (a_state.restage) {
    held.restage = HeldRestageOf(*a_state.restage);
  }
  return held;
}

Orchestration::HeldRestage HeldRestageOf(const LiveActor &a_staged) {
  Orchestration::HeldRestage held;
  if (a_staged.awaitedPlan) {
    held.awaited = PlanJobIdOf(*a_staged.awaitedPlan);
  }
  if (a_staged.firstDraw) {
    held.draw = Orchestration::UnpublishedCount{
        a_staged.firstDraw->slots.size(), DrawnSlots(*a_staged.firstDraw)};
  }
  return held;
}
}

void Manager::NoteStale(RE::FormID a_actorID,
                        const std::vector<ApplicationToken> &a_tokens) {
  stale_[a_actorID] = a_tokens;
}

GatheredFacts
Manager::GatherFacts(std::uint32_t a_nowMS, const Settings &a_settings,
                     std::vector<Orchestration::PlanJobDone> a_jobs) {
  tickClock_ = ClockAfter(tickClock_, a_nowMS);
  GatheredFacts inputs;
  inputs.catalog =
      AppliedCatalog(a_settings.showDebugRecipes ? DebugRecipes::kShown
                                                 : DebugRecipes::kHidden);
  inputs.waiting = world_.waitingInstalls;
  inputs.observedAtMS = NowMS();
  Orchestration::TickFacts &facts = inputs.facts;
  facts.now = tickClock_.now;
  facts.frame = renderFrame_;
  facts.settings = TickSettingsOf(a_settings);
  facts.results = std::exchange(results_, {});
  facts.jobs = std::move(a_jobs);
  facts.planSlots = FreePlanSlots(planQueue_.Unfinished());
  facts.forgets = ForgetRequests();
  facts.claims = ClaimsOf();
  facts.budgetSample = std::exchange(pendingSample_, std::nullopt);
  for (auto &[id, tokens] : std::exchange(stale_, {})) {
    facts.stale.push_back(
        StaleActorOf(id, *inputs.catalog, a_settings, inputs.observedAtMS));
    inputs.tokens[id] = std::move(tokens);
  }
  const std::optional<RE::FormID> inspected = InspectedActor(a_nowMS);
  for (const auto &[actor, record] : world_.actors) {
    facts.sightings[actor] = SightingOf(FormOf(actor), a_settings, inspected);
  }
  for (const Orchestration::StaleActor &stale : facts.stale) {
    if (!facts.sightings.contains(stale.actor)) {
      facts.sightings[stale.actor] =
          SightingOf(FormOf(stale.actor), a_settings, inspected);
    }
  }
  return inputs;
}

Orchestration::Sighting
Manager::SightingOf(RE::FormID a_actorID, const Settings &a_settings,
                    std::optional<RE::FormID> a_inspected) const {
  Orchestration::Sighting sighting;
  const RE::NiPointer<RE::Actor> actor{
      RE::TESForm::LookupByID<RE::Actor>(a_actorID)};
  if (!actor) {
    return sighting;
  }
  sighting.role = RoleOf(*actor);
  sighting.finest =
      a_inspected == a_actorID
          ? ResolutionLevel::kFull
          : FromIndex<ResolutionLevel>(IndexOf(a_settings.npcResolution));
  sighting.loaded3D = Loaded3D(*actor);
  sighting.name = NameOf(*actor);
  sighting.distance = DistanceToPlayer(*actor);
  if (!sighting.loaded3D) {
    return sighting;
  }
  sighting.shown = InViewOf({sighting.role, a_settings.cullOutOfView,
                             a_inspected == a_actorID,
                             CameraViewOf(*actor, sighting.role, a_settings)});
  sighting.screenFraction =
      ScreenFractionOf(ScreenFraction(*actor), a_settings.sizeByScreen,
                       RegressionScreenResolution());
  return sighting;
}

Orchestration::StaleActor Manager::StaleActorOf(RE::FormID a_actorID,
                                                const RecipeCatalog &a_catalog,
                                                const Settings &a_settings,
                                                std::uint32_t a_observedAtMS) {
  const Orchestration::ActorId id = ActorOf(a_actorID);
  const RE::NiPointer<RE::Actor> actor{
      RE::TESForm::LookupByID<RE::Actor>(a_actorID)};
  if (!actor) {
    return {id, Actors::ActorFacts{id, false, std::nullopt, {}}, {}};
  }
  const MaterialLayout layoutBefore = materialLayout_;
  PhaseTrace phases{Trace::Event::kApplication, "actor_decide", a_actorID};
  Actors::ActorFacts facts = phases.Measure("observe_us", [&] {
    return ObserveActor({*actor, a_settings,
                         a_settings.enableShaders && emissivePathEnabled_,
                         a_catalog.Recipes(), a_observedAtMS},
                        models_[a_actorID], materialLayout_);
  });
  NoteMaterialLayout(layoutBefore);
  if (a_settings.verboseLogging) {
    NoteArmorWithoutGeometry(facts);
  }
  Actors::PlannedActor planned = phases.Measure("plan_us", [&] {
    const auto record = world_.actors.find(id);
    const std::vector<Actors::Claim> claims =
        record != world_.actors.end() && record->second.syncState
            ? Actors::LiveClaims(record->second.syncState->claims, facts)
            : std::vector<Actors::Claim>{};
    return Actors::PlanActor({facts, a_catalog, editor_.CurrentView(), claims});
  });
  phases.Add("pieces", std::to_string(facts.pieces.size()));
  phases.Add("planned", std::to_string(planned.plan.placements.size()));
  phases.Emit();
  const Actors::Reinstall reinstall = applications_.ReinstallDue(a_actorID)
                                          ? Actors::Reinstall::kAlways
                                          : Actors::Reinstall::kIfChanged;
  return {id, std::move(facts), std::move(planned), reinstall};
}

std::vector<Orchestration::ClaimedGeometry> Manager::ClaimsOf() {
  std::vector<Orchestration::ClaimedGeometry> claims;
  for (auto &[id, state] : live_) {
    for (LivePiece &piece : state.pieces) {
      for (LiveGeometry &bound : piece.geometries) {
        if (!TakenByAnotherSystem(bound)) {
          continue;
        }
        logger::info("claimed '{}': its material or shell was replaced by "
                     "another system",
                     bound.name);
        RetireGeometry(state, bound);
        claims.push_back({ActorOf(id), {bound.ref, piece.model}});
      }
    }
  }
  return claims;
}

std::vector<Orchestration::ActorId> Manager::ForgetRequests() {
  std::vector<Orchestration::ActorId> requests;
  for (const RE::FormID id : std::exchange(forgets_, {})) {
    if (world_.actors.contains(ActorOf(id))) {
      requests.push_back(ActorOf(id));
      continue;
    }
    ForgetActorResources(id);
  }
  return requests;
}

void Manager::CheckAgreement(const Orchestration::TickFacts &a_facts) {
  const Orchestration::World settled =
      Orchestration::SettledWorld(world_, a_facts);
  std::map<Orchestration::ActorId, Orchestration::HeldResources> held;
  for (const auto &[id, state] : live_) {
    held.emplace(ActorOf(id), HeldOf(state));
  }
  std::map<RE::FormID, std::vector<std::string>> found;
  for (Orchestration::Disagreement &disagreement :
       Orchestration::Disagreements(settled, held)) {
    found[FormOf(disagreement.actor)].push_back(std::move(disagreement.what));
  }
  for (const auto &[id, what] : found) {
    const auto before = disagreements_.find(id);
    if (before != disagreements_.end() && before->second == what) {
      continue;
    }
    ReportDisagreement(id, {"phase", PhaseText(settled, ActorOf(id))},
                       Joined(what));
  }
  disagreements_ = std::move(found);
}

void Manager::ReportDisagreement(RE::FormID a_actorID, Trace::Field a_where,
                                 std::string a_what) {
  ++disagreementCount_;
  LogOnce(warningHistory_, Severity::kWarning,
          std::format("actor {:08X} ({} {}): resources disagree with the tick "
                      "core: {}",
                      a_actorID, a_where.name, a_where.value, a_what));
  Trace::EmitSafely(Trace::Event::kMetrics,
                    {{"action", "resource_disagreement"},
                     {"actor", std::to_string(a_actorID)},
                     std::move(a_where),
                     {"what", std::move(a_what)}});
}

void Manager::EmitCoreRecords(
    const std::vector<Orchestration::TraceRecord> &a_trace,
    const std::vector<Orchestration::LogRecord> &a_logs) {
  for (const Orchestration::TraceRecord &record : a_trace) {
    if (DefersASync(record)) {
      Metrics::CountSyncDeferral();
    }
    Trace::EmitSafely(record.event, record.fields);
  }
  for (const Orchestration::LogRecord &line : a_logs) {
    if (line.level == Orchestration::LogLevel::kWarning) {
      logger::warn("{}", line.text);
    } else {
      logger::info("{}", line.text);
    }
  }
}
}
