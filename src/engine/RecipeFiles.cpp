// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RecipeFiles.h"

#include "engine/TextFile.h"
#include "studio/PaintSession.h"

#include <algorithm>
#include <format>
#include <system_error>

namespace BetterEnchantmentEffects {
namespace {
Diagnostic FileRefusal(std::string_view a_id,
                       const std::filesystem::path &a_path,
                       std::string_view a_problem) {
  return MakeDiagnostic(Severity::kError, std::format("recipe {}", a_id),
                        std::format("{}: {}", a_path.string(), a_problem));
}

std::optional<Diagnostic> CheckFile(const std::filesystem::path &a_path,
                                    std::string_view a_id, bool &a_exists) {
  std::error_code ec;
  const auto status = std::filesystem::symlink_status(a_path, ec);
  if (ec == std::errc::no_such_file_or_directory) {
    a_exists = false;
    return std::nullopt;
  }
  if (ec) {
    return FileRefusal(a_id, a_path, ec.message());
  }
  a_exists = std::filesystem::exists(status);
  if (a_exists && !std::filesystem::is_regular_file(status)) {
    return FileRefusal(a_id, a_path, "not a regular recipe file");
  }
  return std::nullopt;
}
}

std::expected<LoadResult, Diagnostic>
ReadRecipeFile(const std::filesystem::path &a_path, std::string_view a_id) {
  const auto text = ReadText(a_path);
  if (!text) {
    return std::unexpected(FileRefusal(a_id, a_path, text.error()));
  }
  LoadResult result = ParseRecipe(*text, a_id);
  if (!result.recipe) {
    return std::unexpected(FileRefusal(
        a_id, a_path,
        result.diagnostics.empty() ? "recipe is unreadable"
                                   : result.diagnostics.front().message));
  }
  return result;
}

std::expected<Recipe, Diagnostic>
WriteRecipeFile(const std::filesystem::path &a_path, const Recipe &a_recipe,
                bool a_promoteImported) {
  if (a_recipe.id == Studio::kPaintRecipe) {
    return std::unexpected(
        FileRefusal(a_recipe.id, a_path, "the paint recipe is never written"));
  }
  Recipe written = a_recipe;
  if (a_promoteImported) {
    written.metadata.imported.clear();
  }
  const auto isPaintMask = [](std::string_view a_name) {
    return a_name == Studio::kScratchMask || a_name == Studio::kPeekMask;
  };
  std::erase_if(written.masks,
                [&](const Mask &a_mask) { return isPaintMask(a_mask.name); });
  for (Output &output : written.outputs) {
    SurfaceOutput *surface = Get<SurfaceOutput>(output);
    if (!surface) {
      continue;
    }
    for (Layer &layer : surface->stack) {
      if (layer.mask && isPaintMask(layer.mask->name)) {
        layer.mask.reset();
      }
    }
  }
  if (!WriteText(a_path, SerializeRecipe(written))) {
    return std::unexpected(
        FileRefusal(a_recipe.id, a_path, "could not write recipe"));
  }
  return written;
}

std::optional<Diagnostic> RenameRecipeFile(const std::filesystem::path &a_from,
                                           const std::filesystem::path &a_to,
                                           std::string_view a_id,
                                           bool a_owned) {
  bool destinationExists = false;
  if (auto problem = CheckFile(a_to, a_id, destinationExists)) {
    return problem;
  }
  if (destinationExists) {
    return FileRefusal(a_id, a_to,
                       "a file already exists; choose another name");
  }
  if (!a_owned) {
    return std::nullopt;
  }
  bool sourceExists = false;
  if (auto problem = CheckFile(a_from, a_id, sourceExists)) {
    return problem;
  }
  if (!sourceExists) {
    return std::nullopt;
  }
  std::error_code ec;
  std::filesystem::rename(a_from, a_to, ec);
  if (ec) {
    return FileRefusal(
        a_id, a_from,
        std::format("could not move to {} ({})", a_to.string(), ec.message()));
  }
  return std::nullopt;
}

std::optional<Diagnostic> DeleteRecipeFile(const std::filesystem::path &a_path,
                                           std::string_view a_id,
                                           bool a_owned) {
  if (!a_owned) {
    return std::nullopt;
  }
  bool exists = false;
  if (auto problem = CheckFile(a_path, a_id, exists)) {
    return problem;
  }
  if (!exists) {
    return std::nullopt;
  }
  std::error_code ec;
  std::filesystem::remove(a_path, ec);
  if (ec) {
    return FileRefusal(a_id, a_path,
                       std::format("could not remove ({})", ec.message()));
  }
  return std::nullopt;
}
}
