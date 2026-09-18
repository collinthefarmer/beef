#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
enum class GameObjectKind {
  kActorValue,
  kAnimEvent,
  kKeyword,
  kEnchantment,
  kEffectShader,
  kMagicEffect,
  kArmor,
  kLight,
};
inline constexpr std::size_t kGameObjectKindCount = 8;

[[nodiscard]] std::string_view
GameObjectKindName(GameObjectKind a_kind) noexcept;

inline constexpr std::string_view kHitReceivedEvent{"hit.received"};
inline constexpr std::string_view kHitDealtEvent{"hit.dealt"};

struct GameObjectCandidate {
  std::string display;
  std::string value;
  std::string qualifier;

  [[nodiscard]] bool operator==(const GameObjectCandidate &) const = default;
};

struct GameObjectCatalog {
  GameObjectKind kind = GameObjectKind::kActorValue;
  std::vector<GameObjectCandidate> candidates;
  std::string sourceNote;
};

[[nodiscard]] bool CandidateMatches(const GameObjectCandidate &a_candidate,
                                    std::string_view a_filter) noexcept;
[[nodiscard]] std::string
CandidateLabel(const GameObjectCandidate &a_candidate);
}
