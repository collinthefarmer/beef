// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/ActivePreviews.h"
#include "test_support.h"

#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  Snapshot snapshot;
  PieceRow piece;
  piece.ref = PieceRef{1, 2, false};
  piece.actorName = "Belrand";
  piece.armorName = "Cuirass";
  RecipeRow glow;
  glow.id = "glow";
  OutputRow emissive;
  emissive.index = 0;
  emissive.surface = Surface::kMaterial;
  emissive.slot = Slot::kEmissive;
  glow.outputs.push_back(emissive);
  piece.recipes.push_back(glow);
  snapshot.pieces.push_back(piece);

  Check(ActivePreviewsOf(View{}, snapshot).empty(),
        "a live view lists no previews");

  View view;
  view.soloPiece = piece.ref;
  view.isolation = Isolation{}.SoloLayer("glow", 0, 1, true);
  view.muted.insert(LayerKey{"glow", 0, 0});
  view.muted.insert(LayerKey{"gone", 3, 2});
  const std::vector<ActivePreview> previews = ActivePreviewsOf(view, snapshot);
  Check(previews.size() == 4, "each solo and mute is one entry");
  Check(previews[0].label == "Only Belrand / Cuirass (3rd)" &&
            Is<SoloPiece>(previews[0].clear),
        "a piece solo names the piece and clears with a piece solo off");
  Check(previews[1].label == "Solo glow / material / emissive / layer 2" &&
            Is<SoloRecipe>(previews[1].clear),
        "an isolation names its recipe, output and layer");
  Check(previews[2].label == "Muted glow / material / emissive / layer 1" &&
            previews[3].label == "Muted gone / output 4 / layer 3",
        "a mute names its recipe and layer, with a fallback for an unseen "
        "recipe");
  const auto *unmute = Get<MuteLayer>(previews[2].clear);
  Check(unmute && unmute->recipeID == "glow" && !unmute->on,
        "a mute clears with its own mute off");

  return test::Finish("studio_activepreviews");
}
