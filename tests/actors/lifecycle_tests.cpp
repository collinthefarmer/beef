// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Lifecycle.h"
#include "actors/Reconcile.h"
#include "actors/fixtures.h"
#include "test_support.h"

#include <variant>

using namespace actorfixtures;

namespace {
ActorPlan CircuitPlan() {
  return PlanBuilder{}.With(Instance("circuit"), Geo(0x1, "Body")).plan;
}

PieceFacts Attached(std::uint32_t a_item) {
  PieceFacts piece;
  piece.ref = Piece(a_item);
  piece.model = ModelToken{1};
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
  facts.inRange = true;
  facts.pieces = std::move(a_pieces);
  return facts;
}

Phase Installed(ActorPlan a_plan) { return Present{std::move(a_plan), {}}; }

void AnUnloadedActorRetiresEverything() {
  ActorFacts gone;
  const Advanced next =
      Advance({Installed(CircuitPlan()), gone, ActorPlan{}, 0});
  test::Check(std::holds_alternative<Unloaded>(next.phase),
              "an unloaded actor becomes Unloaded");
  test::Check(
      Reconcile(AfterCommands(CircuitPlan(), next.commands), ActorPlan{})
          .empty(),
      "and its commands retire everything installed");
}

void AnActorOutOfRangeIsEvicted() {
  ActorFacts far = Loaded({Attached(0x1)});
  far.inRange = false;
  const Advanced next =
      Advance({Installed(CircuitPlan()), far, ActorPlan{}, 0});
  test::Check(std::holds_alternative<Evicted>(next.phase),
              "an actor out of range becomes Evicted");
  test::Check(!next.commands.empty(), "and its effects retire");
}

void AnArrivingActorInstalls() {
  const Advanced next =
      Advance({Phase{Unloaded{}}, Loaded({Attached(0x1)}), CircuitPlan(), 0});
  test::Check(std::holds_alternative<Present>(next.phase),
              "a loaded actor in range is Present");
  test::Check(
      Reconcile(AfterCommands(ActorPlan{}, next.commands), CircuitPlan())
          .empty(),
      "and its commands install the plan");
}

void UnchangedFactsCostNothing() {
  const Advanced next = Advance(
      {Installed(CircuitPlan()), Loaded({Attached(0x1)}), CircuitPlan(), 0});
  test::Check(next.commands.empty(),
              "an equip that changes no piece gives no commands");
}

void AnInstalledActorWaitsForANewModel() {
  const ActorFacts pending = Loaded({Attached(0x1), Pending(0x2)});
  const Advanced waiting =
      Advance({Installed(CircuitPlan()), pending, ActorPlan{}, 100});
  const Present *present = std::get_if<Present>(&waiting.phase);
  test::Check(waiting.commands.empty() && present && present->wait &&
                  present->wait->sinceMs == 100 && !present->wait->expired,
              "an installed actor keeps its effects while a model is pending");

  const Advanced stillWaiting =
      Advance({waiting.phase, pending, ActorPlan{}, 100 + kModelWaitMs - 1});
  test::Check(stillWaiting.commands.empty(), "the wait holds until its limit");

  const Advanced expired =
      Advance({stillWaiting.phase, pending, ActorPlan{}, 100 + kModelWaitMs});
  const Present *after = std::get_if<Present>(&expired.phase);
  test::Check(!expired.commands.empty() && after && after->wait &&
                  after->wait->expired,
              "an expired wait reconciles with what is attached");

  const Advanced attached =
      Advance({expired.phase, Loaded({Attached(0x1), Attached(0x2)}),
               CircuitPlan(), 100 + kModelWaitMs + 50});
  const Present *done = std::get_if<Present>(&attached.phase);
  test::Check(done && !done->wait, "an attached model ends the wait");
}

void AnExpiredWaitDoesNotWaitAgain() {
  const ActorFacts pending = Loaded({Attached(0x1), Pending(0x2)});
  const Phase expired = Present{CircuitPlan(), ModelWait{0, true}};
  const Advanced next = Advance({expired, pending, CircuitPlan(), 10});
  const Present *present = std::get_if<Present>(&next.phase);
  test::Check(present && present->wait && present->wait->expired &&
                  next.commands.empty(),
              "an expired wait reconciles at once and stays expired");
}

void AFirstInstallDoesNotWait() {
  const Advanced next =
      Advance({Phase{Unloaded{}}, Loaded({Attached(0x1), Pending(0x2)}),
               CircuitPlan(), 0});
  const Present *present = std::get_if<Present>(&next.phase);
  test::Check(!next.commands.empty() && present && !present->wait,
              "an actor with nothing installed installs what is attached");
}
}

int main() {
  AnUnloadedActorRetiresEverything();
  AnActorOutOfRangeIsEvicted();
  AnArrivingActorInstalls();
  UnchangedFactsCostNothing();
  AnInstalledActorWaitsForANewModel();
  AnExpiredWaitDoesNotWaitAgain();
  AFirstInstallDoesNotWait();
  return test::Finish("actor lifecycle");
}
