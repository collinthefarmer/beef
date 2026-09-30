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
[[nodiscard]] BuiltTerm MaterialiseTerm(const MaskPreset &a_preset,
                                        const SourceCatalog &a_existing);
[[nodiscard]] BuiltTerm BuildTerm(const TermKind &a_kind,
                                  const MaskPresets &a_presets,
                                  const SourceCatalog &a_existing);

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
struct OfferGroupSpec {
  OfferGroup value;
  std::string_view name;
  std::string_view word;
  bool ofGeometry;
};
inline constexpr OfferGroupSpec kOfferGroups[]{
    {OfferGroup::kParts, "parts", "part", true},
    {OfferGroup::kCharts, "charts", "chart", true},
    {OfferGroup::kMaterials, "materials", "material", true},
    {OfferGroup::kBones, "bones", "bone", true},
    {OfferGroup::kPartitions, "partitions", "partition", true},
    {OfferGroup::kChannels, "channels", "channel", true},
    {OfferGroup::kPresets, "presets", "preset", false},
    {OfferGroup::kMasks, "masks", "mask", false},
    {OfferGroup::kSources, "sources", "source", false},
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
[[nodiscard]] bool OfferMatches(const TermOffer &a_offer,
                                std::string_view a_filter);
[[nodiscard]] std::vector<const TermOffer *>
OffersInGroup(std::span<const TermOffer> a_offers, OfferGroup a_group,
              std::string_view a_filter);
[[nodiscard]] std::string TermDetailOf(const Term &a_term,
                                       std::span<const TermOffer> a_offers);
}
