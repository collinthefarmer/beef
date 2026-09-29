// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Signals.h"
#include "test_support.h"

#include <algorithm>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  const auto folder =
      test::Fixtures().parent_path().parent_path() / "recipes/examples";
  for (const std::string name :
       {"arcane-circuit", "resonant-ward", "winterglass"}) {
    auto loaded = ParseRecipe(test::ReadFile(folder / (name + ".json")), name);
    Check(loaded.recipe && !loaded.HasErrors(), "demo recipe loads cleanly");
    if (!loaded.recipe || loaded.HasErrors())
      continue;
    const auto graph = RecipeGraph::Compile(*loaded.recipe);
    const auto all = graph.TickOrder();
    const auto changing = graph.ChangingTickOrder();
    Check(std::ranges::all_of(changing,
                              [&](NodeId id) {
                                return std::ranges::find(all, id) != all.end();
                              }),
          "the changing tick order is a subset of the tick order");
    Check(changing.size() < all.size(),
          "a demo recipe has unchanging tick nodes to fold");
    const NullEnvironment environment;
    SignalState ticked{graph};
    float time = 0.0f;
    for (int tick = 0; tick < 50; ++tick, time += 0.25f)
      ticked.Tick(environment, {time, 0.25f});
    SignalState fresh{graph};
    fresh.Tick(environment, {time - 0.25f, 0.25f});
    std::size_t stale = 0;
    for (const auto id : all)
      if (std::ranges::find(changing, id) == changing.end() &&
          ticked.ValueOf(OutputRef{id, 0}) != fresh.ValueOf(OutputRef{id, 0}))
        ++stale;
    test::Equal(stale, std::size_t{0},
                "an unchanging node evaluated once equals its value at the "
                "latest tick");
  }
  return test::Finish("tick folding");
}
