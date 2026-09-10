#include "studio/PaintSession.h"
#include "test_support.h"

#include "Core.h"

#include <algorithm>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  RecipeRow row;
  row.maskRows.push_back(TextRow{std::string{kScratchMask}, "@x", 0});
  Check(ScratchOf(row) == std::optional<std::string>{"@x"},
        "ScratchOf reads the scratch mask text");
  Check(ScratchOf(RecipeRow{}) == std::nullopt,
        "ScratchOf is empty when there is no scratch mask");

  const SurfaceOutput output = PaintOutput(Surface::kShell);
  Check(output.surface == Surface::kShell && output.stack.size() == 1 &&
            output.stack[0].mask ==
                std::optional<Ref>{Ref{std::string{kScratchMask}}},
        "PaintOutput lays one layer masked by the scratch mask");

  const auto surfaceEdits = PaintSurfaceEdits(Surface::kMaterial);
  Check(surfaceEdits.size() == 3 && Get<RemoveOutput>(surfaceEdits[0]) &&
            Get<AddOutput>(surfaceEdits[1]) && Get<AddLayer>(surfaceEdits[2]),
        "PaintSurfaceEdits replaces the output with one masked layer");

  Recipe active;
  active.id = "source";
  active.metadata.name = "Source";
  active.priority = 5;
  const Recipe paint = PaintRecipe(active, RecipeKey{}, Surface::kMaterial);
  Check(paint.id == kPaintRecipe && paint.priority == kPaintPriority &&
            paint.outputs.size() == 1 &&
            paint.FindMask(kScratchMask) != nullptr,
        "PaintRecipe rebuilds the active recipe around one scratch-masked "
        "output");

  Recipe painted;
  painted.sources.push_back(
      Source{"metallic", MaterialSource{MaterialChannel::kMetallic}});
  painted.masks.push_back(Mask{std::string{kScratchMask}, "@metallic"});
  const auto keep = KeepEdits(painted, active, "engraving");
  Check(keep.size() == 3,
        "KeepEdits names the source, adds the mask and sets it");
  const auto *set = keep.empty() ? nullptr : Get<SetMask>(keep.back());
  Check(set != nullptr && set->mask == "engraving" && set->text == "@metallic",
        "the kept mask carries the scratch expression under its new name");
  Check(std::ranges::any_of(
            keep, [](const RecipeEdit &e) { return Get<AddSource>(e); }),
        "the read source is brought across into the active recipe");

  return test::Finish("studio_paintsession");
}
