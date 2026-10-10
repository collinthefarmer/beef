// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Sync.h"

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
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
namespace {
const Sighting &SightingOf(const TickFacts &a_facts, const World &a_world,
                           ActorId a_actor) {
  static const Sighting unseen;
  if (const auto fact = a_facts.sightings.find(a_actor);
      fact != a_facts.sightings.end()) {
    return fact->second;
  }
  if (const auto record = a_world.actors.find(a_actor);
      record != a_world.actors.end()) {
    return record->second.sight;
  }
  return unseen;
}

std::string NameOf(const World &a_world, ActorId a_actor) {
  if (const auto record = a_world.actors.find(a_actor);
      record != a_world.actors.end() && !record->second.sight.name.empty()) {
    return record->second.sight.name;
  }
  return std::format("{:08X}", FormOf(a_actor));
}

const Actors::ActorPlan &
InstalledPlanOf(const std::optional<Actors::ActorState> &a_state) {
  static const Actors::ActorPlan none;
  return a_state ? a_state->installed : none;
}

bool Observing(std::span<const Command> a_commands, ActorId a_actor) {
  return std::ranges::any_of(a_commands, [a_actor](const Command &a_command) {
    const Observe *observe = Get<Observe>(a_command);
    return observe && observe->actor == a_actor;
  });
}

void ObserveOnce(TickOutput &a_output, ActorId a_actor) {
  if (!Observing(a_output.commands, a_actor)) {
    a_output.commands.emplace_back(Observe{a_actor});
  }
}

void ForgetActor(TickOutput &a_output, ActorId a_actor) {
  a_output.world.actors.erase(a_actor);
  a_output.world.waitingInstalls =
      Removed(std::move(a_output.world.waitingInstalls), FormOf(a_actor));
  a_output.commands.emplace_back(Forget{a_actor});
}

WaitingInstall Refreshed(WaitingInstall a_entry, const Sighting &a_sight) {
  a_entry.role = a_sight.role;
  a_entry.inView = a_sight.role == ActorRole::kPlayer || a_sight.inView;
  a_entry.screenFraction = a_sight.screenFraction;
  return a_entry;
}

std::vector<WaitingInstall> RefreshedWaiting(const TickFacts &a_facts,
                                             const World &a_world) {
  std::vector<WaitingInstall> waiting;
  waiting.reserve(a_world.waitingInstalls.size());
  for (const WaitingInstall &entry : a_world.waitingInstalls) {
    waiting.push_back(
        Refreshed(entry, SightingOf(a_facts, a_world, ActorOf(entry.actor))));
  }
  return waiting;
}

bool Unloaded(const TickFacts &a_facts, ActorId a_actor) {
  const auto fact = a_facts.sightings.find(a_actor);
  return fact != a_facts.sightings.end() && !fact->second.loaded3D;
}

std::vector<const StaleActor *> InSyncOrder(const TickFacts &a_facts,
                                            const World &a_world) {
  std::vector<WaitingInstall> waiting = a_world.waitingInstalls;
  std::vector<const StaleActor *> order;
  const std::uint32_t now = PlannerMs(a_facts.now);
  while (const std::optional<std::size_t> next =
             NextInstall(waiting, FrameSyncs{}, now)) {
    if (*next >= waiting.size()) {
      break;
    }
    const ActorId actor = ActorOf(waiting[*next].actor);
    for (const StaleActor &stale : a_facts.stale) {
      if (stale.actor == actor) {
        order.push_back(&stale);
      }
    }
    waiting.erase(waiting.begin() + static_cast<std::ptrdiff_t>(*next));
  }
  for (const StaleActor &stale : a_facts.stale) {
    if (!std::ranges::contains(order, &stale)) {
      order.push_back(&stale);
    }
  }
  return order;
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
           {"waited_ms", std::to_string(waited)},
           {"waiting", std::to_string(a_world.waitingInstalls.size())}}};
}

struct Decided {
  const StaleActor &stale;
  std::optional<Actors::ActorState> before;
  Actors::Transition next;
  Actors::SyncKind kind = Actors::SyncKind::kUnchanged;
};

Decided Decide(const StaleActor &a_stale, const World &a_world,
               const TickFacts &a_facts) {
  const auto record = a_world.actors.find(a_stale.actor);
  std::optional<Actors::ActorState> before =
      record != a_world.actors.end() ? record->second.syncState : std::nullopt;
  const float evict =
      EffectiveEvictDistance(a_facts.settings.evictDistance, a_world.radius);
  Actors::Transition next =
      Actors::Advance({before, a_stale.facts, a_stale.planned.plan,
                       PlannerMs(a_facts.now), evict, a_stale.reinstall});
  const Actors::SyncKind kind = Actors::KindOf(before, next, a_stale.reinstall);
  return {a_stale, std::move(before), std::move(next), kind};
}

bool Installs(const Decided &a_decided) {
  return !a_decided.next.commands.empty() &&
         Actors::AnythingInstalled(a_decided.next.state);
}

void Defer(TickOutput &a_output, const Decided &a_decided, FrameSyncs a_frame,
           const TickFacts &a_facts) {
  const ActorId actor = a_decided.stale.actor;
  const std::uint32_t now = PlannerMs(a_facts.now);
  World &world = a_output.world;
  world.actors.try_emplace(actor);
  a_output.commands.emplace_back(
      KeepModels{actor, InstalledPlanOf(a_decided.before)});
  world.waitingInstalls =
      Deferred(std::move(world.waitingInstalls), FormOf(actor), now);
  world.waitingInstalls = RefreshedWaiting(a_facts, world);
  a_output.trace.push_back(
      DeferredTrace(actor, a_decided.kind, a_frame, world, now));
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

void Settle(TickOutput &a_output, Decided a_decided, std::uint32_t a_now) {
  const ActorId actor = a_decided.stale.actor;
  const bool waiting = Actors::RecheckOf(a_decided.next.state, a_now) ==
                       Actors::Recheck::kWaiting;
  a_output.commands.emplace_back(
      KeepModels{actor, InstalledPlanOf(a_decided.next.state)});
  a_output.world.actors[actor].syncState = std::move(a_decided.next.state);
  a_output.commands.emplace_back(SettleApplications{actor, !waiting, true});
}

void RetireRender(ActorRecord &a_record) {
  if (!a_record.render) {
    return;
  }
  if (const std::optional<ResolutionLevel> level =
          TargetLevel(a_record.render->phase)) {
    a_record.reinstallLevel = level;
  }
  if (const std::optional<std::uint64_t> bytes = ReturnBytesOf(a_record)) {
    a_record.returnBytes = bytes;
  }
  a_record.render.reset();
}

FrameSyncs Change(TickOutput &a_output, Decided a_decided,
                  const TickFacts &a_facts, FrameSyncs a_frame) {
  const ActorId actor = a_decided.stale.actor;
  const bool installs = Installs(a_decided);
  ActorRecord &record = a_output.world.actors[actor];
  const bool replacing =
      record.render.has_value() || Actors::AnythingInstalled(a_decided.before);
  const std::uint64_t replaced = PlannedBytesOf(record);
  const InstallBound bound =
      InstallBoundOf(record, SightingOf(a_facts, a_output.world, actor),
                     a_output.world.budget, replaced, a_facts.settings);
  a_output.commands.emplace_back(
      KeepModels{actor, InstalledPlanOf(a_decided.next.state)});
  if (replacing) {
    a_output.commands.emplace_back(Retire{actor});
    RetireRender(record);
  }
  record.syncState = std::move(a_decided.next.state);
  if (installs) {
    a_output.commands.emplace_back(
        Install{actor, a_decided.stale.facts, a_decided.stale.planned,
                replacing ? InstallKind::kReinstall : InstallKind::kInstall,
                bound, replaced});
    a_output.world.budget = Released(a_output.world.budget, replaced);
    a_frame = Synced(a_frame, kInstallBudgetUs, true);
  }
  a_output.commands.emplace_back(SettleApplications{actor, false, true});
  return a_frame;
}

FrameSyncs SyncStale(TickOutput &a_output, const StaleActor &a_stale,
                     const TickFacts &a_facts, FrameSyncs a_frame) {
  Decided decided = Decide(a_stale, a_output.world, a_facts);
  if (Installs(decided) && !InstallMayStart(a_frame)) {
    Defer(a_output, decided, a_frame, a_facts);
    return a_frame;
  }
  const ActorId actor = a_stale.actor;
  const std::uint32_t now = PlannerMs(a_facts.now);
  World &world = a_output.world;
  world.waitingInstalls =
      Removed(std::move(world.waitingInstalls), FormOf(actor));
  RecordSync(world, actor, decided.kind, now);
  if (!decided.next.state) {
    ForgetActor(a_output, actor);
    a_output.commands.emplace_back(SettleApplications{actor, true, true});
    return a_frame;
  }
  if (decided.next.commands.empty()) {
    Settle(a_output, std::move(decided), now);
    return a_frame;
  }
  return Change(a_output, std::move(decided), a_facts, a_frame);
}

bool ForgottenThisTick(const TickOutput &a_output, ActorId a_actor) {
  return std::ranges::any_of(a_output.commands, [a_actor](const Command &a_c) {
    const Forget *forget = Get<Forget>(a_c);
    return forget && forget->actor == a_actor;
  });
}

void ForgetUnloaded(TickOutput &a_output, const TickFacts &a_facts) {
  std::vector<ActorId> gone;
  for (const auto &[actor, record] : a_output.world.actors) {
    if (Unloaded(a_facts, actor)) {
      gone.push_back(actor);
    }
  }
  for (const CommandResult &result : a_facts.results) {
    if (const InstallFailed *failed = Get<InstallFailed>(result)) {
      gone.push_back(failed->actor);
    }
  }
  for (const ActorId actor : a_facts.forgets) {
    if (a_output.world.actors.contains(actor)) {
      gone.push_back(actor);
    }
  }
  for (const ActorId actor : gone) {
    if (!ForgottenThisTick(a_output, actor)) {
      ForgetActor(a_output, actor);
    }
  }
}

void ObserveDue(TickOutput &a_output, const TickFacts &a_facts) {
  World &world = a_output.world;
  const std::uint32_t now = PlannerMs(a_facts.now);
  const bool waitingDue =
      a_facts.now - world.timers.lastWaitingCheck >= Actors::kPendingRecheckMs;
  const bool recheckDue =
      a_facts.now - world.timers.lastRecheck >= Actors::kRecheckMs;
  if (waitingDue) {
    world.timers.lastWaitingCheck = a_facts.now;
  }
  if (recheckDue) {
    world.timers.lastRecheck = a_facts.now;
  }
  const float evict =
      EffectiveEvictDistance(a_facts.settings.evictDistance, world.radius);
  std::vector<ActorId> due;
  for (const auto &[actor, record] : world.actors) {
    const Actors::Recheck recheck = Actors::RecheckOf(record.syncState, now);
    const bool waiting = waitingDue && recheck == Actors::Recheck::kWaiting;
    const bool rechecked =
        recheckDue &&
        (recheck != Actors::Recheck::kNone ||
         (record.syncState &&
          RangeChanged(*record.syncState,
                       SightingOf(a_facts, world, actor).distance, evict)));
    if (waiting || rechecked) {
      due.push_back(actor);
    }
  }
  for (const ClaimedGeometry &claimed : a_facts.claims) {
    if (world.actors.contains(claimed.actor)) {
      due.push_back(claimed.actor);
    }
  }
  if (const std::optional<std::size_t> next =
          NextInstall(world.waitingInstalls, FrameSyncs{}, now);
      next && *next < world.waitingInstalls.size()) {
    due.push_back(ActorOf(world.waitingInstalls[*next].actor));
  }
  for (const ActorId actor : due) {
    ObserveOnce(a_output, actor);
  }
}
}

std::uint32_t PlannerMs(Millis a_now) noexcept {
  return static_cast<std::uint32_t>(a_now);
}

std::optional<std::uint64_t>
ReturnBytesOf(const ActorRecord &a_record) noexcept {
  if (!a_record.render) {
    return std::nullopt;
  }
  const InstalledRender &render = *a_record.render;
  std::uint64_t bytes = 0;
  const std::optional<ResolutionLevel> level = TargetLevel(render.phase);
  const std::optional<LevelCost> *cost =
      level ? &render.levelCosts.At(*level) : nullptr;
  if (cost && *cost) {
    bytes = (*cost)->withIntermediates;
  } else {
    bytes = PlannedBytesOf(a_record);
  }
  if (bytes == 0) {
    return std::nullopt;
  }
  return bytes;
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

namespace {
ActorRecord *TrackedRecord(World &a_world, ActorId a_actor) {
  const auto found = a_world.actors.find(a_actor);
  return found == a_world.actors.end() ? nullptr : &found->second;
}

void ChargeInstall(World &a_world, const ActorEvent &a_event) {
  if (const Installed *installed = Get<Installed>(a_event.event)) {
    a_world.budget = Charged(a_world.budget, installed->chargedBytes);
  }
}

void DropFailedInstall(World &a_world, const InstallFailed &a_failed) {
  a_world.waitingInstalls =
      Removed(std::move(a_world.waitingInstalls), FormOf(a_failed.actor));
  if (ActorRecord *record = TrackedRecord(a_world, a_failed.actor)) {
    record->syncState.reset();
  }
}

void RecordRetired(World &a_world, const Retired &a_retired) {
  if (ActorRecord *record = TrackedRecord(a_world, a_retired.actor)) {
    RetireRender(*record);
  }
}

void RecordSyncResult(World &a_world, const CommandResult &a_result) {
  Match(
      a_result,
      [&](const ActorEvent &a_event) { ChargeInstall(a_world, a_event); },
      [&](const InstallFailed &a_failed) {
        DropFailedInstall(a_world, a_failed);
      },
      [&](const Retired &a_retired) { RecordRetired(a_world, a_retired); });
}

void RecordClaim(World &a_world, const ClaimedGeometry &a_claimed) {
  ActorRecord *record = TrackedRecord(a_world, a_claimed.actor);
  if (!record || !record->syncState) {
    return;
  }
  record->syncState =
      Actors::WithClaim(std::move(*record->syncState), a_claimed.claim);
}
}

TickOutput RecordSyncResults(World a_world, const TickFacts &a_facts) {
  for (const CommandResult &result : a_facts.results) {
    RecordSyncResult(a_world, result);
  }

  for (const ClaimedGeometry &claimed : a_facts.claims) {
    RecordClaim(a_world, claimed);
  }

  return {std::move(a_world), {}, {}, {}};
}

TickOutput RunSync(World a_world, const TickFacts &a_facts) {
  TickOutput output{std::move(a_world), {}, {}, {}};
  ForgetUnloaded(output, a_facts);
  output.world.waitingInstalls = RefreshedWaiting(a_facts, output.world);
  FrameSyncs frame = a_facts.frameSyncs;
  for (const StaleActor *stale : InSyncOrder(a_facts, output.world)) {
    if (ForgottenThisTick(output, stale->actor)) {
      continue;
    }
    frame = SyncStale(output, *stale, a_facts, frame);
  }
  ObserveDue(output, a_facts);
  return output;
}
}
