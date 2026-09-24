// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/GameObjects.h"

#include "Core.h"
#include "studio/Names.h"

#include <array>
#include <format>

namespace BetterEnchantmentEffects::Studio {
namespace {
constexpr std::array<std::string_view, kGameObjectKindCount> kKindNames{
    "actor value",   "anim event",   "keyword", "enchantment",
    "effect shader", "magic effect", "armor",   "light",
};
}

std::string_view GameObjectKindName(GameObjectKind a_kind) noexcept {
  const auto index = IndexOf(a_kind);
  return index < kKindNames.size() ? kKindNames[index] : std::string_view{};
}

bool CandidateMatches(const GameObjectCandidate &a_candidate,
                      std::string_view a_filter) noexcept {
  return NameMatches(a_candidate.display, a_filter) ||
         NameMatches(a_candidate.value, a_filter) ||
         NameMatches(a_candidate.qualifier, a_filter);
}

std::string CandidateLabel(const GameObjectCandidate &a_candidate) {
  if (a_candidate.display == a_candidate.value) {
    return a_candidate.value;
  }
  return std::format("{}  ({})", a_candidate.display, a_candidate.value);
}
}
