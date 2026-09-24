#include "engine/RecipeFiles.h"
#include "engine/TextFile.h"
#include "test_support.h"

#include <filesystem>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

int main() {
  const auto dir = test::ScratchDir("engine_recipefiles");
  const auto from = dir / "original.json";
  const auto to = dir / "renamed.json";
  Check(WriteText(from, "original"), "create original");
  Check(WriteText(to, "other"), "create unloaded destination");
  const auto collision = RenameRecipeFile(from, to, "original", true);
  Check(collision && collision->where == "recipe original" &&
            collision->message.contains(to.string()),
        "rename refuses an existing destination with an actionable diagnostic");
  Equal(ReadText(from).value_or(""), std::string{"original"},
        "collision preserves original");
  Equal(ReadText(to).value_or(""), std::string{"other"},
        "collision preserves destination");
  std::filesystem::remove(to);
  Check(RenameRecipeFile(from, dir / "missing" / "new.json", "original", true)
            .has_value(),
        "failed filesystem move is refused");
  Equal(ReadText(from).value_or(""), std::string{"original"},
        "failed move preserves original");
  Check(!RenameRecipeFile(from, to, "original", false),
        "shipped rename leaves the original for a later user save");
  Check(std::filesystem::exists(from) && !std::filesystem::exists(to),
        "shipped rename does not move the original");
  Check(!DeleteRecipeFile(from, "original", false) &&
            std::filesystem::exists(from),
        "shipped delete preserves file");
  const auto blocked = dir / "blocked";
  std::filesystem::create_directory(blocked);
  const auto protectedFile = blocked / "kept.json";
  Check(WriteText(protectedFile, "kept"), "create protected file");
  std::filesystem::permissions(blocked, std::filesystem::perms::owner_read |
                                            std::filesystem::perms::owner_exec);
  const auto denied = DeleteRecipeFile(protectedFile, "kept", true);
  std::filesystem::permissions(blocked, std::filesystem::perms::owner_all);
  Check(denied && denied->where == "recipe kept" &&
            denied->message.contains(protectedFile.string()),
        "remove permission failure reaches caller with file and recipe");
  Equal(ReadText(protectedFile).value_or(""), std::string{"kept"},
        "refused delete preserves bytes");
  Check(DeleteRecipeFile(blocked, "blocked", true).has_value(),
        "a directory is never deleted as a recipe file");
  Check(RenameRecipeFile(from, protectedFile / "child.json", "original", true)
            .has_value(),
        "destination inspection failure is refused");
  Check(
      DeleteRecipeFile(protectedFile / "child.json", "child", true).has_value(),
      "source inspection failure is refused");
  Check(!RenameRecipeFile(from, to, "original", true), "user rename succeeds");
  Equal(ReadText(to).value_or(""), std::string{"original"},
        "rename preserves file contents");
  Check(!std::filesystem::exists(from), "rename removes old path");
  Check(!DeleteRecipeFile(to, "renamed", true), "user delete succeeds");
  Check(!std::filesystem::exists(to), "delete removes file");
  Check(!DeleteRecipeFile(to, "renamed", true),
        "deleting an unsaved or already absent file succeeds");
  Check(!RenameRecipeFile(from, to, "original", true),
        "an unsaved document can be renamed");
  return test::Finish("engine recipe files");
}
