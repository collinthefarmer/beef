// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RecipeFiles.h"
#include "engine/TextFile.h"
#include "studio/History.h"
#include "studio/PaintSession.h"
#include "test_support.h"

#include <filesystem>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
Recipe Imported() {
  Recipe recipe;
  recipe.id = "imported";
  recipe.keys = {RecipeKey{}};
  recipe.metadata.imported = "effectShader:source";
  recipe.masks = {Mask{"scratch", "0.25", {}}, Mask{"peek", "0.5", {}},
                  Mask{"authored", "0.75", {}}};
  SurfaceOutput output;
  output.scalars.strength = 1.0f;
  for (const char *name : {"scratch", "peek", "authored"}) {
    Layer layer;
    layer.source = Vec3{1.0f, 0.0f, 0.0f};
    layer.mask = Ref{name};
    output.stack.push_back(layer);
  }
  recipe.outputs = {output};
  return recipe;
}

void SavePromotion(const std::filesystem::path &a_dir) {
  const Recipe original = Imported();
  const auto source = a_dir / "imported" / "imported.json";
  const auto target = a_dir / "user" / "imported.json";
  const auto originalBytes = SerializeRecipe(original);
  Check(WriteText(source, originalBytes), "seed imported source");
  Check(WriteText(target, "previous user file"), "seed user destination");
  const auto staged = std::filesystem::path{target.string() + ".writing"};
  std::filesystem::create_directory(staged);
  Recipe current = original;
  const auto refused = WriteRecipeFile(target, current, true);
  Check(!refused && refused.error().where.contains(current.id) &&
            refused.error().message.contains(target.string()),
        "failed promotion identifies the recipe and destination");
  Check(current == original && !current.metadata.imported.empty(),
        "failed promotion preserves all in-memory import and paint state");
  Check(ReadText(source).value_or("") == originalBytes &&
            ReadText(target).value_or("") == "previous user file",
        "failed promotion preserves imported and existing user files");
  std::filesystem::remove(staged);
  const auto saved = WriteRecipeFile(target, current, true);
  Check(saved.has_value(), "promotion retry succeeds");
  if (!saved) {
    return;
  }
  Check(current == original, "successful preparation does not mutate caller");
  Check(saved->metadata.imported.empty() && !saved->FindMask("scratch") &&
            !saved->FindMask("peek") && saved->FindMask("authored"),
        "saved document removes only import metadata and temporary masks");
  const auto *surface = Get<SurfaceOutput>(saved->outputs[0]);
  Check(
      surface && surface->stack.size() == 3 && !surface->stack[0].mask &&
          !surface->stack[1].mask && surface->stack[2].mask == Ref{"authored"},
      "scratch layer references are removed while authored references survive");
  const auto bytes = ReadText(target);
  Check(bytes && *bytes == SerializeRecipe(*saved),
        "returned saved document exactly describes the written bytes");
  const auto reloaded = ParseRecipe(bytes.value_or(""), saved->id);
  Check(reloaded.recipe && !reloaded.HasErrors() &&
            SerializeRecipe(*reloaded.recipe) == SerializeRecipe(*saved),
        "promoted document reopens without diagnostics or content drift");
  Check(ReadText(source).value_or("") == originalBytes,
        "successful promotion preserves the imported source file");
  Studio::EditHistory history;
  history.Push(current);
  current = *saved;
  Check(history.Undo(current) == original && history.Redo(original) == current,
        "save normalization can undo and redo as one complete document change");
  Check(ReadText(target) == bytes,
        "undo and redo do not silently rewrite the saved file");
}

void OrdinaryAndTransient(const std::filesystem::path &a_dir) {
  const Recipe original = Imported();
  const auto path = a_dir / "ordinary.json";
  const auto saved = WriteRecipeFile(path, original, false);
  Check(saved && saved->metadata.imported == original.metadata.imported,
        "ordinary save does not infer promotion from metadata alone");
  if (saved) {
    const auto repeated = WriteRecipeFile(path, *saved, false);
    Check(repeated && *repeated == *saved,
          "saving an already normalized document is idempotent");
  }
  Recipe paint = original;
  paint.id = std::string{Studio::kPaintRecipe};
  const auto before = ReadText(path);
  Check(!WriteRecipeFile(path, paint, true) && ReadText(path) == before,
        "paint draft refusal cannot overwrite an existing recipe file");
  const auto absent = a_dir / "absent" / "paint.json";
  Check(!WriteRecipeFile(absent, paint, false) &&
            !std::filesystem::exists(absent.parent_path()),
        "paint draft refusal creates no file or destination folder");
}
}

int main() {
  const auto dir = test::ScratchDir("recipe_save");
  SavePromotion(dir);
  OrdinaryAndTransient(dir);
  return test::Finish("recipe save");
}
