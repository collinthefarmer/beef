// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/GameObjects.h"

#include "Core.h"
#include "studio/Names.h"

#include <array>
#include <format>

namespace BetterEnchantmentEffects::Studio {
namespace {
struct GameObjectKindSpec {
  std::string_view name;
  bool viewedActor = false;
};

constexpr std::array<GameObjectKindSpec, kGameObjectKindCount> kKinds{{
    {"actor value"},
    {"anim event", true},
    {"keyword"},
    {"enchantment"},
    {"effect shader"},
    {"magic effect"},
    {"armor"},
    {"light"},
    {"node", true},
    {"texture", true},
}};
}

std::string_view GameObjectKindName(GameObjectKind a_kind) noexcept {
  const auto index = IndexOf(a_kind);
  return index < kKinds.size() ? kKinds[index].name : std::string_view{};
}

bool IsViewedActorKind(GameObjectKind a_kind) noexcept {
  const auto index = IndexOf(a_kind);
  return index < kKinds.size() && kKinds[index].viewedActor;
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

std::string QualifiedLabel(const GameObjectCandidate &a_candidate) {
  if (a_candidate.qualifier.empty()) {
    return CandidateLabel(a_candidate);
  }
  return std::format("{}  [{}]", CandidateLabel(a_candidate),
                     a_candidate.qualifier);
}
}
