// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/ActivePreviews.h"

#include "studio/Selection.h"

#include <algorithm>
#include <format>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] const RecipeRow *AnyRecipeRow(const Snapshot &a_snapshot,
                                            std::string_view a_id) {
  for (const PieceRow &piece : a_snapshot.pieces) {
    if (const RecipeRow *row = FindById(piece.recipes, a_id)) {
      return row;
    }
  }
  return FindById(a_snapshot.documents, a_id);
}

[[nodiscard]] std::string OutputText(const Snapshot &a_snapshot,
                                     std::string_view a_recipe,
                                     std::size_t a_output) {
  const RecipeRow *recipe = AnyRecipeRow(a_snapshot, a_recipe);
  const OutputRow *output =
      recipe ? FindBy(recipe->outputs, a_output, &OutputRow::index) : nullptr;
  if (!output) {
    return std::format("output {}", a_output + 1);
  }
  return std::format("{} / {}", SurfaceName(output->surface),
                     SlotName(output->slot));
}

[[nodiscard]] std::string PieceText(const Snapshot &a_snapshot,
                                    const PieceRef &a_piece) {
  const auto found =
      std::ranges::find_if(a_snapshot.pieces, [&](const PieceRow &a_row) {
        return a_row.ref.SamePiece(a_piece);
      });
  if (found == a_snapshot.pieces.end()) {
    return std::format("armor {:08X} on actor {:08X}", a_piece.armorID,
                       a_piece.actorID);
  }
  return PieceLabel(*found);
}

[[nodiscard]] std::string IsolationText(const Isolation &a_isolation,
                                        const Snapshot &a_snapshot) {
  std::string text = std::format("Solo {}", a_isolation.recipeID);
  if (a_isolation.output) {
    text += " / " +
            OutputText(a_snapshot, a_isolation.recipeID, *a_isolation.output);
  }
  if (a_isolation.layer) {
    text += std::format(" / layer {}", *a_isolation.layer + 1);
  }
  return text;
}
}

std::vector<ActivePreview> ActivePreviewsOf(const View &a_view,
                                            const Snapshot &a_snapshot) {
  std::vector<ActivePreview> previews;
  if (a_view.soloPiece) {
    previews.push_back(
        {std::format("Only {}", PieceText(a_snapshot, *a_view.soloPiece)),
         SoloPiece{*a_view.soloPiece, false}});
  }
  if (a_view.Isolating()) {
    previews.push_back({IsolationText(a_view.isolation, a_snapshot),
                        SoloRecipe{a_view.isolation.recipeID, false}});
  }
  for (const LayerKey &key : a_view.muted) {
    previews.push_back(
        {std::format("Muted {} / {} / layer {}", key.recipeID,
                     OutputText(a_snapshot, key.recipeID, key.output),
                     key.layer + 1),
         MuteLayer{key.recipeID, key.output, key.layer, false}});
  }
  return previews;
}
}
