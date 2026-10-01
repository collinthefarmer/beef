// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/Steps.h"

#include "Core.h"

#include <format>
#include <utility>

namespace BetterEnchantmentEffects::Regression {
namespace {
inline constexpr std::uint32_t kActionDeadlineFrames = 600;
inline constexpr std::uint32_t kRenderDeadlineFrames = 1800;
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

const ActorView &ActorOf(const StepContext &a_context, Role a_role) {
  return a_context.seen.actors[RoleIndex(a_role)];
}

const ArmorView &ArmorOf(const StepContext &a_context, Role a_role,
                         Item a_item) {
  return ActorOf(a_context, a_role).armor[ItemIndex(a_item)];
}

bool Added(const Ownership &a_owned, Role a_role, Item a_item) {
  return a_owned.added[RoleIndex(a_role)][ItemIndex(a_item)];
}

Ownership WithAdded(Ownership a_owned, Role a_role, Item a_item, bool a_added) {
  a_owned.added[RoleIndex(a_role)][ItemIndex(a_item)] = a_added;
  return a_owned;
}

Ownership WithSpawned(Ownership a_owned, Role a_role, bool a_spawned) {
  a_owned.spawned[RoleIndex(a_role)] = a_spawned;
  if (!a_spawned) {
    a_owned.added[RoleIndex(a_role)] = {};
  }
  return a_owned;
}

Ownership WithAway(Ownership a_owned, bool a_away) {
  a_owned.away = a_away;
  return a_owned;
}

StepMove Pass(const StepContext &a_context, std::string a_reason = {}) {
  return {Completed{Outcome::kPass, std::move(a_reason)}, std::nullopt,
          a_context.owned};
}

StepMove Block(const StepContext &a_context, std::string a_reason) {
  return {Completed{Outcome::kBlocked, std::move(a_reason)}, std::nullopt,
          a_context.owned};
}

StepMove Fail(const StepContext &a_context, std::string a_reason,
              std::optional<Command> a_command = std::nullopt) {
  return {Completed{Outcome::kFail, std::move(a_reason)}, a_command,
          a_context.owned};
}

StepMove Wait(const StepContext &a_context) {
  return {Pending{}, std::nullopt, a_context.owned};
}

StepMove Issue(Command a_command, Ownership a_owned) {
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

std::string StateOf(const ActorView &a_actor) {
  return std::format(
      "present {}, wears fixture {}, live {}, traces {}, "
      "latest application {}",
      a_actor.present ? "yes" : "no",
      a_actor.armor[ItemIndex(Item::kFixture)].equipped ? "yes" : "no",
      a_actor.live ? "yes" : "no", a_actor.traces,
      a_actor.application.empty() ? "none" : a_actor.application);
}

StepMove WithActorState(StepMove a_move, const ActorView &a_actor) {
  Completed *completed = std::get_if<Completed>(&a_move.verdict);
  if (completed && completed->outcome != Outcome::kPass) {
    completed->reason += std::format(" ({})", StateOf(a_actor));
  }
  return a_move;
}

std::string Missing(Role a_role) {
  return std::format("the {} is not present", RoleName(a_role));
}

StepMove Start(const Settle &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const Settle &a_settle, const StepContext &a_context) {
  return a_context.frames >= a_settle.frames ? Pass(a_context)
                                             : Wait(a_context);
}

StepMove Start(const Solo &a_solo, const StepContext &a_context) {
  return {Completed{}, SoloRecipe{a_solo.recipe}, a_context.owned};
}

StepMove Check(const Solo &, const StepContext &a_context) {
  return Pass(a_context);
}

StepMove Start(const RestoreView &, const StepContext &a_context) {
  return {Completed{}, RestoreSolo{}, a_context.owned};
}

StepMove Check(const RestoreView &, const StepContext &a_context) {
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
  if (a_context.owned.spawned[RoleIndex(a_spawn.role)]) {
    return Pass(a_context, "already spawned");
  }
  return Issue(SpawnActor{a_spawn.role},
               WithSpawned(a_context.owned, a_spawn.role, true));
}

StepMove Check(const Spawn &a_spawn, const StepContext &a_context) {
  return PassWhen(ActorOf(a_context, a_spawn.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not appear");
}

StepMove Start(const Despawn &a_despawn, const StepContext &a_context) {
  if (!a_context.owned.spawned[RoleIndex(a_despawn.role)]) {
    return Pass(a_context, "the run spawned no such actor");
  }
  return Issue(DespawnActor{a_despawn.role},
               WithSpawned(a_context.owned, a_despawn.role, false));
}

StepMove Check(const Despawn &a_despawn, const StepContext &a_context) {
  return PassWhen(!ActorOf(a_context, a_despawn.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not disappear");
}

StepMove Start(const Disable &a_disable, const StepContext &a_context) {
  if (!a_context.owned.spawned[RoleIndex(a_disable.role)]) {
    return Block(a_context, "only an actor the run spawned can be disabled");
  }
  return Issue(DisableActor{a_disable.role}, a_context.owned);
}

StepMove Check(const Disable &a_disable, const StepContext &a_context) {
  return PassWhen(!ActorOf(a_context, a_disable.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not disappear");
}

StepMove Start(const Enable &a_enable, const StepContext &a_context) {
  if (!a_context.owned.spawned[RoleIndex(a_enable.role)]) {
    return Block(a_context, "only an actor the run spawned can be enabled");
  }
  return Issue(EnableActor{a_enable.role}, a_context.owned);
}

StepMove Check(const Enable &a_enable, const StepContext &a_context) {
  return PassWhen(ActorOf(a_context, a_enable.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not reappear");
}

StepMove Start(const Equip &a_equip, const StepContext &a_context) {
  const ActorView &actor = ActorOf(a_context, a_equip.role);
  const ArmorView &armor = ArmorOf(a_context, a_equip.role, a_equip.item);
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
    if (!Added(a_context.owned, a_equip.role, a_equip.item)) {
      return Block(a_context, "the actor carries a copy the run did not add");
    }
    return Issue(EquipCarried{a_equip.role, a_equip.item}, a_context.owned);
  }
  if (a_equip.role == Role::kPlayer && actor.bodyArmorWorn) {
    return Block(a_context, "the player wears body armor");
  }
  return Issue(AddAndEquip{a_equip.role, a_equip.item},
               WithAdded(a_context.owned, a_equip.role, a_equip.item, true));
}

StepMove Check(const Equip &a_equip, const StepContext &a_context) {
  return PassWhen(ArmorOf(a_context, a_equip.role, a_equip.item).equipped,
                  a_context, kActionDeadlineFrames, "the armor did not equip");
}

StepMove Start(const Unequip &a_unequip, const StepContext &a_context) {
  if (!ArmorOf(a_context, a_unequip.role, a_unequip.item).equipped) {
    return Pass(a_context, "not equipped");
  }
  return Issue(UnequipArmor{a_unequip.role, a_unequip.item}, a_context.owned);
}

StepMove Check(const Unequip &a_unequip, const StepContext &a_context) {
  return PassWhen(!ArmorOf(a_context, a_unequip.role, a_unequip.item).equipped,
                  a_context, kActionDeadlineFrames,
                  "the armor did not unequip");
}

StepMove Start(const Remove &a_remove, const StepContext &a_context) {
  if (!Added(a_context.owned, a_remove.role, a_remove.item)) {
    return Pass(a_context, "the run added none");
  }
  return Issue(RemoveArmor{a_remove.role, a_remove.item},
               WithAdded(a_context.owned, a_remove.role, a_remove.item, false));
}

StepMove Check(const Remove &a_remove, const StepContext &a_context) {
  return PassWhen(!ArmorOf(a_context, a_remove.role, a_remove.item).carried,
                  a_context, kActionDeadlineFrames,
                  "the armor did not leave the inventory");
}

StepMove CheckRequest(const StepContext &a_context) {
  switch (a_context.seen.request) {
  case RequestState::kNone:
    return Block(a_context, "the plugin refused the request");
  case RequestState::kWaiting:
    if (a_context.frames >= kRenderDeadlineFrames) {
      return Fail(
          a_context,
          std::format("no result within {} frames", kRenderDeadlineFrames),
          AbortRequest{});
    }
    return Wait(a_context);
  case RequestState::kPass:
    return Pass(a_context);
  case RequestState::kFail:
    return Fail(a_context, "the plugin reported a failure");
  case RequestState::kBlocked:
    return Block(a_context, "nothing rendered on the demo cuirass, or the "
                            "actor was not ready");
  case RequestState::kAborted:
    return {Completed{Outcome::kAborted, "the plugin cancelled the request"},
            std::nullopt, a_context.owned};
  }
  return Fail(a_context, "unknown request state");
}

StepMove Start(const Apply &a_apply, const StepContext &a_context) {
  return Issue(SubmitApply{a_apply.role}, a_context.owned);
}

StepMove Check(const Apply &, const StepContext &a_context) {
  return CheckRequest(a_context);
}

StepMove Start(const Retire &a_retire, const StepContext &a_context) {
  return Issue(SubmitRetire{a_retire.role}, a_context.owned);
}

StepMove Check(const Retire &, const StepContext &a_context) {
  return CheckRequest(a_context);
}

StepMove Start(const AwaitRendered &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const AwaitRendered &a_await, const StepContext &a_context) {
  const ActorView &actor = ActorOf(a_context, a_await.role);
  const bool rendered =
      actor.present &&
      actor.renderedAttempt > a_context.renderMarks[RoleIndex(a_await.role)];
  if (rendered && actor.traces > 0) {
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
                          StateOf(actor)));
}

StepMove Start(const AwaitRetired &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const AwaitRetired &a_await, const StepContext &a_context) {
  const ActorView &actor = ActorOf(a_context, a_await.role);
  return WithActorState(PassWhen(!actor.live, a_context, kRenderDeadlineFrames,
                                 "the plugin did not retire the actor"),
                        actor);
}

StepMove Start(const AwaitBaseline &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const AwaitBaseline &a_await, const StepContext &a_context) {
  const ActorView &actor = ActorOf(a_context, a_await.role);
  return WithActorState(
      PassWhen(actor.present && actor.traces == 0, a_context,
               kRenderDeadlineFrames,
               "plugin textures or shells stayed on the actor"),
      actor);
}

StepMove Start(const ExpectEffect &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const ExpectEffect &a_expect, const StepContext &a_context) {
  const ActorView &actor = ActorOf(a_context, a_expect.role);
  return WithActorState(
      PassWhen(actor.present && actor.live && actor.traces > 0, a_context,
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
  const ActorView &actor = ActorOf(a_context, a_hold.role);
  if (actor.live || actor.traces > 0) {
    return Fail(a_context, std::format("the {} gained plugin state",
                                       RoleName(a_hold.role)));
  }
  return a_context.frames >= a_hold.frames ? Pass(a_context) : Wait(a_context);
}

StepMove Start(const LeaveCell &, const StepContext &a_context) {
  if (a_context.owned.away) {
    return Pass(a_context, "already away");
  }
  return Issue(TravelAway{}, WithAway(a_context.owned, true));
}

StepMove Check(const LeaveCell &, const StepContext &a_context) {
  return PassWhen(a_context.seen.awayFromStart &&
                      ActorOf(a_context, Role::kPlayer).present,
                  a_context, kTravelDeadlineFrames,
                  "the player did not leave the start cell");
}

StepMove Start(const ReturnToStart &, const StepContext &a_context) {
  if (!a_context.owned.away) {
    return Pass(a_context, "never left");
  }
  return Issue(TravelBack{}, WithAway(a_context.owned, false));
}

StepMove Check(const ReturnToStart &, const StepContext &a_context) {
  return PassWhen(!a_context.seen.awayFromStart &&
                      ActorOf(a_context, Role::kPlayer).present,
                  a_context, kTravelDeadlineFrames,
                  "the player did not return to the start");
}

bool InView(View a_view, const StepContext &a_context) {
  return a_context.seen.firstPerson == (a_view == View::kFirstPerson);
}

StepMove Start(const SetView &a_set, const StepContext &a_context) {
  if (InView(a_set.view, a_context)) {
    return Pass(a_context, "already in that view");
  }
  return Issue(SetCamera{a_set.view}, a_context.owned);
}

StepMove Check(const SetView &a_set, const StepContext &a_context) {
  return PassWhen(InView(a_set.view, a_context), a_context,
                  kActionDeadlineFrames, "the camera did not switch");
}

std::string_view WorkName(Work a_work) {
  switch (a_work) {
  case Work::kNothing:
    return "nothing";
  case Work::kApply:
    return "apply";
  case Work::kEdit:
    return "edit";
  case Work::kGesture:
    return "gesture";
  case Work::kPaint:
    return "paint";
  }
  return "unknown";
}

std::string_view RequestName(RequestState a_request) {
  switch (a_request) {
  case RequestState::kNone:
    return "none";
  case RequestState::kWaiting:
    return "waiting";
  case RequestState::kPass:
    return "pass";
  case RequestState::kFail:
    return "fail";
  case RequestState::kBlocked:
    return "blocked";
  case RequestState::kAborted:
    return "aborted";
  }
  return "unknown";
}

bool Idle(const Activity &a_activity) {
  return a_activity.applications == 0 && !a_activity.paint &&
         !a_activity.gesture && !a_activity.fileOperations;
}

std::string Describe(const Activity &a_activity) {
  return std::format("applications {}, paint {}, gesture {}, "
                     "file operations {}",
                     a_activity.applications, a_activity.paint ? "yes" : "no",
                     a_activity.gesture ? "yes" : "no",
                     a_activity.fileOperations ? "yes" : "no");
}

StepMove Start(const LoadDuring &a_load, const StepContext &a_context) {
  Ownership owned = a_context.owned;
  owned.loadTarget = a_context.seen.loads + 1;
  return Issue(ReloadDuring{a_load.work, a_load.recipe}, owned);
}

StepMove Check(const LoadDuring &, const StepContext &a_context) {
  const bool loaded = a_context.seen.loads >= a_context.owned.loadTarget &&
                      ActorOf(a_context, Role::kPlayer).present;
  if (loaded) {
    return {Completed{}, std::nullopt, Ownership{}};
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
  if (a_context.seen.request == RequestState::kAborted) {
    return Pass(a_context);
  }
  return Fail(a_context, std::format("the request from before the load "
                                     "reported {}",
                                     RequestName(a_context.seen.request)));
}

std::string_view OutcomeOf(WorkOutcome a_outcome) {
  switch (a_outcome) {
  case WorkOutcome::kNone:
    return "never recorded";
  case WorkOutcome::kPending:
    return "still pending";
  case WorkOutcome::kApplied:
    return "applied before the load";
  case WorkOutcome::kCancelledByLoad:
    return "cancelled by the load";
  case WorkOutcome::kOther:
    return "ended for another reason";
  }
  return "unknown";
}

StepMove Start(const ExpectCancelled &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const ExpectCancelled &a_expect, const StepContext &a_context) {
  const WorkOutcome outcome = a_expect.work == Work::kGesture
                                  ? a_context.seen.activity.tuning
                                  : a_context.seen.activity.edit;
  if (outcome == WorkOutcome::kCancelledByLoad) {
    return Pass(a_context);
  }
  const std::string &detail = a_context.seen.activity.detail;
  return Fail(a_context,
              std::format("the {} was {}{}", WorkName(a_expect.work),
                          OutcomeOf(outcome),
                          detail.empty() ? "" : std::format(" ({})", detail)));
}

WorkOutcome OutcomeFor(Work a_work, const Activity &a_activity) {
  return a_work == Work::kGesture ? a_activity.tuning : a_activity.edit;
}

StepMove Start(const ExpectSettled &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const ExpectSettled &a_expect, const StepContext &a_context) {
  const WorkOutcome outcome =
      OutcomeFor(a_expect.work, a_context.seen.activity);
  if (outcome == WorkOutcome::kApplied ||
      outcome == WorkOutcome::kCancelledByLoad) {
    return Pass(a_context, std::string{OutcomeOf(outcome)});
  }
  if (a_context.frames < kActionDeadlineFrames &&
      outcome == WorkOutcome::kPending) {
    return Wait(a_context);
  }
  const std::string &detail = a_context.seen.activity.detail;
  return Fail(a_context,
              std::format("the {} was {}{}", WorkName(a_expect.work),
                          OutcomeOf(outcome),
                          detail.empty() ? "" : std::format(" ({})", detail)));
}

std::string DescribeScratch(const RecipeView &a_scratch) {
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
  const bool ended = a_outcome == WorkOutcome::kOther ||
                     a_outcome == WorkOutcome::kCancelledByLoad;
  if (!ended && a_context.frames < kRenderDeadlineFrames) {
    return Wait(a_context);
  }
  const std::string &detail = a_context.seen.activity.detail;
  return Fail(a_context,
              std::format("{} did not finish: {}; scratch {}{}", a_what,
                          OutcomeOf(a_outcome),
                          DescribeScratch(a_context.seen.scratch),
                          detail.empty() ? "" : std::format(" ({})", detail)));
}

StepMove Start(const DeleteScratch &, const StepContext &a_context) {
  if (!a_context.seen.scratch.loaded) {
    return Pass(a_context, "no scratch recipe loaded");
  }
  return Issue(RemoveRecipe{kScratchRecipe}, a_context.owned);
}

StepMove Check(const DeleteScratch &, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.edit == WorkOutcome::kApplied &&
                        !a_context.seen.scratch.loaded,
                    activity.edit, a_context, "the delete");
}

StepMove Start(const DuplicateToScratch &a_copy, const StepContext &a_context) {
  return Issue(CopyRecipe{a_copy.from, kScratchRecipe}, a_context.owned);
}

StepMove Check(const DuplicateToScratch &, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.edit == WorkOutcome::kApplied &&
                        a_context.seen.scratch.loaded,
                    activity.edit, a_context, "the duplicate");
}

StepMove Start(const SetScratchOpacity &a_set, const StepContext &a_context) {
  return Issue(EditOpacity{kScratchRecipe, a_set.value}, a_context.owned);
}

StepMove Check(const SetScratchOpacity &a_set, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.edit == WorkOutcome::kApplied &&
                        a_context.seen.scratch.firstOpacity == a_set.value,
                    activity.edit, a_context, "the opacity edit");
}

StepMove Start(const SaveScratch &, const StepContext &a_context) {
  return Issue(WriteRecipe{kScratchRecipe}, a_context.owned);
}

StepMove Check(const SaveScratch &, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.file == WorkOutcome::kApplied &&
                        !a_context.seen.scratch.dirty,
                    activity.file, a_context, "the save");
}

StepMove Start(const ExpectScratch &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const ExpectScratch &a_expect, const StepContext &a_context) {
  const RecipeView &scratch = a_context.seen.scratch;
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
                          a_expect.opacity, DescribeScratch(scratch)));
}

StepMove Start(const Begin &a_begin, const StepContext &a_context) {
  return Issue(BeginWork{a_begin.work, a_begin.recipe}, a_context.owned);
}

StepMove Check(const Begin &, const StepContext &a_context) {
  return Pass(a_context);
}

bool Active(Work a_work, const Activity &a_activity) {
  switch (a_work) {
  case Work::kPaint:
    return a_activity.paint;
  case Work::kGesture:
    return a_activity.gesture && a_activity.tuning == WorkOutcome::kPending;
  case Work::kNothing:
  case Work::kApply:
  case Work::kEdit:
    return true;
  }
  return false;
}

StepMove Start(const AwaitActive &, const StepContext &a_context) {
  return Wait(a_context);
}

StepMove Check(const AwaitActive &a_await, const StepContext &a_context) {
  if (Active(a_await.work, a_context.seen.activity)) {
    return Pass(a_context);
  }
  if (a_context.frames >= kActionDeadlineFrames) {
    return Fail(a_context,
                std::format("the {} never became active within {} frames ({})",
                            WorkName(a_await.work), kActionDeadlineFrames,
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
std::string Label(const Settle &a_settle) {
  return std::format("settle {}", a_settle.frames);
}

std::string Label(const Solo &a_solo) {
  return std::format("solo {}", a_solo.recipe);
}

std::string Label(const RestoreView &) { return std::string{"restore-view"}; }

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

std::string Label(const LeaveCell &) { return std::string{"leave-cell"}; }

std::string Label(const ReturnToStart &) {
  return std::string{"return-to-start"};
}

std::string Label(const SetView &a_s) {
  return std::string{a_s.view == View::kFirstPerson ? "set-view first-person"
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

std::string Label(const Begin &a_s) {
  return std::format("begin {}", WorkName(a_s.work));
}

std::string Label(const AwaitActive &a_s) {
  return std::format("await-active {}", WorkName(a_s.work));
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

std::array<bool, kRoleCount> RolesMoved(const Step &a_step) {
  const auto only = [](Role a_role) {
    std::array<bool, kRoleCount> moved{};
    moved[RoleIndex(a_role)] = true;
    return moved;
  };
  constexpr std::array<bool, kRoleCount> kNone{};
  constexpr std::array<bool, kRoleCount> kAll{true, true, true};
  return std::visit(
      Overloaded{
          [&](const Settle &) { return kNone; },
          [&](const AwaitRendered &) { return kNone; },
          [&](const AwaitRetired &) { return kNone; },
          [&](const AwaitBaseline &) { return kNone; },
          [&](const ExpectEffect &) { return kNone; },
          [&](const HoldUntouched &) { return kNone; },
          [&](const Solo &) { return kAll; },
          [&](const RestoreView &) { return kAll; },
          [&](const LeaveCell &) { return kAll; },
          [&](const ReturnToStart &) { return kAll; },
          [&](const SetView &) { return only(Role::kPlayer); },
          [&](const LoadDuring &) { return kAll; },
          [&](const ExpectAborted &) { return kNone; },
          [&](const ExpectCancelled &) { return kNone; },
          [&](const ExpectSettled &) { return kNone; },
          [&](const DeleteScratch &) { return kAll; },
          [&](const DuplicateToScratch &) { return kAll; },
          [&](const SetScratchOpacity &) { return kAll; },
          [&](const SaveScratch &) { return kAll; },
          [&](const ExpectScratch &) { return kNone; },
          [&](const Begin &) { return only(Role::kPlayer); },
          [&](const AwaitActive &) { return kNone; },
          [&](const AwaitIdle &) { return kNone; },
          [&](const auto &a_targeted) { return only(a_targeted.role); },
      },
      a_step);
}

std::string StepLabel(const Step &a_step) {
  return std::visit([](const auto &a_kind) { return Label(a_kind); }, a_step);
}
}
