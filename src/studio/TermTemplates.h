// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/Forms.h"
#include "studio/Mask.h"
#include "studio/Presets.h"
#include "studio/Snapshot.h"
#include "studio/SourcePlan.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct BuiltTerm {
  std::vector<RecipeEdit> edits;
  std::string expression;
};
struct PlacedTerm {
  Term term;
  std::vector<RecipeEdit> edits;
};
inline constexpr std::string_view kAllGeometries = "all geometries";
[[nodiscard]] BuiltTerm MaterialiseTerm(const MaskPreset &a_preset,
                                        const SourceCatalog &a_existing);
[[nodiscard]] BuiltTerm
BuildTerm(const TermKind &a_kind, const MaskPresets &a_presets,
          const SourceCatalog &a_existing,
          const std::optional<std::string> &a_guard = std::nullopt);
[[nodiscard]] bool IsGeometrySpecific(const TermKind &a_kind) noexcept;
[[nodiscard]] PlacedTerm PlaceTerm(const TermKind &a_kind,
                                   const std::optional<std::string> &a_guard,
                                   const MaskPresets &a_presets,
                                   const SourceCatalog &a_existing,
                                   const GeometryRow &a_geometry);

[[nodiscard]] std::string TermLabel(std::string_view a_text,
                                    const MaskPresets &a_presets,
                                    const SourceCatalog &a_existing);
[[nodiscard]] std::string TermLabelOf(const TermKind &a_kind,
                                      const MaskPresets &a_presets,
                                      const GeometryRow &a_geometry);
[[nodiscard]] std::string ProposedMaskName(std::span<const Term> a_terms,
                                           std::string_view a_editing);

struct TermField {
  FormField field;
  std::function<std::optional<TermKind>(const std::string &a_text)> apply;
};
[[nodiscard]] std::vector<TermField> TermForm(const TermKind &a_kind,
                                              const MaskPresets &a_presets,
                                              const GeometryRow &a_geometry);

enum class OfferGroup {
  kParts,
  kCharts,
  kMaterials,
  kBones,
  kPartitions,
  kChannels,
  kPresets,
  kMasks,
  kSources,
};
enum class OfferScope {
  kRecipe,
  kSharedTerm,
  kGeometry,
};
struct OfferGroupSpec {
  OfferGroup value;
  std::string_view name;
  std::string_view word;
  OfferScope scope;
};
inline constexpr OfferGroupSpec kOfferGroups[]{
    {OfferGroup::kParts, "parts", "part", OfferScope::kGeometry},
    {OfferGroup::kCharts, "charts", "chart", OfferScope::kGeometry},
    {OfferGroup::kMaterials, "materials", "material", OfferScope::kSharedTerm},
    {OfferGroup::kBones, "bones", "bone", OfferScope::kSharedTerm},
    {OfferGroup::kPartitions, "partitions", "partition",
     OfferScope::kSharedTerm},
    {OfferGroup::kChannels, "channels", "channel", OfferScope::kRecipe},
    {OfferGroup::kPresets, "presets", "preset", OfferScope::kRecipe},
    {OfferGroup::kMasks, "masks", "mask", OfferScope::kRecipe},
    {OfferGroup::kSources, "sources", "source", OfferScope::kRecipe},
};
inline constexpr std::size_t kOfferGroupCount = 9;
static_assert(std::size(kOfferGroups) == kOfferGroupCount);

struct TermOffer {
  OfferGroup group = OfferGroup::kParts;
  std::string name;
  std::string detail;
  std::optional<std::string> unavailable;
  std::optional<float> coverage;
  TermKind kind;
  std::string geometry;
};
[[nodiscard]] std::vector<TermOffer> OffersOf(const MaskPresets &a_presets,
                                              const RecipeRow &a_recipe,
                                              const GeometryRow &a_geometry,
                                              std::string_view a_editing);
[[nodiscard]] std::vector<TermOffer>
OffersOfRecipe(const MaskPresets &a_presets, const RecipeRow &a_recipe,
               std::string_view a_editing);
[[nodiscard]] const GeometryRow *
ScopedGeometry(const RecipeRow &a_recipe,
               const std::optional<std::string> &a_scope);
[[nodiscard]] std::vector<TermOffer>
OffersOfScope(const MaskPresets &a_presets, const RecipeRow &a_recipe,
              const std::optional<std::string> &a_scope,
              std::string_view a_editing);
[[nodiscard]] std::optional<std::string> GuardOf(const TermOffer &a_offer);
[[nodiscard]] std::vector<std::string> ScopeChoices(const RecipeRow &a_recipe);
[[nodiscard]] std::string
ScopeChoiceOf(const RecipeRow &a_recipe,
              const std::optional<std::string> &a_scope);
[[nodiscard]] std::optional<std::string>
ScopeOfChoice(std::string_view a_choice);
[[nodiscard]] bool OfferMatches(const TermOffer &a_offer,
                                std::string_view a_filter);
[[nodiscard]] std::vector<const TermOffer *>
OffersInGroup(std::span<const TermOffer> a_offers, OfferGroup a_group,
              std::string_view a_filter);
[[nodiscard]] std::string TermDetailOf(const Term &a_term,
                                       std::span<const TermOffer> a_offers);
}
