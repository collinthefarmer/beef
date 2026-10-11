// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Lifecycle.h"
#include "actors/Reconcile.h"
#include "actors/fixtures.h"
#include "test_support.h"

#include <cstdint>
#include <optional>
#include <vector>

using namespace actorfixtures;

namespace {
ActorPlan CircuitPlan() {
  return PlanBuilder{}.With(Instance("circuit"), Geo(0x1, "Body")).plan;
}

PieceFacts Attached(std::uint32_t a_item, std::uint64_t a_model = 1) {
  PieceFacts piece;
  piece.ref = Piece(a_item);
  piece.model = ModelToken{a_model};
  return piece;
}

PieceFacts Pending(std::uint32_t a_item) {
  PieceFacts piece;
  piece.ref = Piece(a_item);
  return piece;
}

ActorFacts Loaded(std::vector<PieceFacts> a_pieces) {
  ActorFacts facts;
  facts.loaded = true;
  facts.pieces = std::move(a_pieces);
  return facts;
}

std::optional<ActorState> InstalledState(ActorPlan a_plan) {
  return ActorState{a_plan, a_plan, {}, {}};
}

const std::optional<ActorState> kNoRecord;

void AnUnloadedActorRetiresEverything() {
  const ActorFacts gone;
  const Transition next =
      Advance({InstalledState(CircuitPlan()), gone, ActorPlan{}, 0});
  test::Check(!next.state, "an unloaded actor leaves no record");
  test::Check(
      Reconcile(AfterCommands(CircuitPlan(), next.commands), ActorPlan{})
          .empty(),
      "and its commands retire everything installed");
}

void EvictionHasHysteresis() {
  ActorFacts at = Loaded({Attached(0x1)});
  at.distance = 1100.0f;
  const Transition evicted =
      Advance({InstalledState(CircuitPlan()), at, CircuitPlan(), 0, 1000.0f});
  test::Check(evicted.state && evicted.state->installed == ActorPlan{} &&
                  evicted.state->plan == CircuitPlan() &&
                  !evicted.commands.empty(),
              "an evicted actor retires everything and keeps its plan");

  at.distance = 900.0f;
  const Transition held =
      Advance({evicted.state, at, CircuitPlan(), 0, 1000.0f});
  test::Check(held.commands.empty() && held.state &&
                  held.state->installed == ActorPlan{},
              "an evicted actor stays evicted until well inside the distance");

  at.distance = 700.0f;
  const Transition back = Advance({held.state, at, CircuitPlan(), 0, 1000.0f});
  test::Check(back.state && back.state->installed == CircuitPlan() &&
                  !back.commands.empty(),
              "an evicted actor inside 0.8 of the distance comes back");

  const ActorFacts player = Loaded({Attached(0x1)});
  const Transition near = Advance(
      {InstalledState(CircuitPlan()), player, CircuitPlan(), 0, 1000.0f});
  test::Check(near.commands.empty() && AnythingInstalled(near.state),
              "an actor without a distance is never evicted");
}

void AReinstallKeepsTheStayingDistance() {
  ActorFacts at = Loaded({Attached(0x1)});
  at.distance = 900.0f;
  const Transition unchanged =
      Advance({InstalledState(CircuitPlan()), at, CircuitPlan(), 0, 1000.0f,
               Reinstall::kIfChanged});
  test::Check(unchanged.commands.empty(),
              "an unchanged plan between 0.8 and 1.0 of the distance costs "
              "nothing");

  const Transition reinstalled =
      Advance({InstalledState(CircuitPlan()), at, CircuitPlan(), 0, 1000.0f,
               Reinstall::kAlways});
  test::Check(!reinstalled.commands.empty() && reinstalled.state &&
                  reinstalled.state->installed == CircuitPlan(),
              "a reinstall between 0.8 and 1.0 of the distance installs the "
              "plan again");
  test::Check(
      Reconcile(AfterCommands(ActorPlan{}, reinstalled.commands), CircuitPlan())
          .empty(),
      "and its commands install the whole plan");

  at.distance = 1100.0f;
  const Transition evicted =
      Advance({InstalledState(CircuitPlan()), at, CircuitPlan(), 0, 1000.0f,
               Reinstall::kAlways});
  test::Check(evicted.state && evicted.state->installed == ActorPlan{} &&
                  !evicted.commands.empty(),
              "a reinstall beyond the distance evicts");

  at.distance = 900.0f;
  const Transition empty =
      Advance({InstalledState(CircuitPlan()), at, ActorPlan{}, 0, 1000.0f,
               Reinstall::kAlways});
  test::Check(
      !empty.state &&
          Reconcile(AfterCommands(CircuitPlan(), empty.commands), ActorPlan{})
              .empty(),
      "a reinstall of an empty plan retires everything");

  const Transition unwaited = Advance(
      {InstalledState(CircuitPlan()), Loaded({Attached(0x1), Pending(0x2)}),
       CircuitPlan(), 100, 0.0f, Reinstall::kAlways});
  test::Check(!unwaited.commands.empty() && unwaited.state &&
                  unwaited.state->installed == CircuitPlan(),
              "a reinstall does not wait for a pending model");
}

void AnArrivingActorInstallsInsideTheRestoreDistance() {
  ActorFacts at = Loaded({Attached(0x1)});
  at.distance = 900.0f;
  const Transition between =
      Advance({kNoRecord, at, CircuitPlan(), 0, 1000.0f});
  test::Check(between.commands.empty() && between.state &&
                  between.state->plan == CircuitPlan(),
              "an arriving actor between 0.8 and 1.0 of the distance waits "
              "with its plan recorded");
  at.distance = 700.0f;
  const Transition inside =
      Advance({between.state, at, CircuitPlan(), 0, 1000.0f});
  test::Check(!inside.commands.empty() && AnythingInstalled(inside.state),
              "and installs once inside 0.8 of the distance");
}

void AnArrivingActorInstalls() {
  const Transition next =
      Advance({kNoRecord, Loaded({Attached(0x1)}), CircuitPlan(), 0});
  test::Check(next.state && next.state->installed == CircuitPlan(),
              "the next state holds what the commands install");
  test::Check(
      Reconcile(AfterCommands(ActorPlan{}, next.commands), CircuitPlan())
          .empty(),
      "and the commands install the plan");
}

void AnActorThatWantsNothingIsForgotten() {
  const Transition next = Advance(
      {InstalledState(CircuitPlan()), Loaded({Attached(0x1)}), ActorPlan{}, 0});
  test::Check(!next.state && !next.commands.empty(),
              "an actor whose plan empties retires everything and leaves no "
              "record");
  ActorFacts far = Loaded({Attached(0x1)});
  far.distance = 5000.0f;
  test::Check(!Advance({kNoRecord, far, ActorPlan{}, 0, 1000.0f}).state,
              "an actor that wants nothing keeps no record, near or far");
}

void UnchangedFactsCostNothing() {
  const Transition next = Advance({InstalledState(CircuitPlan()),
                                   Loaded({Attached(0x1)}), CircuitPlan(), 0});
  test::Check(next.commands.empty() &&
                  next.state == InstalledState(CircuitPlan()),
              "an equip that changes no piece gives no commands");
}

void AnInstalledActorWaitsForANewModel() {
  const ActorFacts pending = Loaded({Attached(0x1), Pending(0x2)});
  const Transition waiting =
      Advance({InstalledState(CircuitPlan()), pending, ActorPlan{}, 100});
  test::Check(waiting.commands.empty() && waiting.state &&
                  waiting.state->pending.size() == 1 &&
                  waiting.state->pending.front().sinceMs == 100,
              "an installed actor keeps its effects while a model is pending");

  const Transition stillWaiting =
      Advance({waiting.state, pending, ActorPlan{}, 100 + kModelWaitMs - 1});
  test::Check(stillWaiting.commands.empty() && stillWaiting.state &&
                  stillWaiting.state->pending.front().sinceMs == 100,
              "the wait keeps its start and holds until its limit");

  const Transition expired =
      Advance({stillWaiting.state, pending, ActorPlan{}, 100 + kModelWaitMs});
  test::Check(!expired.commands.empty(),
              "an expired wait reconciles with what is attached");

  const Transition attached =
      Advance({expired.state, Loaded({Attached(0x1), Attached(0x2)}),
               CircuitPlan(), 100 + kModelWaitMs + 50});
  test::Check(attached.state && attached.state->pending.empty(),
              "an attached model leaves the pending list");
}

void EachPendingPieceWaitsOnItsOwn() {
  const std::optional<ActorState> stuck = ActorState{
      CircuitPlan(), CircuitPlan(), {PendingModel{Piece(0x2), 0}}, {}};
  const Transition unchanged = Advance(
      {stuck, Loaded({Attached(0x1), Pending(0x2)}), CircuitPlan(), 5000});
  test::Check(unchanged.commands.empty() && unchanged.state &&
                  unchanged.state->pending.front().sinceMs == 0,
              "a model that never attaches stops holding the actor back");

  const Transition newPiece =
      Advance({stuck, Loaded({Attached(0x1), Pending(0x2), Pending(0x3)}),
               ActorPlan{}, 5000});
  test::Check(newPiece.commands.empty() && newPiece.state &&
                  newPiece.state->pending.size() == 2,
              "a newly pending piece waits even beside an expired one");
}

void InRangeDerivesHysteresisFromWhatIsInstalled() {
  test::Check(InRange(true, 1000.0f, 1000.0f) &&
                  !InRange(true, 1001.0f, 1000.0f),
              "an actor with something installed keeps it out to the distance");
  test::Check(!InRange(false, 800.0f, 1000.0f) &&
                  InRange(false, 799.0f, 1000.0f),
              "an actor with nothing installed comes in inside 0.8 of it");
  test::Check(InRange(false, 5000.0f, 0.0f) && InRange(true, 5000.0f, 0.0f),
              "every actor is in range when eviction is off");
  test::Check(InRange(false, std::nullopt, 1000.0f),
              "an actor without a distance is always in range");
}

void AnEvictedActorUnloadsQuietly() {
  const std::optional<ActorState> evicted =
      ActorState{CircuitPlan(), ActorPlan{}, {}, {}};
  const Transition next =
      Advance({evicted, ActorFacts{}, ActorPlan{}, 0, 1000.0f});
  test::Check(!next.state && next.commands.empty(),
              "an evicted actor that unloads has nothing to retire");
}

void FirstPersonPiecesDoNotHoldBack() {
  PieceFacts firstPerson = Pending(0x2);
  firstPerson.ref.firstPerson = true;
  const Transition next = Advance(
      {kNoRecord, Loaded({Attached(0x1), firstPerson}), CircuitPlan(), 100});
  test::Check(!next.commands.empty(),
              "a pending first-person model does not hold back the install");
  test::Check(next.state && next.state->pending.size() == 1,
              "and it stays pending until it attaches");
}

void RecheckFollowsTheWait() {
  const std::optional<ActorState> waiting = ActorState{
      CircuitPlan(), CircuitPlan(), {PendingModel{Piece(0x2), 0}}, {}};
  test::Check(ModelLoadOf(waiting, kModelWaitMs) == ModelLoad::kLoading,
              "a pending model is rechecked soon until just after its wait");
  test::Check(ModelLoadOf(waiting, kModelWaitMs + kLoadingCheckMs) ==
                  ModelLoad::kLate,
              "a model that never attaches is rechecked slowly");
  test::Check(ModelLoadOf(InstalledState(CircuitPlan()), 0) == ModelLoad::kNone,
              "an actor with nothing pending is not rechecked");
  test::Check(ModelLoadOf(kNoRecord, 0) == ModelLoad::kNone,
              "an actor without a record is not rechecked");
}

void TheWaitSurvivesAClockWrap() {
  const std::uint32_t since = UINT32_MAX - 10;
  const std::optional<ActorState> waiting = ActorState{
      CircuitPlan(), CircuitPlan(), {PendingModel{Piece(0x2), since}}, {}};
  const Transition next = Advance(
      {waiting, Loaded({Attached(0x1), Pending(0x2)}), ActorPlan{}, 50});
  test::Check(next.commands.empty(),
              "a wait that started before the clock wrapped still holds");
}

void AWaitHoldsAnotherPiecesRetire() {
  const ActorPlan both = PlanBuilder{}
                             .With(Instance("circuit"), Geo(0x1, "Body"))
                             .With(Instance("circuit"), Geo(0x3, "Hands"))
                             .plan;
  const Transition next =
      Advance({InstalledState(both), Loaded({Attached(0x1), Pending(0x2)}),
               CircuitPlan(), 100});
  test::Check(next.commands.empty(),
              "a retire waits with the install for at most the model wait");
}

void AFirstInstallDoesNotWait() {
  const Transition next = Advance(
      {kNoRecord, Loaded({Attached(0x1), Pending(0x2)}), CircuitPlan(), 0});
  test::Check(!next.commands.empty() && AnythingInstalled(next.state),
              "an actor with nothing installed installs what is attached");
}

void ClaimsLastAsLongAsTheirModel() {
  const Claim body{Geo(0x1, "Body"), ModelToken{1}};
  const std::vector<Claim> claims{body};
  test::Check(LiveClaims(claims, Loaded({Attached(0x1, 1)})).size() == 1,
              "a claim stands while its piece keeps the claimed model");
  test::Check(LiveClaims(claims, Loaded({Attached(0x1, 2)})).empty(),
              "a new model ends the claim");
  test::Check(LiveClaims(claims, Loaded({})).empty(),
              "a removed piece ends the claim");
  const ActorState once = WithClaim(ActorState{}, body);
  test::Check(WithClaim(once, body).claims.size() == 1,
              "a claim is recorded once");
}

void AClaimKeepsTheRecord() {
  const Claim body{Geo(0x1, "Body"), ModelToken{1}};
  const std::optional<ActorState> claimed = ActorState{{}, {}, {}, {body}};
  const Transition next =
      Advance({claimed, Loaded({Attached(0x1)}), ActorPlan{}, 0});
  test::Check(next.state && next.state->claims.size() == 1,
              "an actor holding only a claim keeps its record");
  const Transition reequipped =
      Advance({claimed, Loaded({Attached(0x1, 2)}), ActorPlan{}, 0});
  test::Check(!reequipped.state,
              "once the model changes the claim ends and the record goes");
}

void ClaimingAnInstalledGeometryRetiresOnlyIt() {
  const ActorPlan both = PlanBuilder{}
                             .With(Instance("circuit"), Geo(0x1, "Body"))
                             .With(Instance("circuit"), Geo(0x3, "Hands"))
                             .plan;
  const ActorPlan withoutBody =
      PlanBuilder{}.With(Instance("circuit"), Geo(0x3, "Hands")).plan;
  const ActorState claimed =
      WithClaim(*InstalledState(both), Claim{Geo(0x1, "Body"), ModelToken{1}});
  const Transition next = Advance(
      {claimed, Loaded({Attached(0x1), Attached(0x3)}), withoutBody, 0});
  const ActorPlan after = AfterCommands(both, next.commands);
  test::Check(next.state && next.state->installed == withoutBody &&
                  after == withoutBody && next.state->claims.size() == 1,
              "a plan without the claimed geometry retires only that geometry");
}
}

int main() {
  AnUnloadedActorRetiresEverything();
  EvictionHasHysteresis();
  AReinstallKeepsTheStayingDistance();
  AnArrivingActorInstallsInsideTheRestoreDistance();
  AnArrivingActorInstalls();
  AnActorThatWantsNothingIsForgotten();
  UnchangedFactsCostNothing();
  AnInstalledActorWaitsForANewModel();
  EachPendingPieceWaitsOnItsOwn();
  AFirstInstallDoesNotWait();
  InRangeDerivesHysteresisFromWhatIsInstalled();
  AnEvictedActorUnloadsQuietly();
  FirstPersonPiecesDoNotHoldBack();
  RecheckFollowsTheWait();
  TheWaitSurvivesAClockWrap();
  AWaitHoldsAnotherPiecesRetire();
  ClaimsLastAsLongAsTheirModel();
  AClaimKeepsTheRecord();
  ClaimingAnInstalledGeometryRetiresOnlyIt();
  return test::Finish("actor lifecycle");
}
