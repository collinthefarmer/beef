#include "studio/Board.h"
#include "test_support.h"

#include <algorithm>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
OutputRow MaterialOutput(std::size_t a_index, Slot a_slot) {
  OutputRow output;
  output.index = a_index;
  output.target = Target::kMaterial;
  output.surface = Surface::kMaterial;
  output.slot = a_slot;
  return output;
}
}

int main() {
  RecipeRow recipe;
  recipe.id = "glow";
  recipe.shellMaterial = ShellMaterial::kVanilla;

  GeometryRow geometry;
  geometry.name = "body";
  geometry.shell = "SkinShell";

  OutputRow emissive = MaterialOutput(0, Slot::kEmissive);
  LayerRow layer;
  layer.mask = "@edges";
  emissive.layers.push_back(layer);
  geometry.outputs.push_back(emissive);

  geometry.outputs.push_back(MaterialOutput(1, Slot::kFuzz));

  OutputRow refused = MaterialOutput(2, Slot::kRmaos);
  refused.problem = "unknown source";
  geometry.outputs.push_back(refused);

  OutputRow light;
  light.index = 3;
  light.target = Target::kLight;
  geometry.outputs.push_back(light);

  Selection selection;
  View view;
  view.isolateRecipe = "glow";
  view.isolateOutput = 0;

  const Board board = BuildBoard(recipe, geometry, selection, view);

  Check(board.cells.size() == kSlotCount * 2,
        "the board holds one cell per slot per surface");
  Check(board.shell == "SkinShell", "the board carries the geometry shell");

  const Cell *written = CellAt(board, Surface::kMaterial, Slot::kEmissive);
  Check(written != nullptr && written->state == CellState::kWritten,
        "an output that writes a slot marks the cell written");
  Check(written != nullptr && written->output == 0,
        "the written cell names its output");
  Check(written != nullptr && std::ranges::find(written->badges, "edges") !=
                                  written->badges.end(),
        "a layer mask becomes a cell badge");
  Check(written != nullptr && written->isolated,
        "isolating the output marks the cell isolated");

  const Cell *empty = CellAt(board, Surface::kMaterial, Slot::kDiffuse);
  Check(empty != nullptr && empty->state == CellState::kEmpty,
        "an unwritten, unexcluded slot is empty");

  const Cell *excluded = CellAt(board, Surface::kMaterial, Slot::kGlint);
  Check(excluded != nullptr && excluded->state == CellState::kExcluded,
        "fuzz excludes glint on the same surface");

  const Cell *refusedCell = CellAt(board, Surface::kMaterial, Slot::kRmaos);
  Check(refusedCell != nullptr && refusedCell->state == CellState::kRefused,
        "an output with a problem refuses its cell");
  Check(refusedCell != nullptr && refusedCell->reason == "unknown source",
        "the refused cell carries the output problem as its reason");

  const Cell *absent = CellAt(board, Surface::kShell, Slot::kDiffuse);
  Check(absent != nullptr && absent->state == CellState::kAbsent,
        "a slot the surface does not offer is absent");

  const Cell *shellEmissive = CellAt(board, Surface::kShell, Slot::kEmissive);
  Check(shellEmissive != nullptr && shellEmissive->state == CellState::kEmpty,
        "the vanilla shell still offers emissive");

  Check(board.light.present && board.light.output == 3,
        "a light output in the geometry populates the light cell");

  return test::Finish("studio_board");
}
