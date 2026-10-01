// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/StepRules.h"

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

const ActorFacts &ActorOf(const StepContext &a_context, Role a_role) {
  return a_context.seen.actors[IndexOf(a_role)];
}

const ItemFacts &ArmorOf(const StepContext &a_context, Role a_role,
                         Item a_item) {
  return ActorOf(a_context, a_role).armor[IndexOf(a_item)];
}

bool Added(const RunChanges &a_changes, Role a_role, Item a_item) {
  return a_changes.added[IndexOf(a_role)][IndexOf(a_item)];
}

StepMove Pass(std::string a_reason = {}) {
  return {Completed{Outcome::kPass, std::move(a_reason)}, std::nullopt};
}

StepMove PassAndIssue(Command a_command) { return {Completed{}, a_command}; }

StepMove Block(std::string a_reason) {
  return {Completed{Outcome::kBlocked, std::move(a_reason)}, std::nullopt};
}

StepMove Fail(std::string a_reason,
              std::optional<Command> a_command = std::nullopt) {
  return {Completed{Outcome::kFail, std::move(a_reason)}, a_command};
}

StepMove Abort(std::string a_reason) {
  return {Completed{Outcome::kAborted, std::move(a_reason)}, std::nullopt};
}

StepMove Wait() { return {Pending{}, std::nullopt}; }

StepMove Issue(Command a_command) { return {Pending{}, a_command}; }

StepMove PassWhen(bool a_done, const StepContext &a_context,
                  std::uint32_t a_deadline, std::string_view a_waitingFor) {
  if (a_done) {
    return Pass();
  }
  if (a_context.frames >= a_deadline) {
    return Fail(std::format("{} within {} frames", a_waitingFor, a_deadline));
  }
  return Wait();
}

std::string Describe(const ActorFacts &a_actor) {
  return std::format(
      "present {}, wears fixture {}, live {}, residue {}, "
      "latest application {}",
      a_actor.present ? "yes" : "no",
      a_actor.armor[IndexOf(Item::kFixture)].equipped ? "yes" : "no",
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

StepMove ExpectWithin(bool a_holds, const StepContext &a_context,
                      std::string a_failure) {
  if (a_holds) {
    return Pass();
  }
  if (a_context.frames >= kExpectDeadlineFrames) {
    return Fail(std::move(a_failure));
  }
  return Wait();
}

std::string WithDetail(std::string a_text, const Activity &a_activity) {
  if (!a_activity.detail.empty()) {
    a_text += std::format(" ({})", a_activity.detail);
  }
  return a_text;
}

std::string Missing(Role a_role) {
  return std::format("the {} is not present", NameOf(kRoles, a_role));
}

StepMove Start(const WaitFrames &, const StepContext &) { return Wait(); }

StepMove Check(const WaitFrames &a_settle, const StepContext &a_context) {
  return a_context.frames >= a_settle.frames ? Pass() : Wait();
}

StepMove Start(const Solo &a_solo, const StepContext &) {
  return PassAndIssue(SoloRecipe{a_solo.recipe});
}

StepMove Check(const Solo &, const StepContext &) { return Pass(); }

StepMove Start(const RestoreSolo &, const StepContext &) {
  return PassAndIssue(EndSolo{});
}

StepMove Check(const RestoreSolo &, const StepContext &) { return Pass(); }

StepMove Start(const Spawn &a_spawn, const StepContext &a_context) {
  if (a_spawn.role == Role::kPlayer) {
    return Fail("the player cannot be spawned");
  }
  if (!a_context.seen.npcEffects) {
    return Block("effects are limited to the player in the settings");
  }
  if (a_context.changes.spawned[IndexOf(a_spawn.role)]) {
    return Pass("already spawned");
  }
  return Issue(SpawnActor{a_spawn.role});
}

StepMove Check(const Spawn &a_spawn, const StepContext &a_context) {
  return PassWhen(ActorOf(a_context, a_spawn.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not appear");
}

StepMove Start(const Despawn &a_despawn, const StepContext &a_context) {
  if (!a_context.changes.spawned[IndexOf(a_despawn.role)]) {
    return Pass("the run spawned no such actor");
  }
  return Issue(DespawnActor{a_despawn.role});
}

StepMove Check(const Despawn &a_despawn, const StepContext &a_context) {
  return PassWhen(!ActorOf(a_context, a_despawn.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not disappear");
}

StepMove Start(const Disable &a_disable, const StepContext &a_context) {
  if (!a_context.changes.spawned[IndexOf(a_disable.role)]) {
    return Block("only an actor the run spawned can be disabled");
  }
  return Issue(DisableActor{a_disable.role});
}

StepMove Check(const Disable &a_disable, const StepContext &a_context) {
  return PassWhen(!ActorOf(a_context, a_disable.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not disappear");
}

StepMove Start(const Enable &a_enable, const StepContext &a_context) {
  if (!a_context.changes.spawned[IndexOf(a_enable.role)]) {
    return Block("only an actor the run spawned can be enabled");
  }
  return Issue(EnableActor{a_enable.role});
}

StepMove Check(const Enable &a_enable, const StepContext &a_context) {
  return PassWhen(ActorOf(a_context, a_enable.role).present, a_context,
                  kActionDeadlineFrames, "the actor did not reappear");
}

StepMove Start(const Equip &a_equip, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_equip.role);
  const ItemFacts &armor = ArmorOf(a_context, a_equip.role, a_equip.item);
  if (!a_context.seen.itemsLoaded[IndexOf(a_equip.item)]) {
    return Block(
        std::format("the {} is not loaded", NameOf(kItems, a_equip.item)));
  }
  if (!actor.present) {
    return Block(Missing(a_equip.role));
  }
  if (armor.equipped) {
    return Pass("already equipped");
  }
  if (armor.carried) {
    if (!Added(a_context.changes, a_equip.role, a_equip.item)) {
      return Block("the actor carries a copy the run did not add");
    }
    return Issue(EquipCarried{a_equip.role, a_equip.item});
  }
  if (a_equip.role == Role::kPlayer && actor.bodyArmorWorn) {
    return Block("the player wears body armor");
  }
  return Issue(AddAndEquip{a_equip.role, a_equip.item});
}

StepMove Check(const Equip &a_equip, const StepContext &a_context) {
  return PassWhen(ArmorOf(a_context, a_equip.role, a_equip.item).equipped,
                  a_context, kActionDeadlineFrames, "the armor did not equip");
}

StepMove Start(const Unequip &a_unequip, const StepContext &a_context) {
  if (!ArmorOf(a_context, a_unequip.role, a_unequip.item).equipped) {
    return Pass("not equipped");
  }
  return Issue(UnequipArmor{a_unequip.role, a_unequip.item});
}

StepMove Check(const Unequip &a_unequip, const StepContext &a_context) {
  return PassWhen(!ArmorOf(a_context, a_unequip.role, a_unequip.item).equipped,
                  a_context, kActionDeadlineFrames,
                  "the armor did not unequip");
}

StepMove Start(const Remove &a_remove, const StepContext &a_context) {
  if (!Added(a_context.changes, a_remove.role, a_remove.item)) {
    return Pass("the run added none");
  }
  return Issue(RemoveArmor{a_remove.role, a_remove.item});
}

StepMove Check(const Remove &a_remove, const StepContext &a_context) {
  return PassWhen(!ArmorOf(a_context, a_remove.role, a_remove.item).carried,
                  a_context, kActionDeadlineFrames,
                  "the armor did not leave the inventory");
}

StepMove CheckOutcome(Outcome a_outcome) {
  switch (a_outcome) {
  case Outcome::kPass:
    return Pass();
  case Outcome::kFail:
    return Fail("the plugin reported a failure");
  case Outcome::kBlocked:
    return Block("nothing rendered on the demo cuirass, or the "
                 "actor was not ready");
  case Outcome::kAborted:
    return Abort("the plugin cancelled the request");
  }
  return Fail("unknown request outcome");
}

StepMove CheckRequest(const StepContext &a_context) {
  return std::visit(
      Overloaded{
          [&](const NoRequest &) {
            return Block("the plugin refused the request");
          },
          [&](const RequestPending &) {
            if (a_context.frames >= kRenderDeadlineFrames) {
              return Fail(std::format("no result within {} frames",
                                      kRenderDeadlineFrames),
                          AbortRequest{});
            }
            return Wait();
          },
          [&](Outcome a_outcome) { return CheckOutcome(a_outcome); },
      },
      a_context.seen.request);
}

StepMove Start(const Apply &a_apply, const StepContext &) {
  return Issue(SubmitApply{a_apply.role});
}

StepMove Check(const Apply &, const StepContext &a_context) {
  return CheckRequest(a_context);
}

StepMove Start(const Retire &a_retire, const StepContext &) {
  return Issue(SubmitRetire{a_retire.role});
}

StepMove Check(const Retire &, const StepContext &a_context) {
  return CheckRequest(a_context);
}

StepMove Start(const AwaitRendered &, const StepContext &) { return Wait(); }

StepMove Check(const AwaitRendered &a_await, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_await.role);
  const bool rendered =
      actor.present &&
      actor.renderedAttempt > a_context.renderedBefore[IndexOf(a_await.role)];
  if (rendered && actor.residue > 0) {
    return Pass();
  }
  if (a_context.frames < kRenderDeadlineFrames) {
    return Wait();
  }
  return Fail(std::format("{} ({})",
                          rendered ? "a new application rendered, but no "
                                     "plugin texture or shell was found on "
                                     "the actor"
                                   : std::format("no new application rendered "
                                                 "within {} frames",
                                                 kRenderDeadlineFrames),
                          Describe(actor)));
}

StepMove Start(const AwaitRetired &, const StepContext &) { return Wait(); }

StepMove Check(const AwaitRetired &a_await, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_await.role);
  return WithActorState(PassWhen(!actor.live, a_context, kRenderDeadlineFrames,
                                 "the plugin did not retire the actor"),
                        actor);
}

StepMove Start(const AwaitBaseline &, const StepContext &) { return Wait(); }

StepMove Check(const AwaitBaseline &a_await, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_await.role);
  return WithActorState(
      PassWhen(actor.present && actor.residue == 0, a_context,
               kRenderDeadlineFrames,
               "plugin textures or shells stayed on the actor"),
      actor);
}

StepMove Start(const ExpectEffect &, const StepContext &) { return Wait(); }

StepMove Check(const ExpectEffect &a_expect, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_expect.role);
  return WithActorState(
      PassWhen(actor.present && actor.live && actor.residue > 0, a_context,
               kExpectDeadlineFrames, "the effect was not on the actor"),
      actor);
}

StepMove Start(const HoldUntouched &a_hold, const StepContext &a_context) {
  if (!ActorOf(a_context, a_hold.role).present) {
    return Block(Missing(a_hold.role));
  }
  return Wait();
}

StepMove Check(const HoldUntouched &a_hold, const StepContext &a_context) {
  const ActorFacts &actor = ActorOf(a_context, a_hold.role);
  if (actor.live || actor.residue > 0) {
    return Fail(
        std::format("the {} gained plugin state", NameOf(kRoles, a_hold.role)));
  }
  return a_context.frames >= a_hold.frames ? Pass() : Wait();
}

StepMove Start(const LeaveStart &, const StepContext &a_context) {
  if (a_context.changes.away) {
    return Pass("already away");
  }
  return Issue(TravelFromStart{});
}

StepMove Check(const LeaveStart &, const StepContext &a_context) {
  return PassWhen(a_context.seen.awayFromStart &&
                      ActorOf(a_context, Role::kPlayer).present,
                  a_context, kTravelDeadlineFrames,
                  "the player did not leave the start cell");
}

StepMove Start(const ReturnToStart &, const StepContext &a_context) {
  if (!a_context.changes.away) {
    return Pass("never left");
  }
  return Issue(TravelToStart{});
}

StepMove Check(const ReturnToStart &, const StepContext &a_context) {
  return PassWhen(!a_context.seen.awayFromStart &&
                      ActorOf(a_context, Role::kPlayer).present,
                  a_context, kTravelDeadlineFrames,
                  "the player did not return to the start");
}

bool InView(Camera a_camera, const StepContext &a_context) {
  return a_context.seen.camera == a_camera;
}

StepMove Start(const SetCamera &a_set, const StepContext &a_context) {
  if (InView(a_set.camera, a_context)) {
    return Pass("already in that view");
  }
  return Issue(SwitchCamera{a_set.camera});
}

StepMove Check(const SetCamera &a_set, const StepContext &a_context) {
  return PassWhen(InView(a_set.camera, a_context), a_context,
                  kActionDeadlineFrames, "the camera did not switch");
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

StepMove Start(const LoadDuring &a_load, const StepContext &) {
  return Issue(LoadSaveWith{a_load.work, a_load.recipe});
}

StepMove Check(const LoadDuring &, const StepContext &a_context) {
  const bool loaded = a_context.seen.loads > a_context.loadsAtStart &&
                      ActorOf(a_context, Role::kPlayer).present;
  if (loaded) {
    return Pass();
  }
  if (a_context.frames >= kTravelDeadlineFrames) {
    return Fail(std::format("the save did not load within {} frames",
                            kTravelDeadlineFrames));
  }
  return Wait();
}

StepMove Start(const ExpectAborted &, const StepContext &) { return Wait(); }

StepMove Check(const ExpectAborted &, const StepContext &a_context) {
  return ExpectWithin(
      a_context.seen.request == RequestState{Outcome::kAborted}, a_context,
      std::format("the request from before the load reported {}",
                  RequestName(a_context.seen.request)));
}

WorkOutcome TrackedOutcomeOf(TrackedWork a_work, const Activity &a_activity) {
  return a_work == TrackedWork::kGesture ? a_activity.gestureOutcome
                                         : a_activity.editOutcome;
}

std::string_view PhraseOf(WorkOutcome a_outcome) {
  return NameOf(kWorkOutcomePhrases, a_outcome);
}

StepMove Start(const ExpectCancelled &, const StepContext &) { return Wait(); }

StepMove Check(const ExpectCancelled &a_expect, const StepContext &a_context) {
  const WorkOutcome outcome =
      TrackedOutcomeOf(a_expect.work, a_context.seen.activity);
  return ExpectWithin(
      outcome == WorkOutcome::kCancelledByLoad, a_context,
      WithDetail(std::format("the {} was {}", WordOf(a_expect.work),
                             PhraseOf(outcome)),
                 a_context.seen.activity));
}

StepMove Start(const ExpectSettled &, const StepContext &) { return Wait(); }

StepMove Check(const ExpectSettled &a_expect, const StepContext &a_context) {
  const WorkOutcome outcome =
      TrackedOutcomeOf(a_expect.work, a_context.seen.activity);
  if (outcome == WorkOutcome::kApplied ||
      outcome == WorkOutcome::kCancelledByLoad) {
    return Pass(std::string{PhraseOf(outcome)});
  }
  return ExpectWithin(
      false, a_context,
      WithDetail(std::format("the {} was {}", WordOf(a_expect.work),
                             PhraseOf(outcome)),
                 a_context.seen.activity));
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
    return Pass();
  }
  const bool ended = a_outcome == WorkOutcome::kFailed ||
                     a_outcome == WorkOutcome::kCancelledByLoad;
  if (!ended && a_context.frames < kRenderDeadlineFrames) {
    return Wait();
  }
  const std::string &detail = a_context.seen.activity.detail;
  return Fail(std::format("{} did not finish: {}; scratch {}{}", a_what,
                          PhraseOf(a_outcome), Describe(a_context.seen.scratch),
                          detail.empty() ? "" : std::format(" ({})", detail)));
}

StepMove Start(const DeleteScratch &, const StepContext &a_context) {
  if (!a_context.seen.scratch.loaded) {
    return Pass("no scratch recipe loaded");
  }
  return Issue(StartDelete{kScratchRecipe});
}

StepMove Check(const DeleteScratch &, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.editOutcome == WorkOutcome::kApplied &&
                        !a_context.seen.scratch.loaded,
                    activity.editOutcome, a_context, "the delete");
}

StepMove Start(const DuplicateToScratch &a_copy, const StepContext &) {
  return Issue(StartDuplicate{a_copy.from, kScratchRecipe});
}

StepMove Check(const DuplicateToScratch &, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.editOutcome == WorkOutcome::kApplied &&
                        a_context.seen.scratch.loaded,
                    activity.editOutcome, a_context, "the duplicate");
}

StepMove Start(const SetScratchOpacity &a_set, const StepContext &) {
  return Issue(StartOpacityEdit{kScratchRecipe, a_set.value});
}

StepMove Check(const SetScratchOpacity &a_set, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.editOutcome == WorkOutcome::kApplied &&
                        a_context.seen.scratch.firstOpacity == a_set.value,
                    activity.editOutcome, a_context, "the opacity edit");
}

StepMove Start(const SaveScratch &, const StepContext &) {
  return Issue(StartSave{kScratchRecipe});
}

StepMove Check(const SaveScratch &, const StepContext &a_context) {
  const Activity &activity = a_context.seen.activity;
  return FinishWhen(activity.fileOutcome == WorkOutcome::kApplied &&
                        !a_context.seen.scratch.dirty,
                    activity.fileOutcome, a_context, "the save");
}

StepMove Start(const ExpectScratch &, const StepContext &) { return Wait(); }

StepMove Check(const ExpectScratch &a_expect, const StepContext &a_context) {
  const RecipeFacts &scratch = a_context.seen.scratch;
  return ExpectWithin(scratch.loaded && !scratch.dirty &&
                          scratch.firstOpacity == a_expect.opacity,
                      a_context,
                      std::format("the scratch recipe is not the saved one "
                                  "with opacity {}: {}",
                                  a_expect.opacity, Describe(scratch)));
}

StepMove Start(const SpawnCrowd &a_spawn, const StepContext &a_context) {
  if (a_spawn.count == 0 || a_spawn.count > kMaxCrowd) {
    return Fail(std::format("a crowd has 1 to {} actors", kMaxCrowd));
  }
  if (!a_context.seen.npcEffects) {
    return Block("effects are limited to the player in the settings");
  }
  if (a_context.changes.crowd > 0) {
    return Pass("already spawned");
  }
  return Issue(SpawnCrowdActors{a_spawn.count, a_spawn.body, a_spawn.dress});
}

StepMove Check(const SpawnCrowd &, const StepContext &a_context) {
  const CrowdFacts &crowd = a_context.seen.crowd;
  if (crowd.present >= a_context.changes.crowd) {
    return PassAndIssue(EquipCrowdArmor{});
  }
  if (a_context.frames >= kActionDeadlineFrames) {
    return Fail(std::format("only {} of {} crowd actors appeared within {} "
                            "frames",
                            crowd.present, a_context.changes.crowd,
                            kActionDeadlineFrames));
  }
  return Wait();
}

StepMove Start(const AwaitCrowdRendered &, const StepContext &a_context) {
  if (a_context.changes.crowd == 0) {
    return Block("no crowd was spawned");
  }
  return Wait();
}

StepMove Check(const AwaitCrowdRendered &, const StepContext &a_context) {
  const CrowdFacts &crowd = a_context.seen.crowd;
  return PassWhen(crowd.rendered >= a_context.changes.crowd, a_context,
                  kCrowdDeadlineFrames,
                  std::format("only {} of {} crowd actors rendered",
                              crowd.rendered, a_context.changes.crowd));
}

StepMove Start(const StartCrowdFight &, const StepContext &a_context) {
  if (a_context.changes.crowd < 2) {
    return Block("a fight needs a crowd of two or more");
  }
  if (a_context.changes.crowdBody == Body::kMannequin) {
    return Block("mannequins cannot fight");
  }
  return PassAndIssue(SetCrowdHostile{});
}

StepMove Check(const StartCrowdFight &, const StepContext &) { return Pass(); }

StepMove Start(const AwaitCrowdFighting &, const StepContext &a_context) {
  if (a_context.changes.crowd < 2) {
    return Block("a fight needs a crowd of two or more");
  }
  return Wait();
}

StepMove Check(const AwaitCrowdFighting &, const StepContext &a_context) {
  const CrowdFacts &crowd = a_context.seen.crowd;
  return PassWhen(crowd.fighting * 2 >= a_context.changes.crowd, a_context,
                  kCrowdDeadlineFrames,
                  std::format("only {} of {} crowd actors are in combat",
                              crowd.fighting, a_context.changes.crowd));
}

StepMove Start(const UnequipCrowd &, const StepContext &a_context) {
  if (a_context.changes.crowd == 0) {
    return Block("no crowd was spawned");
  }
  return PassAndIssue(UnequipCrowdArmor{});
}

StepMove Check(const UnequipCrowd &, const StepContext &) { return Pass(); }

StepMove Start(const EquipCrowd &, const StepContext &a_context) {
  if (a_context.changes.crowd == 0) {
    return Block("no crowd was spawned");
  }
  return PassAndIssue(EquipCrowdArmor{});
}

StepMove Check(const EquipCrowd &, const StepContext &) { return Pass(); }

StepMove Start(const AwaitCrowdBare &, const StepContext &a_context) {
  if (a_context.changes.crowd == 0) {
    return Block("no crowd was spawned");
  }
  return Wait();
}

StepMove Check(const AwaitCrowdBare &, const StepContext &a_context) {
  const CrowdFacts &crowd = a_context.seen.crowd;
  if (crowd.present < a_context.changes.crowd) {
    return Fail(std::format("only {} of {} crowd actors stayed present",
                            crowd.present, a_context.changes.crowd));
  }
  return PassWhen(crowd.rendered == 0, a_context, kCrowdDeadlineFrames,
                  std::format("{} of {} crowd actors kept the effect",
                              crowd.rendered, a_context.changes.crowd));
}

StepMove Start(const DespawnCrowd &, const StepContext &a_context) {
  if (a_context.changes.crowd == 0) {
    return Pass("no crowd");
  }
  return Issue(DespawnCrowdActors{});
}

StepMove Check(const DespawnCrowd &, const StepContext &a_context) {
  return PassWhen(a_context.seen.crowd.present == 0, a_context,
                  kActionDeadlineFrames, "the crowd did not disappear");
}

StepMove Start(const HoldWindow &, const StepContext &) { return Wait(); }

StepMove Check(const HoldWindow &a_hold, const StepContext &a_context) {
  if (a_context.seen.nowMs - a_context.startedAtMs >=
      std::uint64_t{a_hold.seconds} * 1000) {
    return Pass();
  }
  const std::uint64_t stalled =
      (std::uint64_t{a_hold.seconds} + 1) * kMaxFramesPerSecond;
  if (a_context.frames >= stalled) {
    return Fail("the game clock did not advance");
  }
  return Wait();
}

StepMove Start(const BeginSession &a_begin, const StepContext &) {
  return Issue(OpenSession{a_begin.session, a_begin.recipe});
}

StepMove Check(const BeginSession &, const StepContext &) { return Pass(); }

StepMove Start(const BeginWindow &, const StepContext &) { return Pass(); }

StepMove Check(const BeginWindow &, const StepContext &) { return Pass(); }

StepMove Start(const EndWindow &, const StepContext &) { return Pass(); }

StepMove Check(const EndWindow &, const StepContext &) { return Pass(); }

bool SessionActive(Session a_session, const Activity &a_activity) {
  return a_session == Session::kPaint
             ? a_activity.paintActive
             : a_activity.gestureActive &&
                   a_activity.gestureOutcome == WorkOutcome::kPending;
}

StepMove Start(const AwaitSessionActive &, const StepContext &) {
  return Wait();
}

StepMove Check(const AwaitSessionActive &a_await,
               const StepContext &a_context) {
  if (SessionActive(a_await.session, a_context.seen.activity)) {
    return Pass();
  }
  if (a_context.frames >= kRenderDeadlineFrames) {
    return Fail(
        WithDetail(std::format("the {} never became active within {} frames",
                               WordOf(a_await.session), kRenderDeadlineFrames),
                   a_context.seen.activity));
  }
  return Wait();
}

StepMove Start(const AwaitIdle &, const StepContext &) { return Wait(); }

StepMove Check(const AwaitIdle &, const StepContext &a_context) {
  if (Idle(a_context.seen.activity)) {
    return Pass();
  }
  if (a_context.frames >= kRenderDeadlineFrames) {
    return Fail(std::format("work stayed pending: {}",
                            Describe(a_context.seen.activity)));
  }
  return Wait();
}

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
    moved[IndexOf(a_role)] = true;
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
          [&](const StartCrowdFight &) { return kNone; },
          [&](const AwaitCrowdFighting &) { return kNone; },
          [&](const UnequipCrowd &) { return kNone; },
          [&](const EquipCrowd &) { return kNone; },
          [&](const AwaitCrowdBare &) { return kNone; },
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

}
