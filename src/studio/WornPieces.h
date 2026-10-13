// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
[[nodiscard]] std::vector<WornPieceRow>
OrderedWornPieces(std::vector<WornPieceRow> a_rows);

[[nodiscard]] std::string WornPieceLabel(const WornPieceRow &a_piece);
[[nodiscard]] bool HasKeywords(const WornPieceRow &a_piece) noexcept;
[[nodiscard]] std::optional<RecipeKey> ArmorKeyOf(const WornPieceRow &a_piece);

[[nodiscard]] std::vector<RecipeKey>
KeywordKeysToAdd(std::span<const RecipeKey> a_keys,
                 std::span<const KeyChoice> a_offered);
[[nodiscard]] std::string KeywordOfferLabel(const WornPieceRow &a_piece,
                                            std::size_t a_adding);
}
