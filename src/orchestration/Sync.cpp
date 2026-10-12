// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Sync.h"
#include "orchestration/Output.h"

#include "Core.h"
#include "actors/Lifecycle.h"
#include "actors/SyncLog.h"
#include "orchestration/Budget.h"
#include "orchestration/Tick.h"
#include "planners/BudgetRadius.h"
#include "planners/InstallPacing.h"
#include "planners/ResolutionLevel.h"

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
namespace {
MaybeActor ActorIf(bool a_keep, ActorId a_actor) noexcept {
  if (!a_keep) {
    return std::nullopt;
  }
  return a_actor;
}

void ClearWaitingInstall(World &a_world, ActorId a_actor) {
  a_world.waitingInstalls =
      Removed(std::move(a_world.waitingInstalls), FormOf(a_actor));
}

const Sighting &LatestSighting(const World &a_world, const TickFacts &a_facts,
                               ActorId a_actor) {
  static const Sighting unseen;
  if (const Sighting *sighted = Lookup(a_facts.sightings, a_actor)) {
    return *sighted;
  }
  if (const TrackedActor *record = FindActor(a_world, a_actor)) {
    return record->sight;
  }
  return unseen;
}

const Actors::ActorPlan &InstalledPlanOf(const SyncState &a_state) {
  static const Actors::ActorPlan none;
  return a_state ? a_state->installed : none;
}

void Insert(ActorSet &a_into, const ActorList &a_actors) {
  a_into.insert(a_actors.begin(), a_actors.end());
}

void Insert(ActorSet &a_into, MaybeActor a_actor) {
  if (!a_actor) {
    return;
  }
  a_into.insert(*a_actor);
}

void DropRender(TrackedActor &a_record) {
  if (!a_record.render) {
    return;
  }
  a_record.reinstallLevel =
      Coalesce(TargetLevel(a_record.render->phase), a_record.reinstallLevel);
  a_record.returnBytes =
      Coalesce(ReturnBytesOf(a_record), a_record.returnBytes);
  a_record.render.reset();
}

struct SyncDecision {
  const StaleActor &stale;
  SyncState before;
  Actors::Transition next;
  Actors::SyncKind kind = Actors::SyncKind::kUnchanged;
};

struct ChangePlan {
  bool replaces = false;
  bool installs = false;
  std::uint64_t replacedBytes = 0;
  InstallBounds bounds;
};

enum class SyncOutcome : std::uint8_t { kDefer, kForget, kSettle, kChange };

struct DueChecks {
  bool loadingModels = false;
  bool slowChecks = false;
};

void ApplySyncResult(World &a_world, const CommandResult &a_result);
void ChargeInstall(World &a_world, const ActorEvent &a_event);
void DropFailedInstall(World &a_world, const InstallFailed &a_failed);
void ForgetRetiredRender(World &a_world, const Retired &a_retired);
void AddClaim(World &a_world, const ClaimedGeometry &a_claimed);
ActorSet SelectGone(const World &a_world, const TickFacts &a_facts);
ActorList UnloadedActors(const World &a_world, const TickFacts &a_facts);
bool Unloaded(const TickFacts &a_facts, ActorId a_actor);
ActorList FailedInstalls(const TickFacts &a_facts);
ActorList RequestedForgets(const World &a_world, const TickFacts &a_facts);
void ForgetGone(TickOutput &a_output, const ActorSet &a_gone,
                const TickFacts &a_facts);
void ForgetActor(TickOutput &a_output, ActorId a_actor,
                 const TickFacts &a_facts);
void ApplyForget(TickOutput &a_output, ActorId a_actor);
std::optional<PlanJobId> AwaitedJobOf(const World &a_world, ActorId a_actor);
void CancelUnawaitedJob(TickOutput &a_output, std::optional<PlanJobId> a_job,
                        const TickFacts &a_facts);
bool IsAwaited(const World &a_world, PlanJobId a_job);
bool IsReportedDone(const TickFacts &a_facts, PlanJobId a_job);
std::vector<WaitingInstall> RefreshedWaiting(const TickFacts &a_facts,
                                             const World &a_world);
WaitingInstall Refreshed(WaitingInstall a_entry, const Sighting &a_sight);
std::uint32_t FormOfStale(const StaleActor &a_stale) noexcept;
FrameSyncs SyncStale(TickOutput &a_output, const StaleActor &a_stale,
                     const TickFacts &a_facts, FrameSyncs a_frame);
SyncDecision DecideSync(const StaleActor &a_stale, const World &a_world,
                        const TickFacts &a_facts);
SyncState SyncStateOf(const World &a_world, ActorId a_actor);
float EvictDistanceOf(const World &a_world, const TickFacts &a_facts) noexcept;
SyncOutcome OutcomeOf(const SyncDecision &a_decision,
                      FrameSyncs a_frame) noexcept;
bool WouldInstall(const SyncDecision &a_decision);
void Defer(TickOutput &a_output, const SyncDecision &a_decision,
           FrameSyncs a_frame, const TickFacts &a_facts);
void ApplyDeferred(TickOutput &a_output, ActorId a_actor,
                   const Sighting &a_sight, std::uint32_t a_now);
TraceRecord DeferredTrace(ActorId a_actor, Actors::SyncKind a_kind,
                          FrameSyncs a_frame, const World &a_world,
                          std::uint32_t a_now);
std::string PlanSlotsText(std::optional<std::size_t> a_slots);
void ApplySyncDone(TickOutput &a_output, ActorId a_actor,
                   Actors::SyncKind a_kind, std::uint32_t a_now);
void RecordSync(World &a_world, ActorId a_actor, Actors::SyncKind a_kind,
                std::uint32_t a_now);
std::string NameOf(const World &a_world, ActorId a_actor);
void ForgetWithoutPlan(TickOutput &a_output, ActorId a_actor,
                       const TickFacts &a_facts);
void Settle(TickOutput &a_output, SyncDecision a_decision, std::uint32_t a_now);
void ApplySyncState(TickOutput &a_output, ActorId a_actor, SyncState a_state);
FrameSyncs Change(TickOutput &a_output, SyncDecision a_decision,
                  const TickFacts &a_facts, FrameSyncs a_frame);
ChangePlan PlanChange(const World &a_world, const SyncDecision &a_decision,
                      const TickFacts &a_facts);
void RetireForChange(TickOutput &a_output, ActorId a_actor,
                     const TickFacts &a_facts);
void InstallForChange(TickOutput &a_output, const SyncDecision &a_decision,
                      const ChangePlan &a_plan);
void ObserveDue(TickOutput &a_output, const TickFacts &a_facts);
DueChecks DueChecksAt(const Timers &a_timers, Millis a_now) noexcept;
void ApplyTimers(TickOutput &a_output, DueChecks a_checks, Millis a_now);
ActorSet SelectObserved(const World &a_world, const TickFacts &a_facts,
                        DueChecks a_checks);
ActorList ActorsWithLoadingModels(const World &a_world,
                                  const TickFacts &a_facts, DueChecks a_checks);
ActorList ActorsWhere(const World &a_world, const TickFacts &a_facts,
                      bool (*a_rule)(Actors::ModelLoad));
bool ModelsLoading(Actors::ModelLoad a_load) noexcept;
ActorList ActorsWithMissingModels(const World &a_world,
                                  const TickFacts &a_facts, DueChecks a_checks);
bool ModelsMissing(Actors::ModelLoad a_load) noexcept;
ActorList ActorsCrossingEvictRange(const World &a_world,
                                   const TickFacts &a_facts,
                                   DueChecks a_checks);
ActorList ActorsWithClaimedGeometry(const World &a_world,
                                    const TickFacts &a_facts);
MaybeActor NextDeferredInstall(const World &a_world, const TickFacts &a_facts);
MaybeBytes TargetLevelBytes(const InstalledRender &a_render) noexcept;
}

TickOutput RecordSyncResults(World a_world, const TickFacts &a_facts) {
  for (const CommandResult &result : a_facts.results) {
    ApplySyncResult(a_world, result);
  }

  for (const ClaimedGeometry &claimed : a_facts.claims) {
    AddClaim(a_world, claimed);
  }

  return {std::move(a_world), {}, {}, {}};
}

namespace {
void ApplySyncResult(World &a_world, const CommandResult &a_result) {
  Match(
      a_result,
      [&](const ActorEvent &a_event) { ChargeInstall(a_world, a_event); },
      [&](const InstallFailed &a_failed) {
        DropFailedInstall(a_world, a_failed);
      },
      [&](const Retired &a_retired) {
        ForgetRetiredRender(a_world, a_retired);
      });
}

void ChargeInstall(World &a_world, const ActorEvent &a_event) {
  const Installed *installed = Get<Installed>(a_event.event);
  if (!installed) {
    return;
  }
  a_world.budget = Charged(a_world.budget, installed->chargedBytes);
}

void DropFailedInstall(World &a_world, const InstallFailed &a_failed) {
  ClearWaitingInstall(a_world, a_failed.actor);
  TrackedActor *record = FindActor(a_world, a_failed.actor);
  if (!record) {
    return;
  }
  record->syncState.reset();
}

void ForgetRetiredRender(World &a_world, const Retired &a_retired) {
  TrackedActor *record = FindActor(a_world, a_retired.actor);
  if (!record) {
    return;
  }
  DropRender(*record);
}

void AddClaim(World &a_world, const ClaimedGeometry &a_claimed) {
  TrackedActor *record = FindActor(a_world, a_claimed.actor);
  if (!record || !record->syncState) {
    return;
  }
  record->syncState =
      Actors::WithClaim(std::move(*record->syncState), a_claimed.claim);
}
}

TickOutput RunSync(World a_world, const TickFacts &a_facts) {
  TickOutput output{std::move(a_world), {}, {}, {}};
  const ActorSet gone = SelectGone(output.world, a_facts);
  ForgetGone(output, gone, a_facts);
  output.world.waitingInstalls = RefreshedWaiting(a_facts, output.world);

  FrameSyncs frame{.planSlots = a_facts.planSlots};
  const FormIds installOrder =
      InstallOrder(output.world.waitingInstalls, PlannerMs(a_facts.now));
  for (const StaleActor *stale :
       OrderedBy(a_facts.stale, installOrder, FormOfStale)) {
    if (gone.contains(stale->actor)) {
      continue;
    }
    frame = SyncStale(output, *stale, a_facts, frame);
  }

  ObserveDue(output, a_facts);
  return output;
}

namespace {
ActorSet SelectGone(const World &a_world, const TickFacts &a_facts) {
  ActorSet gone;
  Insert(gone, UnloadedActors(a_world, a_facts));
  Insert(gone, FailedInstalls(a_facts));
  Insert(gone, RequestedForgets(a_world, a_facts));
  return gone;
}

ActorList UnloadedActors(const World &a_world, const TickFacts &a_facts) {
  return Choose(a_world.actors, [&](const auto &a_entry) {
    return ActorIf(Unloaded(a_facts, a_entry.first), a_entry.first);
  });
}

bool Unloaded(const TickFacts &a_facts, ActorId a_actor) {
  const auto fact = a_facts.sightings.find(a_actor);
  return fact != a_facts.sightings.end() && !fact->second.loaded3D;
}

ActorList FailedInstalls(const TickFacts &a_facts) {
  return Choose(a_facts.results, [](const CommandResult &a_result) {
    const InstallFailed *failed = Get<InstallFailed>(a_result);
    if (!failed) {
      return MaybeActor{};
    }
    return MaybeActor{failed->actor};
  });
}

ActorList RequestedForgets(const World &a_world, const TickFacts &a_facts) {
  return Choose(a_facts.forgets, [&](ActorId a_actor) {
    return ActorIf(a_world.actors.contains(a_actor), a_actor);
  });
}

void ForgetGone(TickOutput &a_output, const ActorSet &a_gone,
                const TickFacts &a_facts) {
  for (const ActorId actor : a_gone) {
    ForgetActor(a_output, actor, a_facts);
  }
}

void ForgetActor(TickOutput &a_output, ActorId a_actor,
                 const TickFacts &a_facts) {
  const std::optional<PlanJobId> job = AwaitedJobOf(a_output.world, a_actor);
  ApplyForget(a_output, a_actor);
  Issue(a_output, Forget{a_actor});
  CancelUnawaitedJob(a_output, job, a_facts);
}

void ApplyForget(TickOutput &a_output, ActorId a_actor) {
  a_output.world.actors.erase(a_actor);
  ClearWaitingInstall(a_output.world, a_actor);
}

std::optional<PlanJobId> AwaitedJobOf(const World &a_world, ActorId a_actor) {
  const TrackedActor *record = FindActor(a_world, a_actor);
  if (!record || !record->render) {
    return std::nullopt;
  }
  return AwaitedJob(record->render->phase);
}

void CancelUnawaitedJob(TickOutput &a_output, std::optional<PlanJobId> a_job,
                        const TickFacts &a_facts) {
  if (!a_job || IsAwaited(a_output.world, *a_job) ||
      IsReportedDone(a_facts, *a_job)) {
    return;
  }
  Issue(a_output, CancelPlanJob{*a_job});
}

bool IsAwaited(const World &a_world, PlanJobId a_job) {
  return std::ranges::any_of(a_world.actors, [&](const auto &a_entry) {
    return AwaitedJobOf(a_world, a_entry.first) == a_job;
  });
}

bool IsReportedDone(const TickFacts &a_facts, PlanJobId a_job) {
  return std::ranges::any_of(a_facts.jobs, [&](const PlanJobDone &a_done) {
    return JobOf(a_done) == a_job;
  });
}

std::vector<WaitingInstall> RefreshedWaiting(const TickFacts &a_facts,
                                             const World &a_world) {
  return Map(a_world.waitingInstalls, [&](const WaitingInstall &a_entry) {
    return Refreshed(a_entry,
                     LatestSighting(a_world, a_facts, ActorOf(a_entry.actor)));
  });
}

WaitingInstall Refreshed(WaitingInstall a_entry, const Sighting &a_sight) {
  a_entry.role = a_sight.role;
  a_entry.shown = a_sight.shown;
  a_entry.screenFraction = a_sight.screenFraction;
  return a_entry;
}

std::uint32_t FormOfStale(const StaleActor &a_stale) noexcept {
  return FormOf(a_stale.actor);
}

FrameSyncs SyncStale(TickOutput &a_output, const StaleActor &a_stale,
                     const TickFacts &a_facts, FrameSyncs a_frame) {
  SyncDecision decision = DecideSync(a_stale, a_output.world, a_facts);
  const SyncOutcome outcome = OutcomeOf(decision, a_frame);
  if (outcome == SyncOutcome::kDefer) {
    Defer(a_output, decision, a_frame, a_facts);
    return a_frame;
  }

  const std::uint32_t now = PlannerMs(a_facts.now);
  ApplySyncDone(a_output, a_stale.actor, decision.kind, now);

  switch (outcome) {
  case SyncOutcome::kForget:
    ForgetWithoutPlan(a_output, a_stale.actor, a_facts);
    return a_frame;
  case SyncOutcome::kSettle:
    Settle(a_output, std::move(decision), now);
    return a_frame;
  case SyncOutcome::kChange:
    return Change(a_output, std::move(decision), a_facts, a_frame);
  case SyncOutcome::kDefer:
    return a_frame;
  }
  return a_frame;
}

SyncDecision DecideSync(const StaleActor &a_stale, const World &a_world,
                        const TickFacts &a_facts) {
  SyncState before = SyncStateOf(a_world, a_stale.actor);
  Actors::Transition next =
      Actors::Advance({.state = before,
                       .facts = a_stale.facts,
                       .plan = a_stale.planned.plan,
                       .nowMs = PlannerMs(a_facts.now),
                       .evictDistance = EvictDistanceOf(a_world, a_facts),
                       .reinstall = a_stale.reinstall});
  const Actors::SyncKind kind = Actors::KindOf(before, next, a_stale.reinstall);
  return {a_stale, std::move(before), std::move(next), kind};
}

SyncState SyncStateOf(const World &a_world, ActorId a_actor) {
  const TrackedActor *record = FindActor(a_world, a_actor);
  return record ? record->syncState : std::nullopt;
}

float EvictDistanceOf(const World &a_world, const TickFacts &a_facts) noexcept {
  return EffectiveEvictDistance(a_facts.settings.evictDistance, a_world.radius);
}

SyncOutcome OutcomeOf(const SyncDecision &a_decision,
                      FrameSyncs a_frame) noexcept {
  if (WouldInstall(a_decision) && !CanInstallThisTick(a_frame)) {
    return SyncOutcome::kDefer;
  }
  if (!a_decision.next.state) {
    return SyncOutcome::kForget;
  }
  if (a_decision.next.commands.empty()) {
    return SyncOutcome::kSettle;
  }
  return SyncOutcome::kChange;
}

bool WouldInstall(const SyncDecision &a_decision) {
  return !a_decision.next.commands.empty() &&
         Actors::AnythingInstalled(a_decision.next.state);
}

void Defer(TickOutput &a_output, const SyncDecision &a_decision,
           FrameSyncs a_frame, const TickFacts &a_facts) {
  const ActorId actor = a_decision.stale.actor;
  const std::uint32_t now = PlannerMs(a_facts.now);

  ApplyDeferred(a_output, actor, LatestSighting(a_output.world, a_facts, actor),
                now);

  Issue(a_output, KeepModels{actor, InstalledPlanOf(a_decision.before)});
  AddTrace(a_output,
           DeferredTrace(actor, a_decision.kind, a_frame, a_output.world, now));
}

void ApplyDeferred(TickOutput &a_output, ActorId a_actor,
                   const Sighting &a_sight, std::uint32_t a_now) {
  World &world = a_output.world;
  world.actors.try_emplace(a_actor);
  world.waitingInstalls =
      Deferred(std::move(world.waitingInstalls), FormOf(a_actor), a_now);
  for (WaitingInstall &entry : world.waitingInstalls) {
    if (entry.actor == FormOf(a_actor)) {
      entry = Refreshed(entry, a_sight);
    }
  }
}

TraceRecord DeferredTrace(ActorId a_actor, Actors::SyncKind a_kind,
                          FrameSyncs a_frame, const World &a_world,
                          std::uint32_t a_now) {
  const std::uint32_t waited =
      WaitedMS(a_world.waitingInstalls, FormOf(a_actor), a_now).value_or(0);
  return {Trace::Event::kQueue,
          {{"action", "sync_deferred"},
           {"actor", std::to_string(FormOf(a_actor))},
           {"kind", std::string{Actors::SyncKindName(a_kind)}},
           {"frame_us", std::to_string(a_frame.spentUs)},
           {"frame_installs", std::to_string(a_frame.installs)},
           {"plan_slots", PlanSlotsText(a_frame.planSlots)},
           {"waited_ms", std::to_string(waited)},
           {"waiting", std::to_string(a_world.waitingInstalls.size())}}};
}

std::string PlanSlotsText(std::optional<std::size_t> a_slots) {
  return a_slots ? std::to_string(*a_slots) : std::string{"unlimited"};
}

void ApplySyncDone(TickOutput &a_output, ActorId a_actor,
                   Actors::SyncKind a_kind, std::uint32_t a_now) {
  World &world = a_output.world;
  ClearWaitingInstall(world, a_actor);
  RecordSync(world, a_actor, a_kind, a_now);
}

void RecordSync(World &a_world, ActorId a_actor, Actors::SyncKind a_kind,
                std::uint32_t a_now) {
  if (a_kind == Actors::SyncKind::kUnchanged) {
    return;
  }
  a_world.syncLog = Actors::Recorded(
      std::move(a_world.syncLog),
      {FormOf(a_actor), NameOf(a_world, a_actor), a_kind, a_now});
}

std::string NameOf(const World &a_world, ActorId a_actor) {
  const TrackedActor *record = FindActor(a_world, a_actor);
  if (!record || record->sight.name.empty()) {
    return std::format("{:08X}", FormOf(a_actor));
  }
  return record->sight.name;
}

void ForgetWithoutPlan(TickOutput &a_output, ActorId a_actor,
                       const TickFacts &a_facts) {
  ForgetActor(a_output, a_actor, a_facts);
  Issue(a_output, SettleApplications{a_actor, true, true});
}

void Settle(TickOutput &a_output, SyncDecision a_decision,
            std::uint32_t a_now) {
  const ActorId actor = a_decision.stale.actor;
  const bool modelsLoading =
      Actors::ModelLoadOf(a_decision.next.state, a_now) ==
      Actors::ModelLoad::kLoading;
  const Actors::ActorPlan installed = InstalledPlanOf(a_decision.next.state);

  ApplySyncState(a_output, actor, std::move(a_decision.next.state));

  Issue(a_output, KeepModels{actor, installed});
  Issue(a_output, SettleApplications{.actor = actor,
                                     .equip = !modelsLoading,
                                     .reinstall = true});
}

void ApplySyncState(TickOutput &a_output, ActorId a_actor, SyncState a_state) {
  a_output.world.actors[a_actor].syncState = std::move(a_state);
}

FrameSyncs Change(TickOutput &a_output, SyncDecision a_decision,
                  const TickFacts &a_facts, FrameSyncs a_frame) {
  const ActorId actor = a_decision.stale.actor;
  const ChangePlan plan = PlanChange(a_output.world, a_decision, a_facts);

  Issue(a_output, KeepModels{actor, InstalledPlanOf(a_decision.next.state)});
  if (plan.replaces) {
    RetireForChange(a_output, actor, a_facts);
  }
  ApplySyncState(a_output, actor, std::move(a_decision.next.state));
  if (plan.installs) {
    InstallForChange(a_output, a_decision, plan);
  }
  Issue(a_output, SettleApplications{actor, false, true});

  return plan.installs ? Synced(a_frame, kInstallBudgetUs, true) : a_frame;
}

ChangePlan PlanChange(const World &a_world, const SyncDecision &a_decision,
                      const TickFacts &a_facts) {
  static const TrackedActor untracked{};
  const ActorId actor = a_decision.stale.actor;
  const TrackedActor *tracked = FindActor(a_world, actor);
  const TrackedActor &record = tracked ? *tracked : untracked;
  const std::uint64_t replaced = PlannedBytesOf(record);
  return {.replaces = record.render.has_value() ||
                      Actors::AnythingInstalled(a_decision.before),
          .installs = WouldInstall(a_decision),
          .replacedBytes = replaced,
          .bounds =
              InstallBoundsOf(record, LatestSighting(a_world, a_facts, actor),
                              a_world.budget, replaced, a_facts.settings)};
}

void RetireForChange(TickOutput &a_output, ActorId a_actor,
                     const TickFacts &a_facts) {
  const std::optional<PlanJobId> job = AwaitedJobOf(a_output.world, a_actor);
  Issue(a_output, Retire{a_actor});
  DropRender(a_output.world.actors[a_actor]);
  CancelUnawaitedJob(a_output, job, a_facts);
}

void InstallForChange(TickOutput &a_output, const SyncDecision &a_decision,
                      const ChangePlan &a_plan) {
  const StaleActor &stale = a_decision.stale;
  Issue(a_output, Install{stale.actor, stale.facts, stale.planned,
                          a_plan.replaces ? InstallKind::kReinstall
                                          : InstallKind::kInstall,
                          a_plan.bounds, a_plan.replacedBytes});
  ApplyBudget(a_output, Released(a_output.world.budget, a_plan.replacedBytes));
}

void ObserveDue(TickOutput &a_output, const TickFacts &a_facts) {
  const DueChecks checks = DueChecksAt(a_output.world.timers, a_facts.now);
  ApplyTimers(a_output, checks, a_facts.now);

  for (const ActorId actor : SelectObserved(a_output.world, a_facts, checks)) {
    Issue(a_output, Observe{actor});
  }
}

DueChecks DueChecksAt(const Timers &a_timers, Millis a_now) noexcept {
  return {.loadingModels =
              a_now - a_timers.lastLoadingCheck >= Actors::kLoadingCheckMs,
          .slowChecks = a_now - a_timers.lastSlowCheck >= Actors::kSlowCheckMs};
}

void ApplyTimers(TickOutput &a_output, DueChecks a_checks, Millis a_now) {
  Timers &timers = a_output.world.timers;
  if (a_checks.loadingModels) {
    timers.lastLoadingCheck = a_now;
  }
  if (a_checks.slowChecks) {
    timers.lastSlowCheck = a_now;
  }
}

ActorSet SelectObserved(const World &a_world, const TickFacts &a_facts,
                        DueChecks a_checks) {
  ActorSet observed;
  Insert(observed, ActorsWithLoadingModels(a_world, a_facts, a_checks));
  Insert(observed, ActorsWithMissingModels(a_world, a_facts, a_checks));
  Insert(observed, ActorsCrossingEvictRange(a_world, a_facts, a_checks));
  Insert(observed, ActorsWithClaimedGeometry(a_world, a_facts));
  Insert(observed, NextDeferredInstall(a_world, a_facts));
  return observed;
}

ActorList ActorsWithLoadingModels(const World &a_world,
                                  const TickFacts &a_facts,
                                  DueChecks a_checks) {
  if (!a_checks.loadingModels) {
    return {};
  }
  return ActorsWhere(a_world, a_facts, ModelsLoading);
}

ActorList ActorsWhere(const World &a_world, const TickFacts &a_facts,
                      bool (*a_rule)(Actors::ModelLoad)) {
  const std::uint32_t now = PlannerMs(a_facts.now);
  return Choose(a_world.actors, [&](const auto &a_entry) {
    const auto &[actor, record] = a_entry;
    return ActorIf(a_rule(Actors::ModelLoadOf(record.syncState, now)), actor);
  });
}

bool ModelsLoading(Actors::ModelLoad a_load) noexcept {
  return a_load == Actors::ModelLoad::kLoading;
}

ActorList ActorsWithMissingModels(const World &a_world,
                                  const TickFacts &a_facts,
                                  DueChecks a_checks) {
  if (!a_checks.slowChecks) {
    return {};
  }
  return ActorsWhere(a_world, a_facts, ModelsMissing);
}

bool ModelsMissing(Actors::ModelLoad a_load) noexcept {
  return a_load != Actors::ModelLoad::kNone;
}

ActorList ActorsCrossingEvictRange(const World &a_world,
                                   const TickFacts &a_facts,
                                   DueChecks a_checks) {
  if (!a_checks.slowChecks) {
    return {};
  }
  const float evict = EvictDistanceOf(a_world, a_facts);
  return Choose(a_world.actors, [&](const auto &a_entry) {
    const auto &[actor, record] = a_entry;
    const Sighting &sight = LatestSighting(a_world, a_facts, actor);
    return ActorIf(record.syncState &&
                       RangeChanged(*record.syncState, sight.distance, evict),
                   actor);
  });
}

ActorList ActorsWithClaimedGeometry(const World &a_world,
                                    const TickFacts &a_facts) {
  return Choose(a_facts.claims, [&](const ClaimedGeometry &a_claimed) {
    return ActorIf(a_world.actors.contains(a_claimed.actor), a_claimed.actor);
  });
}

MaybeActor NextDeferredInstall(const World &a_world, const TickFacts &a_facts) {
  const std::optional<std::size_t> next = NextInstall(
      a_world.waitingInstalls, FrameSyncs{}, PlannerMs(a_facts.now));
  if (!next || *next >= a_world.waitingInstalls.size()) {
    return std::nullopt;
  }
  return ActorOf(a_world.waitingInstalls[*next].actor);
}
}

std::uint32_t PlannerMs(Millis a_now) noexcept {
  return static_cast<std::uint32_t>(a_now);
}

MaybeBytes ReturnBytesOf(const TrackedActor &a_record) noexcept {
  if (!a_record.render) {
    return std::nullopt;
  }
  const std::uint64_t bytes =
      TargetLevelBytes(*a_record.render).value_or(PlannedBytesOf(a_record));
  if (bytes == 0) {
    return std::nullopt;
  }
  return bytes;
}

namespace {
MaybeBytes TargetLevelBytes(const InstalledRender &a_render) noexcept {
  const std::optional<ResolutionLevel> level = TargetLevel(a_render.phase);
  if (!level) {
    return std::nullopt;
  }
  const std::optional<LevelCost> &cost = a_render.levelCosts.At(*level);
  if (!cost) {
    return std::nullopt;
  }
  return cost->withIntermediates;
}
}

bool RangeChanged(const Actors::ActorState &a_state,
                  std::optional<float> a_distance, float a_evictDistance) {
  const bool installed = !(a_state.installed == Actors::ActorPlan{});
  if (!installed && a_state.plan == Actors::ActorPlan{}) {
    return false;
  }
  return a_distance &&
         Actors::InRange(installed, a_distance, a_evictDistance) != installed;
}
}
