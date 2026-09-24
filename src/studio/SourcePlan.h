// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"
#include "studio/Edits.h"

#include <string>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct RecipeRow;

struct SourceCatalog {
  std::vector<Source> sources;
  std::vector<std::string> reservedNames;
};

class SourcePlanBuilder {
public:
  explicit SourcePlanBuilder(const SourceCatalog &a_existing);
  [[nodiscard]] std::string ReuseOrAdd(const std::string &a_wanted,
                                       const SourceKind &a_kind);
  [[nodiscard]] std::vector<RecipeEdit> TakeEdits() &&;

private:
  SourceCatalog available_;
  std::vector<RecipeEdit> edits_;
};

[[nodiscard]] SourceCatalog SourceCatalogOf(const RecipeRow &a_recipe);
[[nodiscard]] SourceCatalog SourceCatalogOf(const Recipe &a_recipe);
}
