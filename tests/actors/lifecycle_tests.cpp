// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Lifecycle.h"
#include "actors/Reconcile.h"
#include "actors/fixtures.h"
#include "test_support.h"

#include <variant>
#include <vector>

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
  facts.pieces = std::move(a_pieces);
  return facts;
}

Phase Installed(ActorPlan a_plan) { return Present{std::move(a_plan), {}}; }

const Present *PresentOf(const Advanced &a_next) {
  return std::get_if<Present>(&a_next.phase);
}

void AnUnloadedActorRetiresEverything() {
  const ActorFacts gone;
  const Advanced next =
      Advance({Installed(CircuitPlan()), gone, ActorPlan{}, 0});
  test::Check(std::holds_alternative<Unloaded>(next.phase),
              "an unloaded actor becomes Unloaded");
  test::Check(
      Reconcile(AfterCommands(CircuitPlan(), next.commands), ActorPlan{})
          .empty(),
      "and its commands retire everything installed");
}

void EvictionHasHysteresis() {
  ActorFacts at = Loaded({Attached(0x1)});
  at.distance = 1100.0f;
  const Advanced evicted =
      Advance({Installed(CircuitPlan()), at, CircuitPlan(), 0, 1000.0f});
  test::Check(std::holds_alternative<Evicted>(evicted.phase) &&
                  !evicted.commands.empty(),
              "an installed actor beyond the eviction distance is evicted");

  at.distance = 900.0f;
  const Advanced held = Advance({evicted.phase, at, CircuitPlan(), 0, 1000.0f});
  test::Check(std::holds_alternative<Evicted>(held.phase),
              "an evicted actor stays evicted until well inside the distance");

  at.distance = 700.0f;
  const Advanced back = Advance({held.phase, at, CircuitPlan(), 0, 1000.0f});
  test::Check(PresentOf(back) && !back.commands.empty(),
              "an evicted actor inside 0.8 of the distance comes back");

  at.distance = 900.0f;
  const Advanced fresh =
      Advance({Phase{Unloaded{}}, at, CircuitPlan(), 0, 1000.0f});
  test::Check(PresentOf(fresh) != nullptr,
              "an arriving actor inside the distance is present");

  const ActorFacts player = Loaded({Attached(0x1)});
  const Advanced near =
      Advance({Installed(CircuitPlan()), player, CircuitPlan(), 0, 1000.0f});
  test::Check(PresentOf(near) != nullptr,
              "an actor without a distance is never evicted");
}

void AnArrivingActorInstalls() {
  const Advanced next =
      Advance({Phase{Unloaded{}}, Loaded({Attached(0x1)}), CircuitPlan(), 0});
  test::Check(PresentOf(next) != nullptr, "a loaded actor is Present");
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
  test::Check(waiting.commands.empty() && PresentOf(waiting) &&
                  PresentOf(waiting)->pending.size() == 1 &&
                  PresentOf(waiting)->pending.front().sinceMs == 100,
              "an installed actor keeps its effects while a model is pending");

  const Advanced stillWaiting =
      Advance({waiting.phase, pending, ActorPlan{}, 100 + kModelWaitMs - 1});
  test::Check(stillWaiting.commands.empty() &&
                  PresentOf(stillWaiting)->pending.front().sinceMs == 100,
              "the wait keeps its start and holds until its limit");

  const Advanced expired =
      Advance({stillWaiting.phase, pending, ActorPlan{}, 100 + kModelWaitMs});
  test::Check(!expired.commands.empty(),
              "an expired wait reconciles with what is attached");

  const Advanced attached =
      Advance({expired.phase, Loaded({Attached(0x1), Attached(0x2)}),
               CircuitPlan(), 100 + kModelWaitMs + 50});
  test::Check(PresentOf(attached) && PresentOf(attached)->pending.empty(),
              "an attached model leaves the pending list");
}

void EachPendingPieceWaitsOnItsOwn() {
  const Phase stuck = Present{CircuitPlan(), {PendingModel{Piece(0x2), 0}}};
  const Advanced unchanged = Advance(
      {stuck, Loaded({Attached(0x1), Pending(0x2)}), CircuitPlan(), 5000});
  test::Check(unchanged.commands.empty() && PresentOf(unchanged) &&
                  PresentOf(unchanged)->pending.front().sinceMs == 0,
              "a model that never attaches stops holding the actor back");

  const Advanced newPiece =
      Advance({stuck, Loaded({Attached(0x1), Pending(0x2), Pending(0x3)}),
               ActorPlan{}, 5000});
  test::Check(newPiece.commands.empty() && PresentOf(newPiece) &&
                  PresentOf(newPiece)->pending.size() == 2,
              "a newly pending piece waits even beside an expired one");
}

void AFirstInstallDoesNotWait() {
  const Advanced next =
      Advance({Phase{Unloaded{}}, Loaded({Attached(0x1), Pending(0x2)}),
               CircuitPlan(), 0});
  test::Check(!next.commands.empty() && PresentOf(next) != nullptr,
              "an actor with nothing installed installs what is attached");
}
}

int main() {
  AnUnloadedActorRetiresEverything();
  EvictionHasHysteresis();
  AnArrivingActorInstalls();
  UnchangedFactsCostNothing();
  AnInstalledActorWaitsForANewModel();
  EachPendingPieceWaitsOnItsOwn();
  AFirstInstallDoesNotWait();
  return test::Finish("actor lifecycle");
}
