#include "studio/SourcePlan.h"

#include "studio/Names.h"
#include "studio/Rows.h"
#include "studio/Snapshot.h"

#include <utility>

namespace BetterEnchantmentEffects::Studio {
SourceCatalog SourceCatalogOf(const RecipeRow &a_recipe) {
  SourceCatalog existing;
  for (const SourceRow &source : a_recipe.sourceRows) {
    if (const auto kind = SourceKindOf(source)) {
      existing.sources.push_back(Source{source.name, *kind});
    }
  }
  existing.reservedNames = ReservedNames(NamesOf(a_recipe));
  return existing;
}

SourceCatalog SourceCatalogOf(const Recipe &a_recipe) {
  SourceCatalog existing;
  for (const Source &source : a_recipe.sources) {
    existing.sources.push_back(source);
    existing.reservedNames.push_back(source.name);
  }
  for (const Mask &mask : a_recipe.masks) {
    existing.reservedNames.push_back(mask.name);
  }
  for (const Signal &signal : a_recipe.signals) {
    existing.reservedNames.push_back(signal.name);
  }
  for (const Curve &curve : a_recipe.curves) {
    existing.reservedNames.push_back(curve.name);
  }
  return existing;
}

SourcePlanBuilder::SourcePlanBuilder(const SourceCatalog &a_existing)
    : available_(a_existing) {}

std::string SourcePlanBuilder::ReuseOrAdd(const std::string &a_wanted,
                                          const SourceKind &a_kind) {
  for (const auto &[name, kind] : available_.sources) {
    if (name == a_wanted && kind == a_kind) {
      return name;
    }
  }
  for (const auto &[name, kind] : available_.sources) {
    if (kind == a_kind) {
      return name;
    }
  }
  const std::string name = UniqueName(a_wanted, available_.reservedNames);
  available_.reservedNames.push_back(name);
  available_.sources.push_back(Source{name, a_kind});
  edits_.emplace_back(AddSource{name, a_kind});
  return name;
}

std::vector<RecipeEdit> SourcePlanBuilder::TakeEdits() && {
  return std::move(edits_);
}
}
