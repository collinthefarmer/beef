#include "studio/GameObjects.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
GameObjectCandidate Candidate(std::string a_display, std::string a_value,
                              std::string a_qualifier = {}) {
  return GameObjectCandidate{std::move(a_display), std::move(a_value),
                             std::move(a_qualifier)};
}
}

int main() {
  for (std::size_t i = 0; i < kGameObjectKindCount; ++i) {
    Check(!GameObjectKindName(static_cast<GameObjectKind>(i)).empty(),
          "every kind names itself");
  }

  const GameObjectCandidate frost =
      Candidate("Frost Damage", "MagicDamageFrost", "Skyrim.esm");

  Check(CandidateMatches(frost, ""), "empty filter matches every candidate");
  Check(CandidateMatches(frost, "frost"),
        "filter matches the display, folding case");
  Check(CandidateMatches(frost, "DamageFrost"),
        "filter matches the committed value");
  Check(CandidateMatches(frost, "skyrim"),
        "filter matches the source qualifier");
  Check(!CandidateMatches(frost, "shock"), "an absent term matches nothing");

  return test::failures == 0 ? 0 : 1;
}
