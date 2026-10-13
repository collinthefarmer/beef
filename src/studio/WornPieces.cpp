// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/WornPieces.h"

#include "Core.h"

#include <algorithm>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] const KeyChoice *ArmorChoiceOf(const WornPieceRow &a_piece) {
  const auto found =
      std::ranges::find_if(a_piece.keys, [](const KeyChoice &a_choice) {
        return a_choice.key.kind == KeyKind::kArmor;
      });
  return found != a_piece.keys.end() ? &*found : nullptr;
}

[[nodiscard]] std::string ArmorTextOf(const WornPieceRow &a_piece) {
  if (!a_piece.armorName.empty()) {
    return a_piece.armorName;
  }
  const KeyChoice *armor = ArmorChoiceOf(a_piece);
  return armor && !armor->text.empty() ? armor->text : a_piece.armor.ToString();
}

[[nodiscard]] bool IsListedBefore(const WornPieceRow &a_first,
                                  const WornPieceRow &a_second) {
  const std::string first = Lower(ArmorTextOf(a_first));
  const std::string second = Lower(ArmorTextOf(a_second));
  if (first != second) {
    return first < second;
  }
  return a_first.armor.ToString() < a_second.armor.ToString();
}

[[nodiscard]] bool IsSameArmor(const WornPieceRow &a_first,
                               const WornPieceRow &a_second) {
  return a_first.armor == a_second.armor;
}

[[nodiscard]] bool IsKeywordFor(const RecipeKey &a_key,
                                const KeyChoice &a_offered) {
  const FormRef *form = a_key.Form();
  if (a_key.kind != KeyKind::kKeyword || !form) {
    return false;
  }
  if (form->key && *form->key == a_offered.key.form) {
    return true;
  }
  return Lower(form->text) == Lower(a_offered.text);
}

[[nodiscard]] bool IsAmong(std::span<const RecipeKey> a_keys,
                           const KeyChoice &a_offered) {
  return std::ranges::any_of(a_keys, [&](const RecipeKey &a_key) {
    return IsKeywordFor(a_key, a_offered);
  });
}
}

std::vector<WornPieceRow> OrderedWornPieces(std::vector<WornPieceRow> a_rows) {
  std::ranges::stable_sort(a_rows, IsListedBefore);
  std::vector<WornPieceRow> ordered;
  for (WornPieceRow &row : a_rows) {
    if (std::ranges::none_of(ordered, [&](const WornPieceRow &a_kept) {
          return IsSameArmor(a_kept, row);
        })) {
      ordered.push_back(std::move(row));
    }
  }
  return ordered;
}

std::string WornPieceLabel(const WornPieceRow &a_piece) {
  if (!a_piece.enchantment) {
    return std::format("{} (unenchanted)", ArmorTextOf(a_piece));
  }
  return std::format("{} ({})", ArmorTextOf(a_piece), *a_piece.enchantment);
}

bool HasKeywords(const WornPieceRow &a_piece) noexcept {
  return std::ranges::any_of(a_piece.keys, [](const KeyChoice &a_choice) {
    return a_choice.key.kind == KeyKind::kKeyword;
  });
}

std::optional<RecipeKey> ArmorKeyOf(const WornPieceRow &a_piece) {
  if (const KeyChoice *armor = ArmorChoiceOf(a_piece)) {
    return RecipeKeyOf(armor->key, armor->text);
  }
  return std::nullopt;
}

std::vector<RecipeKey> KeywordKeysToAdd(std::span<const RecipeKey> a_keys,
                                        std::span<const KeyChoice> a_offered) {
  std::vector<RecipeKey> added;
  for (const KeyChoice &offered : a_offered) {
    if (offered.key.kind != KeyKind::kKeyword || offered.text.empty() ||
        IsAmong(a_keys, offered) || IsAmong(added, offered)) {
      continue;
    }
    added.push_back(RecipeKeyOf(offered.key, offered.text));
  }
  return added;
}

std::string KeywordOfferLabel(const WornPieceRow &a_piece,
                              std::size_t a_adding) {
  const std::string label = WornPieceLabel(a_piece);
  if (!HasKeywords(a_piece)) {
    return std::format("{}: no keywords", label);
  }
  if (a_adding == 0) {
    return std::format("{}: every keyword is already a key", label);
  }
  return std::format("{}: add {} keyword(s)", label, a_adding);
}
}
