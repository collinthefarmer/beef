// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Reconcile.h"
#include "actors/fixtures.h"
#include "test_support.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <variant>
#include <vector>

using namespace actorfixtures;

namespace {
template <class Command>
std::size_t CountOf(const std::vector<ActorCommand> &a_commands) {
  return static_cast<std::size_t>(
      std::ranges::count_if(a_commands, [](const ActorCommand &a_command) {
        return std::holds_alternative<Command>(a_command);
      }));
}

template <class Command>
std::ptrdiff_t FirstOf(const std::vector<ActorCommand> &a_commands) {
  const auto found =
      std::ranges::find_if(a_commands, [](const ActorCommand &a_command) {
        return std::holds_alternative<Command>(a_command);
      });
  return found == a_commands.end() ? -1 : found - a_commands.begin();
}

template <class Command>
std::ptrdiff_t LastOf(const std::vector<ActorCommand> &a_commands) {
  std::ptrdiff_t last = -1;
  for (std::size_t i = 0; i < a_commands.size(); ++i) {
    if (std::holds_alternative<Command>(a_commands[i])) {
      last = static_cast<std::ptrdiff_t>(i);
    }
  }
  return last;
}

bool Converges(const ActorPlan &a_from, const ActorPlan &a_to) {
  const ActorPlan after = AfterCommands(a_from, Reconcile(a_from, a_to));
  return Reconcile(after, a_to).empty() &&
         after.instances.size() == a_to.instances.size() &&
         after.geometries.size() == a_to.geometries.size() &&
         after.placements.size() == a_to.placements.size();
}

ActorPlan Circuit() {
  return PlanBuilder{}
      .With(Instance("circuit"), Geo(0x1, "Body"))
      .With(Instance("circuit"), Geo(0x1, "Trim"))
      .plan;
}

ActorPlan CircuitAndWard() {
  return PlanBuilder{}
      .With(Instance("circuit"), Geo(0x1, "Body"))
      .With(Instance("circuit"), Geo(0x1, "Trim"))
      .With(Instance("ward"), Geo(0x1, "Body"))
      .plan;
}

void AMatchingPlanNeedsNothing() {
  test::Check(Reconcile(Circuit(), Circuit()).empty(),
              "reconciling a plan with itself gives no commands");
  test::Check(Reconcile(ActorPlan{}, ActorPlan{}).empty(),
              "reconciling two empty plans gives no commands");
}

void EveryPairConverges() {
  const std::vector<ActorPlan> plans{
      ActorPlan{}, Circuit(), CircuitAndWard(),
      PlanBuilder{}.With(Instance("ward"), Geo(0x2, "Helm")).plan};
  for (const ActorPlan &from : plans) {
    for (const ActorPlan &to : plans) {
      test::Check(Converges(from, to),
                  "performing the commands reaches the plan");
    }
  }
}

void RetiresBeforeInstalling() {
  const ActorPlan helm =
      PlanBuilder{}.With(Instance("ward"), Geo(0x2, "Helm")).plan;
  const std::vector<ActorCommand> commands = Reconcile(Circuit(), helm);
  test::Check(LastOf<RetirePlacement>(commands) <
                  FirstOf<RetireGeometry>(commands),
              "placements retire before their geometries");
  test::Check(LastOf<RetireGeometry>(commands) <
                  FirstOf<RetireInstance>(commands),
              "geometries retire before instances");
  test::Check(LastOf<RetireInstance>(commands) <
                  FirstOf<InstallInstance>(commands),
              "everything retires before anything installs");
  test::Check(LastOf<InstallInstance>(commands) <
                      FirstOf<InstallPlacement>(commands) &&
                  LastOf<InstallGeometry>(commands) <
                      FirstOf<InstallPlacement>(commands),
              "instances and geometries install before their placements");
  test::Check(FirstOf<ChainGeometry>(commands) >
                  LastOf<InstallPlacement>(commands),
              "chains rebuild after the placements change");
}

void AddingARecipeTouchesOnlyThatRecipe() {
  const std::vector<ActorCommand> commands =
      Reconcile(Circuit(), CircuitAndWard());
  test::Equal(commands.size(), std::size_t{3},
              "one instance, one placement and one chain");
  test::Equal(CountOf<InstallInstance>(commands), std::size_t{1},
              "the new recipe's instance installs");
  test::Equal(CountOf<InstallPlacement>(commands), std::size_t{1},
              "only the new placement installs");
  test::Check(CountOf<RetirePlacement>(commands) == 0 &&
                  CountOf<RetireGeometry>(commands) == 0 &&
                  CountOf<InstallGeometry>(commands) == 0,
              "the installed recipe and its geometries are kept");
  const ChainGeometry *chain = std::get_if<ChainGeometry>(&commands.back());
  test::Check(chain && chain->ref == Geo(0x1, "Body"),
              "only the geometry that gained a placement rechains");
}

void ANewModelReplacesItsGeometries() {
  ActorPlan remodelled = Circuit();
  remodelled.geometries.front().model = ModelToken{2};
  const std::vector<ActorCommand> commands = Reconcile(Circuit(), remodelled);
  test::Equal(CountOf<RetireGeometry>(commands), std::size_t{1},
              "the geometry whose model changed retires");
  test::Equal(CountOf<InstallGeometry>(commands), std::size_t{1},
              "and installs again on the new model");
  test::Equal(CountOf<RetirePlacement>(commands), std::size_t{1},
              "its placement retires with it");
  test::Equal(CountOf<InstallPlacement>(commands), std::size_t{1},
              "and installs again");
  test::Equal(CountOf<RetireInstance>(commands), std::size_t{0},
              "the instance and its clock are kept");
}

void ARecipeEditReplacesItsInstance() {
  const ActorPlan edited = PlanBuilder{}
                               .With(Instance("circuit", 2), Geo(0x1, "Body"))
                               .With(Instance("circuit", 2), Geo(0x1, "Trim"))
                               .plan;
  const std::vector<ActorCommand> commands = Reconcile(Circuit(), edited);
  test::Equal(CountOf<RetireInstance>(commands), std::size_t{1},
              "the instance of the old recipe retires");
  test::Equal(CountOf<InstallInstance>(commands), std::size_t{1},
              "the instance of the edited recipe installs");
  test::Equal(CountOf<RetireGeometry>(commands), std::size_t{0},
              "the geometries stay bound");
  test::Equal(CountOf<ChainGeometry>(commands), std::size_t{2},
              "both geometries rechain");
}

void AChangedPlacementReinstalls() {
  ActorPlan reprioritised = Circuit();
  reprioritised.placements.front().priority = 5;
  const std::vector<ActorCommand> commands =
      Reconcile(Circuit(), reprioritised);
  test::Check(CountOf<RetirePlacement>(commands) == 1 &&
                  CountOf<InstallPlacement>(commands) == 1 &&
                  CountOf<ChainGeometry>(commands) == 1,
              "a placement whose content changed reinstalls and rechains");
}

void RetiredGeometriesDoNotRechain() {
  const std::vector<ActorCommand> commands = Reconcile(Circuit(), ActorPlan{});
  test::Equal(CountOf<ChainGeometry>(commands), std::size_t{0},
              "a geometry that leaves the plan does not rechain");
}
}

int main() {
  AMatchingPlanNeedsNothing();
  EveryPairConverges();
  RetiresBeforeInstalling();
  AddingARecipeTouchesOnlyThatRecipe();
  ANewModelReplacesItsGeometries();
  ARecipeEditReplacesItsInstance();
  AChangedPlacementReinstalls();
  RetiredGeometriesDoNotRechain();
  return test::Finish("actor reconcile");
}
