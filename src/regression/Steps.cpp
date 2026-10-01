// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/Steps.h"

#include "Core.h"

#include <format>
#include <utility>

namespace BetterEnchantmentEffects::Regression {
namespace {
inline constexpr std::uint32_t kActionDeadlineFrames = 600;
inline constexpr std::uint32_t kRenderDeadlineFrames = 1800;
inline constexpr std::uint32_t kCrowdDeadlineFrames = 3600;
inline constexpr std::uint32_t kMaxFramesPerSecond = 600;
inline constexpr std::uint32_t kTravelDeadlineFrames = 3600;
inline constexpr std::uint32_t kExpectDeadlineFrames = 600;

std::size_t ItemIndex(Item a_item) { return a_item == Item::kFixture ? 0 : 1; }

std::string_view RoleName(Role a_role) {
  switch (a_role) {
  case Role::kPlayer:
    return "player";
  case Role::kWearer:
    return "wearer";
  case Role::kControl:
    return "control";
  }
  return "unknown";
}

std::string_view ItemName(Item a_item) {
  return a_item == Item::kFixture ? "fixture" : "plain-cuirass";
}

const ActorFacts &ActorOf(const StepContext &a_context, Role a_role) {
  return a_context.seen.actors[RoleIndex(a_role)];
}

const ItemFacts &ArmorOf(const StepContext &a_context, Role a_role,
                         Item a_item) {
  return ActorOf(a_context, a_role).armor[ItemIndex(a_item)];
}

bool Added(const RunChanges &a_owned, Role a_role, Item a_item) {
  return a_owned.added[RoleIndex(a_role)][ItemIndex(a_item)];
}

RunChanges WithAdded(RunChanges a_owned, Role a_role, Item a_item,
                     bool a_added) {
  a_owned.added[RoleIndex(a_role)][ItemIndex(a_item)] = a_added;
  return a_owned;
}

RunChanges WithSpawned(RunChanges a_owned, Role a_role, bool a_spawned) {
  a_owned.spawned[RoleIndex(a_role)] = a_spawned;
  if (!a_spawned) {
    a_owned.added[RoleIndex(a_role)] = {};
  }
  return a_owned;
}

RunChanges WithAway(RunChanges a_owned, bool a_away) {
  a_owned.away = a_away;
  return a_owned;
}

StepMove Pass(const StepContext &a_context, std::string a_reason = {}) {
  return {Completed{Outcome::kPass, std::move(a_reason)}, std::nullopt,
          a_context.changes};
}

StepMove Block(const StepContext &a_context, std::string a_reason) {
  return {Completed{Outcome::kBlocked, std::move(a_reason)}, std::nullopt,
          a_context.changes};
}

StepMove Fail(const StepContext &a_context, std::string a_reason,
              std::optional<Command> a_command = std::nullopt) {
  return {Completed{Outcome::kFail, std::move(a_reason)}, a_command,
          a_context.changes};
}

StepMove Wait(const StepContext &a_context) {
  return {Pending{}, std::nullopt, a_context.changes};
}

StepMove Issue(Command a_command, RunChanges a_owned) {
  return {Pending{}, a_command, a_owned};
}

StepMove PassWhen(bool a_done, const StepContext &a_context,
                  std::uint32_t a_deadline, std::string_view a_waitingFor) {
  if (a_done) {
    return Pass(a_context);
  }
  if (a_context.frames >= a_deadline) {
    return Fail(a_context,
                std::format("{} within {} frames", a_waitingFor, a_deadline));
  }
  return Wait(a_context);
}

std::string Describe(const ActorFacts &a_actor) {
  return std::format(
      "present {}, wears fixture {}, live {}, residue {}, "
      "latest application {}",
      a_actor.present ? "yes" : "no",
      a_actor.armor[ItemIndex(Item::kFixture)].equipped ? "yes" : "no",
      a_actor.live ? "yes" : "no", a_actor.residue,
      a_actor.application.empty() ? "none" : a_actor.application);
}

StepMove WithActorState(StepMove a_move, const ActorFacts &a_actor) {
  Completed *completed = std::get_if<Completed>(&a_move.verdict);
  if (completed && completed->outcome != Outcome::kPass) {
    completed->reason += std::format(" ({})", Describe(a_actor));
  }
  return a_move;
}

std::string Missing(Role a_role) {
  return std::format("the {} is not present", RoleName(a_role));
}

StepMove Start(const WaitFrames &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const WaitFrames &a_settle, const StepContext &a_context) {
  return a_context.frames >= a_settle.frames ? Pass(a_context)
                                             : Wait(a_context);
}

StepMove Start(const Solo &a_solo, const StepContext &a_context) {
  return {Completed{}, SoloRecipe{a_solo.recipe}, a_context.changes};
}

StepMove Check(const Solo &, const StepContext &a_context) {
  return Pass(a_context);
}

StepMove Start(const RestoreSolo &, const StepContext &a_context) {
  return {Completed{}, EndSolo{}, a_context.changes};
}

StepMove Check(const RestoreSolo &, const StepContext &a_context) {
  return Pass(a_context);
}

StepMove Start(const Spawn &a_spawn, const StepContext &a_context) {
  if (a_spawn.role == Role::kPlayer) {
    return Fail(a_context, "the player cannot be spawned");
  }
  if (!a_context.seen.npcEffects) {
    return Block(a_context,
                 "effects are limited to the player in the settings");
  }
  if (a_context.changes.spawned[RoleIndex(a_spawn.role)]) {
    return Pass(a_context, "already spawned");
  }
  return Issue(SpawnActor{a_spawn.role},
               WithSpawned(a_context.changes, a_spawn.role, true));
}

StepMove Check(const Spawn &a_spawn, const StepContext &a_context) {
  return PassWhen(ActorOf(a_context, a_spawn.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not appear");
}

StepMove Start(const Despawn &a_despawn, const StepContext &a_context) {
  if (!a_context.changes.spawned[RoleIndex(a_despawn.role)]) {
    return Pass(a_context, "the run spawned no such actor");
  }
  return Issue(DespawnActor{a_despawn.role},
               WithSpawned(a_context.changes, a_despawn.role, false));
}

StepMove Check(const Despawn &a_despawn, const StepContext &a_context) {
  return PassWhen(!ActorOf(a_context, a_despawn.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not disappear");
}

StepMove Start(const Disable &a_disable, const StepContext &a_context) {
  if (!a_context.changes.spawned[RoleIndex(a_disable.role)]) {
    return Block(a_context, "only an actor the run spawned can be disabled");
  }
  return Issue(DisableActor{a_disable.role}, a_context.changes);
}

StepMove Check(const Disable &a_disable, const StepContext &a_context) {
  return PassWhen(!ActorOf(a_context, a_disable.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not disappear");
}

StepMove Start(const Enable &a_enable, const StepContext &a_context) {
  if (!a_context.changes.spawned[RoleIndex(a_enable.role)]) {
    return Block(a_context, "only an actor the run spawned can be enabled");
  }
  return Issue(EnableActor{a_enable.role}, a_context.changes);
}

StepMove Check(const Enable &a_enable, const StepContext &a_context) {
  return PassWhen(ActorOf(a_context, a_enable.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not reappear");
}

StepMove Start(const Equip &a_equip, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_equip.role);
  const ItemFacts &armor = ArmorOf(a_context, a_equip.role, a_equip.item);
  if (!a_context.seen.itemsLoaded[ItemIndex(a_equip.item)]) {
    return Block(a_context,
                 std::format("the {} is not loaded", ItemName(a_equip.item)));
  }
  if (!actor.present) {
    return Block(a_context, Missing(a_equip.role));
  }
  if (armor.equipped) {
    return Pass(a_context, "already equipped");
  }
  if (armor.carried) {
    if (!Added(a_context.changes, a_equip.role, a_equip.item)) {
      return Block(a_context, "the actor carries a copy the run did not add");
    }
    return Issue(EquipCarried{a_equip.role, a_equip.item}, a_context.changes);
  }
  if (a_equip.role == Role::kPlayer && actor.bodyArmorWorn) {
    return Block(a_context, "the player wears body armor");
  }
  return Issue(AddAndEquip{a_equip.role, a_equip.item},
               WithAdded(a_context.changes, a_equip.role, a_equip.item, true));
}

StepMove Check(const Equip &a_equip, const StepContext &a_context) {
  return PassWhen(ArmorOf(a_context, a_equip.role, a_equip.item).equipped,
                  a_context, kActionDeadlineFrames, "the armor did not equip");
}

StepMove Start(const Unequip &a_unequip, const StepContext &a_context) {
  if (!ArmorOf(a_context, a_unequip.role, a_unequip.item).equipped) {
    return Pass(a_context, "not equipped");
  }
  return Issue(UnequipArmor{a_unequip.role, a_unequip.item}, a_context.changes);
}

StepMove Check(const Unequip &a_unequip, const StepContext &a_context) {
  return PassWhen(!ArmorOf(a_context, a_unequip.role, a_unequip.item).equipped,
                  a_context, kActionDeadlineFrames,
                  "the armor did not unequip");
}

StepMove Start(const Remove &a_remove, const StepContext &a_context) {
  if (!Added(a_context.changes, a_remove.role, a_remove.item)) {
    return Pass(a_context, "the run added none");
  }
  return Issue(
      RemoveArmor{a_remove.role, a_remove.item},
      WithAdded(a_context.changes, a_remove.role, a_remove.item, false));
}

StepMove Check(const Remove &a_remove, const StepContext &a_context) {
  return PassWhen(!ArmorOf(a_context, a_remove.role, a_remove.item).carried,
                  a_context, kActionDeadlineFrames,
                  "the armor did not leave the inventory");
}

StepMove CheckOutcome(Outcome a_outcome, const StepContext &a_context) {
  switch (a_outcome) {
  case Outcome::kPass:
    return Pass(a_context);
  case Outcome::kFail:
    return Fail(a_context, "the plugin reported a failure");
  case Outcome::kBlocked:
    return Block(a_context, "nothing rendered on the demo cuirass, or the "
                            "actor was not ready");
  case Outcome::kAborted:
    return {Completed{Outcome::kAborted, "the plugin cancelled the request"},
            std::nullopt, a_context.changes};
  }
  return Fail(a_context, "unknown request outcome");
}

StepMove CheckRequest(const StepContext &a_context) {
  return std::visit(
      Overloaded{
          [&](const NoRequest &) {
            return Block(a_context, "the plugin refused the request");
          },
          [&](const RequestPending &) {
            if (a_context.frames >= kRenderDeadlineFrames) {
              return Fail(a_context,
                          std::format("no result within {} frames",
                                      kRenderDeadlineFrames),
                          AbortRequest{});
            }
            return Wait(a_context);
          },
          [&](Outcome a_outcome) { return CheckOutcome(a_outcome, a_context); },
      },
      a_context.seen.request);
}

StepMove Start(const Apply &a_apply, const StepContext &a_context) {
  return Issue(SubmitApply{a_apply.role}, a_context.changes);
}

StepMove Check(const Apply &, const StepContext &a_context) {
  return CheckRequest(a_context);
}

StepMove Start(const Retire &a_retire, const StepContext &a_context) {
  return Issue(SubmitRetire{a_retire.role}, a_context.changes);
}

StepMove Check(const Retire &, const StepContext &a_context) {
  return CheckRequest(a_context);
}

StepMove Start(const AwaitRendered &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const AwaitRendered &a_await, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_await.role);
  const bool rendered =
      actor.present &&
      actor.renderedAttempt > a_context.renderedBefore[RoleIndex(a_await.role)];
  if (rendered && actor.residue > 0) {
    return Pass(a_context);
  }
  if (a_context.frames < kRenderDeadlineFrames) {
    return Wait(a_context);
  }
  return Fail(a_context,
              std::format("{} ({})",
                          rendered ? "a new application rendered, but no "
                                     "plugin texture or shell was found on "
                                     "the actor"
                                   : std::format("no new application rendered "
                                                 "within {} frames",
                                                 kRenderDeadlineFrames),
                          Describe(actor)));
}

StepMove Start(const AwaitRetired &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const AwaitRetired &a_await, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_await.role);
  return WithActorState(PassWhen(!actor.live, a_context, kRenderDeadlineFrames,
                                 "the plugin did not retire the actor"),
                        actor);
}

StepMove Start(const AwaitBaseline &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const AwaitBaseline &a_await, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_await.role);
  return WithActorState(
      PassWhen(actor.present && actor.residue == 0, a_context,
               kRenderDeadlineFrames,
               "plugin textures or shells stayed on the actor"),
      actor);
}

StepMove Start(const ExpectEffect &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const ExpectEffect &a_expect, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_expect.role);
  return WithActorState(
      PassWhen(actor.present && actor.live && actor.residue > 0, a_context,
               kExpectDeadlineFrames, "the effect was not on the actor"),
      actor);
}

StepMove Start(const HoldUntouched &a_hold, const StepContext &a_context) {
  if (!ActorOf(a_context, a_hold.role).present) {
    return Block(a_context, Missing(a_hold.role));
  }
  return Wait(a_context);
}

StepMove Check(const HoldUntouched &a_hold, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_hold.role);
  if (actor.live || actor.residue > 0) {
    return Fail(a_context, std::format("the {} gained plugin state",
                                       RoleName(a_hold.role)));
  }
  return a_context.frames >= a_hold.frames ? Pass(a_context) : Wait(a_context);
}

StepMove Start(const LeaveStart &, const StepContext &a_context) {
  if (a_context.changes.away) {
    return Pass(a_context, "already away");
  }
  return Issue(TravelFromStart{}, WithAway(a_context.changes, true));
}

StepMove Check(const LeaveStart &, const StepContext &a_context) {
  return PassWhen(a_context.seen.awayFromStart &&
                      ActorOf(a_context, Role::kPlayer).present,
                  a_context, kTravelDeadlineFrames,
                  "the player did not leave the start cell");
}

StepMove Start(const ReturnToStart &, const StepContext &a_context) {
  if (!a_context.changes.away) {
    return Pass(a_context, "never left");
  }
  return Issue(TravelToStart{}, WithAway(a_context.changes, false));
}

StepMove Check(const ReturnToStart &, const StepContext &a_context) {
  return PassWhen(!a_context.seen.awayFromStart &&
                      ActorOf(a_context, Role::kPlayer).present,
                  a_context, kTravelDeadlineFrames,
                  "the player did not return to the start");
}

bool InView(Camera a_view, const StepContext &a_context) {
  return a_context.seen.firstPerson == (a_view == Camera::kFirstPerson);
}

StepMove Start(const SetCamera &a_set, const StepContext &a_context) {
  if (InView(a_set.view, a_context)) {
    return Pass(a_context, "already in that view");
  }
  return Issue(SwitchCamera{a_set.view}, a_context.changes);
}

StepMove Check(const SetCamera &a_set, const StepContext &a_context) {
  return PassWhen(InView(a_set.view, a_context), a_context,
                  kActionDeadlineFrames, "the camera did not switch");
}

std::string_view WorkName(QueuedWork a_work) {
  switch (a_work) {
  case QueuedWork::kNothing:
    return "nothing";
  case QueuedWork::kApply:
    return "apply";
  case QueuedWork::kEdit:
    return "edit";
  }
  return "unknown";
}

std::string_view WorkName(Session a_session) {
  return a_session == Session::kGesture ? "gesture" : "paint";
}

std::string_view WorkName(TrackedWork a_work) {
  return a_work == TrackedWork::kGesture ? "gesture" : "edit";
}

std::string RequestName(const RequestState &a_request) {
  return std::visit(
      Overloaded{
          [](const NoRequest &) { return std::string{"none"}; },
          [](const RequestPending &) { return std::string{"pending"}; },
          [](Outcome a_outcome) { return std::string{OutcomeName(a_outcome)}; },
      },
      a_request);
}

bool Idle(const Activity &a_activity) {
  return a_activity.pendingApplications == 0 && !a_activity.paintActive &&
         !a_activity.gestureActive && !a_activity.fileOperationPending;
}

std::string Describe(const Activity &a_activity) {
  return std::format("applications {}, paint {}, gesture {}, "
                     "file operations {}",
                     a_activity.pendingApplications,
                     a_activity.paintActive ? "yes" : "no",
                     a_activity.gestureActive ? "yes" : "no",
                     a_activity.fileOperationPending ? "yes" : "no");
}

StepMove Start(const LoadDuring &a_load, const StepContext &a_context) {
  RunChanges changes = a_context.changes;
  changes.loadTarget = a_context.seen.loads + 1;
  return Issue(LoadSaveWith{a_load.work, a_load.recipe}, changes);
}

StepMove Check(const LoadDuring &, const StepContext &a_context) {
  const bool loaded = a_context.seen.loads >= a_context.changes.loadTarget &&
                      ActorOf(a_context, Role::kPlayer).present;
  if (loaded) {
    return {Completed{}, std::nullopt, RunChanges{}};
  }
  if (a_context.frames >= kTravelDeadlineFrames) {
    return Fail(a_context, std::format("the save did not load within {} frames",
                                       kTravelDeadlineFrames));
  }
  return Wait(a_context);
}

StepMove Start(const ExpectAborted &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const ExpectAborted &, const StepContext &a_context) {
  if (a_context.seen.request == RequestState{Outcome::kAborted}) {
    return Pass(a_context);
  }
  return Fail(a_context, std::format("the request from before the load "
                                     "reported {}",
                                     RequestName(a_context.seen.request)));
}

WorkOutcome TrackedOutcomeOf(TrackedWork a_work, const Activity &a_activity) {
  return a_work == TrackedWork::kGesture ? a_activity.gestureOutcome
                                         : a_activity.editOutcome;
}

std::string_view PhraseOf(WorkOutcome a_outcome) {
  switch (a_outcome) {
  case WorkOutcome::kNone:
    return "never recorded";
  case WorkOutcome::kPending:
    return "still pending";
  case WorkOutcome::kApplied:
    return "applied before the load";
  case WorkOutcome::kCancelledByLoad:
    return "cancelled by the load";
  case WorkOutcome::kFailed:
    return "ended for another reason";
  }
  return "unknown";
}

StepMove Start(const ExpectCancelled &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const ExpectCancelled &a_expect, const StepContext &a_context) {
  const WorkOutcome outcome =
      TrackedOutcomeOf(a_expect.work, a_context.seen.activity);
  if (outcome == WorkOutcome::kCancelledByLoad) {
    return Pass(a_context);
  }
  const std::string &detail = a_context.seen.activity.detail;
  return Fail(a_context,
              std::format("the {} was {}{}", WorkName(a_expect.work),
                          PhraseOf(outcome),
                          detail.empty() ? "" : std::format(" ({})", detail)));
}

StepMove Start(const ExpectSettled &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const ExpectSettled &a_expect, const StepContext &a_context) {
  const WorkOutcome outcome =
      TrackedOutcomeOf(a_expect.work, a_context.seen.activity);
  if (outcome == WorkOutcome::kApplied ||
      outcome == WorkOutcome::kCancelledByLoad) {
    return Pass(a_context, std::string{PhraseOf(outcome)});
  }
  if (a_context.frames < kActionDeadlineFrames &&
      outcome == WorkOutcome::kPending) {
    return Wait(a_context);
  }
  const std::string &detail = a_context.seen.activity.detail;
  return Fail(a_context,
              std::format("the {} was {}{}", WorkName(a_expect.work),
                          PhraseOf(outcome),
                          detail.empty() ? "" : std::format(" ({})", detail)));
}

std::string Describe(const RecipeFacts &a_scratch) {
  return std::format(
      "loaded {}, dirty {}, first opacity {}", a_scratch.loaded ? "yes" : "no",
      a_scratch.dirty ? "yes" : "no",
      a_scratch.firstOpacity ? std::format("{}", *a_scratch.firstOpacity)
                             : std::string{"not a number"});
}

StepMove FinishWhen(bool a_done, WorkOutcome a_outcome,
                    const StepContext &a_context, std::string_view a_what) {
  if (a_done) {
    return Pass(a_context);
  }
  const bool ended = a_outcome == WorkOutcome::kFailed ||
                     a_outcome == WorkOutcome::kCancelledByLoad;
  if (!ended && a_context.frames < kRenderDeadlineFrames) {
    return Wait(a_context);
  }
  const std::string &detail = a_context.seen.activity.detail;
  return Fail(a_context,
              std::format("{} did not finish: {}; scratch {}{}", a_what,
                          PhraseOf(a_outcome), Describe(a_context.seen.scratch),
                          detail.empty() ? "" : std::format(" ({})", detail)));
}

StepMove Start(const DeleteScratch &, const StepContext &a_context) {
  if (!a_context.seen.scratch.loaded) {
    return Pass(a_context, "no scratch recipe loaded");
  }
  return Issue(StartDelete{kScratchRecipe}, a_context.changes);
}

StepMove Check(const DeleteScratch &, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.editOutcome == WorkOutcome::kApplied &&
                        !a_context.seen.scratch.loaded,
                    activity.editOutcome, a_context, "the delete");
}

StepMove Start(const DuplicateToScratch &a_copy, const StepContext &a_context) {
  return Issue(StartDuplicate{a_copy.from, kScratchRecipe}, a_context.changes);
}

StepMove Check(const DuplicateToScratch &, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.editOutcome == WorkOutcome::kApplied &&
                        a_context.seen.scratch.loaded,
                    activity.editOutcome, a_context, "the duplicate");
}

StepMove Start(const SetScratchOpacity &a_set, const StepContext &a_context) {
  return Issue(StartOpacityEdit{kScratchRecipe, a_set.value},
               a_context.changes);
}

StepMove Check(const SetScratchOpacity &a_set, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.editOutcome == WorkOutcome::kApplied &&
                        a_context.seen.scratch.firstOpacity == a_set.value,
                    activity.editOutcome, a_context, "the opacity edit");
}

StepMove Start(const SaveScratch &, const StepContext &a_context) {
  return Issue(StartSave{kScratchRecipe}, a_context.changes);
}

StepMove Check(const SaveScratch &, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.fileOutcome == WorkOutcome::kApplied &&
                        !a_context.seen.scratch.dirty,
                    activity.fileOutcome, a_context, "the save");
}

StepMove Start(const ExpectScratch &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const ExpectScratch &a_expect, const StepContext &a_context) {
  const RecipeFacts &scratch = a_context.seen.scratch;
  if (scratch.loaded && !scratch.dirty &&
      scratch.firstOpacity == a_expect.opacity) {
    return Pass(a_context);
  }
  if (a_context.frames < kExpectDeadlineFrames) {
    return Wait(a_context);
  }
  return Fail(a_context,
              std::format("the scratch recipe is not the saved one with "
                          "opacity {}: {}",
                          a_expect.opacity, Describe(scratch)));
}

StepMove Start(const SpawnCrowd &a_spawn, const StepContext &a_context) {
  if (a_spawn.count == 0 || a_spawn.count > kMaxCrowd) {
    return Fail(a_context,
                std::format("a crowd has 1 to {} actors", kMaxCrowd));
  }
  if (!a_context.seen.npcEffects) {
    return Block(a_context,
                 "effects are limited to the player in the settings");
  }
  if (a_context.changes.crowd > 0) {
    return Pass(a_context, "already spawned");
  }
  RunChanges changes = a_context.changes;
  changes.crowd = a_spawn.count;
  return Issue(SpawnCrowdActors{a_spawn.count}, changes);
}

StepMove Check(const SpawnCrowd &, const StepContext &a_context) {
  const CrowdFacts &crowd = a_context.seen.crowd;
  return PassWhen(crowd.present >= a_context.changes.crowd, a_context,
                  kActionDeadlineFrames,
                  std::format("only {} of {} crowd actors appeared",
                              crowd.present, a_context.changes.crowd));
}

StepMove Start(const AwaitCrowdRendered &, const StepContext &a_context) {
  if (a_context.changes.crowd == 0) {
    return Block(a_context, "no crowd was spawned");
  }
  return Wait(a_context);
}

StepMove Check(const AwaitCrowdRendered &, const StepContext &a_context) {
  const CrowdFacts &crowd = a_context.seen.crowd;
  return PassWhen(crowd.rendered >= a_context.changes.crowd, a_context,
                  kCrowdDeadlineFrames,
                  std::format("only {} of {} crowd actors rendered",
                              crowd.rendered, a_context.changes.crowd));
}

StepMove Start(const DespawnCrowd &, const StepContext &a_context) {
  if (a_context.changes.crowd == 0) {
    return Pass(a_context, "no crowd");
  }
  RunChanges changes = a_context.changes;
  changes.crowd = 0;
  return Issue(DespawnCrowdActors{}, changes);
}

StepMove Check(const DespawnCrowd &, const StepContext &a_context) {
  return PassWhen(a_context.seen.crowd.present == 0, a_context,
                  kActionDeadlineFrames, "the crowd did not disappear");
}

StepMove Start(const HoldWindow &a_hold, const StepContext &a_context) {
  RunChanges changes = a_context.changes;
  changes.holdUntilMs =
      a_context.seen.nowMs + std::uint64_t{a_hold.seconds} * 1000;
  return {Pending{}, std::nullopt, changes};
}

StepMove Check(const HoldWindow &a_hold, const StepContext &a_context) {
  if (a_context.seen.nowMs >= a_context.changes.holdUntilMs) {
    return Pass(a_context);
  }
  const std::uint64_t stalled =
      (std::uint64_t{a_hold.seconds} + 1) * kMaxFramesPerSecond;
  if (a_context.frames >= stalled) {
    return Fail(a_context, "the game clock did not advance");
  }
  return Wait(a_context);
}

StepMove Start(const BeginSession &a_begin, const StepContext &a_context) {
  return Issue(OpenSession{a_begin.session, a_begin.recipe}, a_context.changes);
}

StepMove Check(const BeginSession &, const StepContext &a_context) {
  return Pass(a_context);
}

StepMove Start(const BeginWindow &, const StepContext &a_context) {
  return Pass(a_context);
}

StepMove Check(const BeginWindow &, const StepContext &a_context) {
  return Pass(a_context);
}

StepMove Start(const EndWindow &, const StepContext &a_context) {
  return Pass(a_context);
}

StepMove Check(const EndWindow &, const StepContext &a_context) {
  return Pass(a_context);
}

bool SessionActive(Session a_session, const Activity &a_activity) {
  return a_session == Session::kPaint
             ? a_activity.paintActive
             : a_activity.gestureActive &&
                   a_activity.gestureOutcome == WorkOutcome::kPending;
}

StepMove Start(const AwaitSessionActive &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const AwaitSessionActive &a_await,
               const StepContext &a_context) {
  if (SessionActive(a_await.session, a_context.seen.activity)) {
    return Pass(a_context);
  }
  if (a_context.frames >= kActionDeadlineFrames) {
    return Fail(a_context,
                std::format("the {} never became active within {} frames ({})",
                            WorkName(a_await.session), kActionDeadlineFrames,
                            a_context.seen.activity.detail));
  }
  return Wait(a_context);
}

StepMove Start(const AwaitIdle &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const AwaitIdle &, const StepContext &a_context) {
  if (Idle(a_context.seen.activity)) {
    return Pass(a_context);
  }
  if (a_context.frames >= kRenderDeadlineFrames) {
    return Fail(a_context, std::format("work stayed pending: {}",
                                       Describe(a_context.seen.activity)));
  }
  return Wait(a_context);
}

std::string WithRole(std::string_view a_action, Role a_role) {
  return std::format("{} {}", a_action, RoleName(a_role));
}

std::string WithArmor(std::string_view a_action, Role a_role, Item a_item) {
  return std::format("{} {} {}", a_action, RoleName(a_role), ItemName(a_item));
}
std::string Label(const WaitFrames &a_settle) {
  return std::format("settle {}", a_settle.frames);
}

std::string Label(const Solo &a_solo) {
  return std::format("solo {}", a_solo.recipe);
}

std::string Label(const RestoreSolo &) { return std::string{"restore-view"}; }

std::string Label(const Spawn &a_s) { return WithRole("spawn", a_s.role); }

std::string Label(const Despawn &a_s) { return WithRole("despawn", a_s.role); }

std::string Label(const Disable &a_s) { return WithRole("disable", a_s.role); }

std::string Label(const Enable &a_s) { return WithRole("enable", a_s.role); }

std::string Label(const Equip &a_s) {
  return WithArmor("equip", a_s.role, a_s.item);
}

std::string Label(const Unequip &a_s) {
  return WithArmor("unequip", a_s.role, a_s.item);
}

std::string Label(const Remove &a_s) {
  return WithArmor("remove", a_s.role, a_s.item);
}

std::string Label(const Apply &a_s) { return WithRole("apply", a_s.role); }

std::string Label(const Retire &a_s) { return WithRole("retire", a_s.role); }

std::string Label(const AwaitRendered &a_s) {
  return WithRole("await-rendered", a_s.role);
}

std::string Label(const AwaitRetired &a_s) {
  return WithRole("await-retired", a_s.role);
}

std::string Label(const AwaitBaseline &a_s) {
  return WithRole("await-baseline", a_s.role);
}

std::string Label(const ExpectEffect &a_s) {
  return WithRole("expect-effect", a_s.role);
}

std::string Label(const HoldUntouched &a_s) {
  return std::format("hold-untouched {} {}", RoleName(a_s.role), a_s.frames);
}

std::string Label(const LeaveStart &) { return std::string{"leave-cell"}; }

std::string Label(const ReturnToStart &) {
  return std::string{"return-to-start"};
}

std::string Label(const SetCamera &a_s) {
  return std::string{a_s.view == Camera::kFirstPerson
                         ? "set-view first-person"
                         : "set-view third-person"};
}

std::string Label(const LoadDuring &a_s) {
  return std::format("load-during {}", WorkName(a_s.work));
}

std::string Label(const ExpectAborted &) {
  return std::string{"expect-aborted"};
}

std::string Label(const ExpectCancelled &a_s) {
  return std::format("expect-cancelled {}", WorkName(a_s.work));
}

std::string Label(const ExpectSettled &a_s) {
  return std::format("expect-settled {}", WorkName(a_s.work));
}

std::string Label(const DeleteScratch &) {
  return std::string{"delete-scratch"};
}

std::string Label(const DuplicateToScratch &a_s) {
  return std::format("duplicate-to-scratch {}", a_s.from);
}

std::string Label(const SetScratchOpacity &a_s) {
  return std::format("set-scratch-opacity {}", a_s.value);
}

std::string Label(const SaveScratch &) { return std::string{"save-scratch"}; }

std::string Label(const ExpectScratch &a_s) {
  return std::format("expect-scratch opacity {}", a_s.opacity);
}

std::string Label(const SpawnCrowd &a_s) {
  return std::format("spawn-crowd {}", a_s.count);
}

std::string Label(const AwaitCrowdRendered &) {
  return std::string{"await-crowd-rendered"};
}

std::string Label(const DespawnCrowd &) { return std::string{"despawn-crowd"}; }

std::string Label(const HoldWindow &a_s) {
  return std::format("hold {} {}s", a_s.window, a_s.seconds);
}

std::string Label(const BeginSession &a_s) {
  return std::format("begin-session {}", WorkName(a_s.session));
}

std::string Label(const BeginWindow &a_s) {
  return std::format("begin-window {}", a_s.window);
}

std::string Label(const EndWindow &a_s) {
  return std::format("end-window {}", a_s.window);
}

std::string Label(const AwaitSessionActive &a_s) {
  return std::format("await-session-active {}", WorkName(a_s.session));
}

std::string Label(const AwaitIdle &) { return std::string{"await-idle"}; }
}

std::size_t RoleIndex(Role a_role) {
  switch (a_role) {
  case Role::kPlayer:
    return 0;
  case Role::kWearer:
    return 1;
  case Role::kControl:
    return 2;
  }
  return 0;
}

StepMove StartStep(const Step &a_step, const StepContext &a_context) {
  return std::visit(
      [&](const auto &a_kind) { return Start(a_kind, a_context); }, a_step);
}

StepMove CheckStep(const Step &a_step, const StepContext &a_context) {
  return std::visit(
      [&](const auto &a_kind) { return Check(a_kind, a_context); }, a_step);
}

std::array<bool, kRoleCount> RolesAffected(const Step &a_step) {
  const auto only = [](Role a_role) {
    std::array<bool, kRoleCount> moved{};
    moved[RoleIndex(a_role)] = true;
    return moved;
  };
  constexpr std::array<bool, kRoleCount> kNone{};
  constexpr std::array<bool, kRoleCount> kAll{true, true, true};
  return std::visit(
      Overloaded{
          [&](const WaitFrames &) { return kNone; },
          [&](const AwaitRendered &) { return kNone; },
          [&](const AwaitRetired &) { return kNone; },
          [&](const AwaitBaseline &) { return kNone; },
          [&](const ExpectEffect &) { return kNone; },
          [&](const HoldUntouched &) { return kNone; },
          [&](const Solo &) { return kAll; },
          [&](const RestoreSolo &) { return kAll; },
          [&](const LeaveStart &) { return kAll; },
          [&](const ReturnToStart &) { return kAll; },
          [&](const SetCamera &) { return only(Role::kPlayer); },
          [&](const LoadDuring &) { return kAll; },
          [&](const ExpectAborted &) { return kNone; },
          [&](const ExpectCancelled &) { return kNone; },
          [&](const ExpectSettled &) { return kNone; },
          [&](const DeleteScratch &) { return kAll; },
          [&](const DuplicateToScratch &) { return kAll; },
          [&](const SetScratchOpacity &) { return kAll; },
          [&](const SaveScratch &) { return kAll; },
          [&](const ExpectScratch &) { return kNone; },
          [&](const SpawnCrowd &) { return kNone; },
          [&](const AwaitCrowdRendered &) { return kNone; },
          [&](const DespawnCrowd &) { return kNone; },
          [&](const HoldWindow &) { return kNone; },
          [&](const BeginSession &) { return only(Role::kPlayer); },
          [&](const AwaitSessionActive &) { return kNone; },
          [&](const BeginWindow &) { return kNone; },
          [&](const EndWindow &) { return kNone; },
          [&](const AwaitIdle &) { return kNone; },
          [&](const auto &a_targeted) { return only(a_targeted.role); },
      },
      a_step);
}

std::string StepLabel(const Step &a_step) {
  return std::visit([](const auto &a_kind) { return Label(a_kind); }, a_step);
}
}
