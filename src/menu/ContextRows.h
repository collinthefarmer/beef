#pragma once

#include "menu/Frame.h"
#include "recipe/Recipe.h"
#include "studio/Forms.h"
#include "studio/Snapshot.h"

#include <cstddef>
#include <optional>
#include <span>

namespace BetterEnchantmentEffects::Menu {
[[nodiscard]] std::optional<RecipeKey>
DefaultKeyOf(const Studio::PieceRow &a_piece);

void DrawRecipeSettings(const Frame &a_frame);
void DrawStudioContext(const Frame &a_frame);
void DrawOutputHeader(const Studio::OutputRow &a_output,
                      std::span<const Studio::FormField> a_scalars,
                      const Frame &a_frame, std::string_view a_note = {});
}
