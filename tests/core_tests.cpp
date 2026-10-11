// GPL-3.0-only with the additional permission in COPYING.md.
#include "Core.h"
#include "test_support.h"

#include <string>
#include <string_view>
#include <variant>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
struct Door {
  bool open = false;
};

struct Window {};

struct Push {};

struct Knock {
  int times = 0;
};

using Thing = std::variant<Door, Window>;
using Act = std::variant<Push, Knock>;

std::string Outcome(const Thing &a_thing, const Act &a_act) {
  return Match(
      a_thing, a_act,
      [](const Door &a_door, const Push &) {
        return std::string{a_door.open ? "closes" : "opens"};
      },
      [](const Door &, const Knock &a_knock) {
        return std::to_string(a_knock.times) + " knocks";
      },
      [](const auto &, const auto &) { return std::string{"nothing"}; });
}

void APairMatchesItsOwnCase() {
  Check(Outcome(Door{false}, Push{}) == "opens",
        "a pair reaches the case for both of its types");
  Check(Outcome(Door{true}, Push{}) == "closes",
        "the case receives the value the first variant holds");
  Check(Outcome(Door{}, Knock{3}) == "3 knocks",
        "the case receives the value the second variant holds");
}

void APairWithoutACaseFallsThrough() {
  Check(Outcome(Window{}, Push{}) == "nothing",
        "a pair without its own case reaches the catch-all");
  Check(Outcome(Window{}, Knock{2}) == "nothing",
        "the catch-all takes every remaining pair");
}

void OneVariantStillMatches() {
  const Act act = Knock{4};
  const int times = Match(
      act, [](const Push &) { return 0; },
      [](const Knock &a_knock) { return a_knock.times; });
  Check(times == 4, "a match over one variant keeps its own cases");
}
}

int main() {
  APairMatchesItsOwnCase();
  APairWithoutACaseFallsThrough();
  OneVariantStillMatches();
  return test::Finish("core");
}
