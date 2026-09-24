// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Recipe.h"

#include <cstdio>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

namespace {
using namespace BetterEnchantmentEffects;

[[nodiscard]] std::optional<std::string>
ReadText(const std::filesystem::path &a_path) {
  std::ifstream in(a_path, std::ios::binary);
  if (!in) {
    return std::nullopt;
  }
  std::ostringstream text;
  text << in.rdbuf();
  if (in.bad()) {
    return std::nullopt;
  }
  return std::move(text).str();
}

[[nodiscard]] const char *SeverityWord(Severity a_severity) {
  return a_severity == Severity::kError ? "error" : "warning";
}

[[nodiscard]] std::string Summary(const Recipe &a_recipe) {
  std::string keys;
  for (const RecipeKey &key : a_recipe.keys) {
    keys += (keys.empty() ? "" : ", ") + key.ToString();
  }
  return std::format(
      "keys: {}; {} signals, {} curves, {} sources, {} masks, {} outputs", keys,
      a_recipe.signals.size(), a_recipe.curves.size(), a_recipe.sources.size(),
      a_recipe.masks.size(), a_recipe.outputs.size());
}

[[nodiscard]] bool ValidateFile(const std::filesystem::path &a_path) {
  const std::string id = a_path.stem().string();
  const std::optional<std::string> text = ReadText(a_path);
  if (!text) {
    std::printf("%s: unreadable\n", a_path.string().c_str());
    return false;
  }
  const LoadResult result = ParseRecipe(*text, id);
  for (const Diagnostic &diagnostic : result.diagnostics) {
    std::printf("%s: %s %s: %s\n", id.c_str(),
                SeverityWord(diagnostic.severity), diagnostic.where.c_str(),
                diagnostic.message.c_str());
  }
  if (!result.recipe) {
    std::printf("%s: unreadable (not a recipe this format reads)\n",
                id.c_str());
    return false;
  }
  if (result.HasRecipeErrors()) {
    std::printf("%s: held back until its recipe-level errors are fixed (%s)\n",
                id.c_str(), Summary(*result.recipe).c_str());
    return false;
  }
  if (result.HasErrors()) {
    std::printf("%s: loads; rows with errors are inert (%s)\n", id.c_str(),
                Summary(*result.recipe).c_str());
    return false;
  }
  std::printf("%s: ok (%s)\n", id.c_str(), Summary(*result.recipe).c_str());
  return true;
}
}

int main(int a_argc, char **a_argv) try {
  if (a_argc < 2) {
    std::printf(
        "usage: beef-validate <recipe.json>...\n"
        "Reads each recipe with the plugin's own parser and prints its\n"
        "diagnostics, each naming the row it belongs to. Exit 0 means every\n"
        "file loads with no errors. Editor IDs are resolved in game, not\n"
        "here; a name this tool accepts can still fail to resolve there.\n");
    return 2;
  }
  bool ok = true;
  for (int i = 1; i < a_argc; ++i) {
    ok = ValidateFile(a_argv[i]) && ok;
  }
  return ok ? 0 : 1;
} catch (const std::exception &error) {
  std::printf("error: %s\n", error.what());
  return 2;
} catch (...) {
  std::printf("error: unexpected failure\n");
  return 2;
}
