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
  painted.id = std::string{kPaintRecipe};
  painted.outputs.push_back(PaintOutput(Surface::kMaterial));
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

  PaintCommitRequest request{1, active.id, "engraving", "@metallic * 0.5"};
  const Recipe beforePaint = painted;
  const Recipe beforeTarget = active;
  const auto prepared = PreparePaintCommit(&painted, &active, request);
  Check(prepared.has_value(),
        "a valid paint commit prepares the requested expression");
  Check(painted == beforePaint && active == beforeTarget,
        "preparing a commit leaves both recipes unchanged");
  if (prepared) {
    Check(!Apply(active, *prepared),
          "a prepared paint commit applies atomically");
    const Mask *mask = active.FindMask("engraving");
    Check(mask && mask->text == request.expression &&
              active.FindSource("metallic"),
          "commit saves the submitted expression and its source dependencies");
  }

  Check(!PreparePaintCommit(nullptr, &active, request) &&
            !PreparePaintCommit(&painted, nullptr, request),
        "a missing paint or target recipe refuses the commit");
  request.expression = "@unknown";
  Check(
      !PreparePaintCommit(&painted, &active, request),
      "invalid submitted text is refused instead of saving the older preview");
  request.expression = "@metallic";
  request.maskName = "scratch";
  Check(!PreparePaintCommit(&painted, &active, request),
        "the scratch name cannot be used for a kept mask");

  Recipe conflict = beforeTarget;
  painted.signals.push_back(
      Signal{"paintOnly", ConstantSignal{1.0f}, std::nullopt});
  const Recipe beforeConflict = conflict;
  request.maskName = "missingSignal";
  request.expression = "@metallic + @paintOnly";
  const auto conflicting = PreparePaintCommit(&painted, &conflict, request);
  Check(conflicting.has_value(),
        "the transfer can be prepared before checking the target edits");
  if (conflicting) {
    Check(Apply(conflict, *conflicting).has_value(),
          "a signal missing from the target refuses the edit batch");
    Check(conflict == beforeConflict,
          "a refused commit rolls back sources added earlier in the batch");
  }

  Recipe aliases;
  aliases.sources.push_back(
      Source{"first", MaterialSource{MaterialChannel::kRoughness}});
  aliases.sources.push_back(
      Source{"second", MaterialSource{MaterialChannel::kRoughness}});
  aliases.masks.push_back(Mask{std::string{kScratchMask}, "@first + @second"});
  Recipe destination = beforeTarget;
  const EditBatch aliasEdits{KeepEdits(aliases, destination, "shared")};
  Check(!Apply(destination, aliasEdits),
        "paint source aliases transfer successfully");
  const Mask *shared = destination.FindMask("shared");
  Check(destination.sources.size() == 1 && shared &&
            shared->text == "@first + @first",
        "paint transfers reuse the staged source and retarget both references");

  return test::Finish("studio_paintsession");
}
