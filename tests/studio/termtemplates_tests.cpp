// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/TermTemplates.h"
#include "test_support.h"

#include <algorithm>
#include <span>
#include <string_view>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
GeometryRow SampleGeometry() {
  GeometryRow geometry;
  geometry.name = "Body";
  geometry.meshRead = true;
  geometry.partitions.push_back(SlotCoverage{32, "body", 100});
  geometry.bones.push_back(BoneCoverage{"NPC Spine2 [Spn2]", 0.4f});
  MeshIsland island;
  island.source = IslandSource::kComponent;
  island.id = 0;
  island.share = 0.5f;
  island.dominantBone = "NPC Spine2 [Spn2]";
  island.dominantShare = 0.6f;
  geometry.islands.push_back(island);
  MaterialCluster cluster;
  cluster.id = 0;
  cluster.share = 0.3f;
  cluster.description = "polished metal";
  geometry.clusters.push_back(cluster);
  return geometry;
}

bool HasGroup(std::span<const TermOffer> a_offers, OfferGroup a_group) {
  return std::ranges::any_of(
      a_offers, [a_group](const TermOffer &o) { return o.group == a_group; });
}

const TermField *FieldNamed(std::span<const TermField> a_form,
                            std::string_view a_name) {
  const auto it = std::ranges::find(a_form, a_name, [](const TermField &f) {
    return std::string_view{f.field.name};
  });
  return it == a_form.end() ? nullptr : &*it;
}
}

namespace {
RecipeRow TwoGeometries() {
  RecipeRow recipe;
  recipe.geometries = {SampleGeometry(), SampleGeometry()};
  recipe.geometries.back().name = "Other";
  return recipe;
}

std::size_t AddedSources(std::span<const RecipeEdit> a_edits) {
  return static_cast<std::size_t>(
      std::ranges::count_if(a_edits, [](const RecipeEdit &a_edit) {
        return Get<AddSource>(a_edit) != nullptr;
      }));
}

void ScopedOffersAndGuards(const MaskPresets &a_presets) {
  const RecipeRow recipe = TwoGeometries();
  const auto everywhere = OffersOfScope(a_presets, recipe, std::nullopt, "");
  Check(!HasGroup(everywhere, OfferGroup::kParts) &&
            !HasGroup(everywhere, OfferGroup::kCharts) &&
            !HasGroup(everywhere, OfferGroup::kMaterials),
        "all geometries hides parts, charts and materials");
  Check(HasGroup(everywhere, OfferGroup::kBones) &&
            HasGroup(everywhere, OfferGroup::kPartitions) &&
            HasGroup(everywhere, OfferGroup::kChannels),
        "all geometries keeps the terms that mean the same everywhere");
  const auto onOther =
      OffersOfScope(a_presets, recipe, std::string{"Other"}, "");
  const auto parts =
      std::ranges::count_if(onOther, [](const TermOffer &a_offer) {
        return a_offer.group == OfferGroup::kParts;
      });
  Check(parts == 1 && std::ranges::all_of(onOther,
                                          [](const TermOffer &a_offer) {
                                            return a_offer.geometry.empty() ||
                                                   a_offer.geometry == "Other";
                                          }),
        "a geometry scope offers that geometry's parts once");
  Check(OffersOfScope(a_presets, recipe, std::string{"Missing"}, "").size() ==
            everywhere.size(),
        "an unknown scope offers what all geometries offers");

  const auto part = std::ranges::find_if(onOther, [](const TermOffer &a_offer) {
    return a_offer.group == OfferGroup::kParts;
  });
  const auto bone = std::ranges::find_if(onOther, [](const TermOffer &a_offer) {
    return a_offer.group == OfferGroup::kBones;
  });
  if (part == onOther.end() || bone == onOther.end()) {
    Check(false, "the scoped offers include a part and a bone");
    return;
  }
  Check(GuardOf(*part) == std::optional<std::string>{"Other"} &&
            !GuardOf(*bone),
        "only a geometry-specific offer carries a guard");
  const PlacedTerm placed =
      PlaceTerm(part->kind, GuardOf(*part), a_presets, SourceCatalog{},
                recipe.geometries.back());
  Check(placed.term.geometry == std::optional<std::string>{"Other"} &&
            placed.term.text == "(abs(@components * 255 - 0) < 0.5) * @Other",
        "a guarded part is multiplied by its geometry's bake");
  Check(AddedSources(placed.edits) == 2,
        "a guarded part adds its id bake and one geometry bake");
  Check(placed.term.label.ends_with(" on Other"),
        "a guarded term's label names its geometry");
  Check(BuildMask(std::span{&placed.term, 1}).contains("@Other"),
        "the built mask keeps the guard");

  SourceCatalog guarded;
  guarded.sources = {Source{"otherOnly", MeshSource{GeometryBake{"Other"}}}};
  guarded.reservedNames = {"otherOnly"};
  const BuiltTerm reused =
      BuildTerm(part->kind, a_presets, guarded, std::string{"Other"});
  Check(reused.expression.ends_with("* @otherOnly") &&
            AddedSources(reused.edits) == 1,
        "an existing geometry bake for the same name is reused");
  const BuiltTerm odd = BuildTerm(part->kind, a_presets, SourceCatalog{},
                                  std::string{"1st:Cuirass"});
  Check(odd.expression.ends_with("* @geometry1st_Cuirass"),
        "a geometry name becomes a valid source name");

  Check(ScopeChoices(recipe) ==
            std::vector<std::string>{std::string{kAllGeometries}, "Body",
                                     "Other"},
        "the scope choices are all geometries and each geometry name");
  Check(!ScopeOfChoice(kAllGeometries) &&
            ScopeOfChoice("Body") == std::optional<std::string>{"Body"},
        "a scope choice reads back as a geometry or none");
  Check(ScopeChoiceOf(recipe, std::string{"Missing"}) == kAllGeometries,
        "an unknown scope shows as all geometries");
}
}

int main() {
  Recipe recipe;
  recipe.sources.push_back(
      Source{"metal", MaterialSource{MaterialChannel::kMetallic}});
  recipe.masks.push_back(Mask{"scratch", "0"});
  const SourceCatalog existing = SourceCatalogOf(recipe);
  Check(existing.sources.size() == 1 && existing.reservedNames.size() == 2 &&
            existing.sources[0].name == "metal",
        "SourceCatalogOf reads sources and reserved names off a Recipe");

  const BuiltTerm threshold = BuildTerm(
      TermKind{ThresholdTerm{MaterialChannel::kRoughness, 0.5f, 1.0f}},
      MaskPresets{}, SourceCatalog{});
  Check(threshold.edits.size() == 1 &&
            threshold.expression.find("smoothstep") != std::string::npos &&
            threshold.expression.find("@roughness") != std::string::npos,
        "BuildTerm adds the source it names and builds a threshold expression");

  MaskPresets presets;
  presets.presets.push_back(MaskPreset{
      "leather",
      std::nullopt,
      {},
      "@r",
      {{"r", SourceKind{MaterialSource{MaterialChannel::kRoughness}}}}});
  const BuiltTerm materialised =
      MaterialiseTerm(presets.presets[0], SourceCatalog{});
  Check(materialised.edits.size() == 1 && materialised.expression == "@r",
        "MaterialiseTerm reuses the preset expression and names its source");
  const SourceKind roughness = MaterialSource{MaterialChannel::kRoughness};
  presets.presets.push_back(
      MaskPreset{"aliases",
                 std::nullopt,
                 {},
                 "@first + @second",
                 {{"first", roughness}, {"second", roughness}}});
  const BuiltTerm aliases =
      BuildTerm(PresetTerm{"aliases"}, presets, SourceCatalog{});
  Check(aliases.edits.size() == 1 && aliases.expression == "@first + @first",
        "preset aliases share one staged source and the expression uses its "
        "name");
  Check(TermLabel("@r", presets, SourceCatalog{}) == "r",
        "a bare @reference labels as its name; a preset carrying a source edit "
        "is not matched by expression alone");
  presets.presets.push_back(
      MaskPreset{"combo", std::nullopt, {}, "@metal * @r", {}});
  Check(TermLabel("@metal * @r", presets, SourceCatalog{}) == "combo",
        "TermLabel recognises a pure-expression preset by its expression");

  const GeometryRow geometry = SampleGeometry();
  const RecipeRow row;
  const auto offers = OffersOf(presets, row, geometry, "");
  Check(HasGroup(offers, OfferGroup::kParts) &&
            HasGroup(offers, OfferGroup::kMaterials) &&
            HasGroup(offers, OfferGroup::kBones) &&
            HasGroup(offers, OfferGroup::kPartitions) &&
            HasGroup(offers, OfferGroup::kChannels) &&
            HasGroup(offers, OfferGroup::kPresets),
        "OffersOf yields a term for each family of the geometry");

  const auto part = std::ranges::find_if(offers, [](const TermOffer &o) {
    return o.group == OfferGroup::kMaterials;
  });
  Check(part != offers.end() && part->coverage.has_value() &&
            part->detail == "polished metal" && part->geometry == "Body",
        "a material offer carries its cluster description, coverage and "
        "geometry");

  GeometryRow unread = geometry;
  unread.meshRead = false;
  const auto blocked = OffersOf(presets, row, unread, "");
  Check(std::ranges::any_of(blocked,
                            [](const TermOffer &o) {
                              return o.group == OfferGroup::kParts &&
                                     o.unavailable.has_value();
                            }),
        "an unread mesh reports parts as unavailable rather than crashing");

  const std::string label =
      TermLabelOf(TermKind{PartitionTerm{BipedSlot{32}}}, presets, geometry);
  Check(label == "body", "TermLabelOf names a partition from the mesh facts");

  const auto thresholdForm =
      TermForm(TermKind{ThresholdTerm{MaterialChannel::kRoughness, 0.5f, 1.0f}},
               presets, geometry);
  const TermField *softness = FieldNamed(thresholdForm, "softness");
  const TermField *posterize = FieldNamed(thresholdForm, "posterize");
  Check(softness && softness->field.workingRange && !softness->field.integral &&
            softness->field.workingRange->first == 0.0f &&
            softness->field.workingRange->second == 1.0f,
        "a continuous threshold field carries a tuning range and is not "
        "integral");
  Check(posterize && posterize->field.workingRange && posterize->field.integral,
        "a whole-number threshold field is tunable and integral");

  const auto clusterForm =
      TermForm(TermKind{ClusterTerm{ClusterSettings{}, 0}}, presets, geometry);
  const TermField *merge = FieldNamed(clusterForm, "merge");
  const TermField *smoothing = FieldNamed(clusterForm, "smoothing");
  Check(merge && merge->field.workingRange && !merge->field.integral,
        "the merge share is tunable and continuous");
  Check(smoothing && smoothing->field.workingRange && smoothing->field.integral,
        "the smoothing radius is tunable and integral");

  const GeometryRow guessed = geometry;
  const auto guessedOffers = OffersOf(presets, RecipeRow{}, guessed, "");
  const auto materialOffer =
      std::ranges::find_if(guessedOffers, [](const TermOffer &offer) {
        return offer.group == OfferGroup::kMaterials;
      });
  const auto *offeredCluster =
      materialOffer != guessedOffers.end()
          ? std::get_if<ClusterTerm>(&materialOffer->kind)
          : nullptr;
  Check(offeredCluster && offeredCluster->settings == ClusterSettings{},
        "a material offer starts from the default cluster settings");

  RecipeRow repeated;
  repeated.geometries = {geometry, geometry};
  repeated.geometries.back().name = "Other";
  const auto combined = OffersOfRecipe(presets, repeated, "");
  Check(std::ranges::count_if(combined,
                              [](const TermOffer &offer) {
                                return offer.group == OfferGroup::kMaterials;
                              }) == 1,
        "identical material terms are offered once across geometries");
  const auto roughnessOffer = [](const TermOffer &offer) {
    return offer.group == OfferGroup::kChannels && offer.name == "roughness";
  };
  Check(std::ranges::count_if(combined, roughnessOffer) == 1 &&
            std::ranges::find_if(combined, roughnessOffer)->geometry.empty(),
        "a channel threshold is offered once and belongs to no geometry");
  const auto groupCount = [&](OfferGroup a_group) {
    return std::ranges::count_if(combined, [&](const TermOffer &offer) {
      return offer.group == a_group;
    });
  };
  const auto singleCount = [&](OfferGroup a_group) {
    return std::ranges::count_if(
        OffersOf(presets, repeated, geometry, ""),
        [&](const TermOffer &offer) { return offer.group == a_group; });
  };
  Check(singleCount(OfferGroup::kBones) > 0 &&
            singleCount(OfferGroup::kPartitions) > 0 &&
            groupCount(OfferGroup::kBones) == singleCount(OfferGroup::kBones) &&
            groupCount(OfferGroup::kPartitions) ==
                singleCount(OfferGroup::kPartitions),
        "identical bone and partition terms are offered once across "
        "geometries");
  Check(singleCount(OfferGroup::kParts) > 0 &&
            groupCount(OfferGroup::kParts) ==
                2 * singleCount(OfferGroup::kParts),
        "a part is offered per geometry, because its id names a different "
        "area on each");
  repeated.geometries.back().clusters.front().description = "rough cloth";
  const auto varied = OffersOfRecipe(presets, repeated, "");
  const auto variedOffer =
      std::ranges::find_if(varied, [](const TermOffer &offer) {
        return offer.group == OfferGroup::kMaterials;
      });
  Check(variedOffer != varied.end() && variedOffer->geometry.empty() &&
            variedOffer->detail == "varies by geometry",
        "a shared cluster operation does not claim one geometry's material "
        "label");

  ScopedOffersAndGuards(presets);
  return test::Finish("studio_termtemplates");
}
