#include "engine/MenuDependency.h"
#include "test_support.h"

#include <string_view>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  std::size_t probes = 0;
  const auto missing = CheckMenuFramework(false, [&](const char *) {
    ++probes;
    return true;
  });
  Check(missing && missing->contains("not loaded") && probes == 0,
        "an unloaded framework refuses registration without probing exports");
  Check(missing && missing->contains("loader log") &&
            missing->contains("Recipe playback"),
        "missing framework explains recovery and optional editor scope");
  Check(!CheckMenuFramework(true, [](const char *) { return true; }),
        "a loaded framework with all required exports permits registration");
  for (const char *symbol : MenuFrameworkExports()) {
    const auto refused = CheckMenuFramework(true, [&](const char *name) {
      return std::string_view{name} != symbol;
    });
    Check(refused && refused->contains(symbol) &&
              refused->contains("editor disabled") &&
              refused->contains("restart"),
          "each missing export refuses registration with an actionable reason");
  }
  return test::Finish("engine_menudependency");
}
