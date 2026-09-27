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
  const TermField *clusters = FieldNamed(clusterForm, "clusters");
  const TermField *roughnessWeight = FieldNamed(clusterForm, "roughness");
  const TermField *seed = FieldNamed(clusterForm, "seed");
  Check(clusters && clusters->field.integral && clusters->field.workingRange,
        "the cluster count is tunable and integral");
  Check(roughnessWeight && roughnessWeight->field.workingRange &&
            !roughnessWeight->field.integral,
        "a cluster weight is tunable and continuous");
  Check(seed && !seed->field.range && !seed->field.workingRange,
        "the seed has no tuning range and stays a plain entry");

  RecipeRow repeated;
  repeated.geometries = {geometry, geometry};
  repeated.geometries.back().name = "Other";
  const auto combined = OffersOfRecipe(presets, repeated, "");
  Check(std::ranges::count_if(combined,
                              [](const TermOffer &offer) {
                                return offer.group == OfferGroup::kMaterials;
                              }) == 1,
        "identical material terms are offered once across geometries");
  repeated.geometries.back().clusters.front().description = "rough cloth";
  const auto varied = OffersOfRecipe(presets, repeated, "");
  const auto materialOffer =
      std::ranges::find_if(varied, [](const TermOffer &offer) {
        return offer.group == OfferGroup::kMaterials;
      });
  Check(materialOffer != varied.end() && materialOffer->geometry.empty() &&
            materialOffer->detail == "appearance varies by geometry",
        "a shared cluster operation does not claim one geometry's material "
        "label");

  return test::Finish("studio_termtemplates");
}
