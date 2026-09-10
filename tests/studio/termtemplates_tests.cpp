#include "studio/TermTemplates.h"
#include "test_support.h"

#include <algorithm>

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
}

int main() {
  Recipe recipe;
  recipe.sources.push_back(
      Source{"metal", MaterialSource{MaterialChannel::kMetallic}});
  recipe.masks.push_back(Mask{"scratch", "0"});
  const Existing existing = ExistingOf(recipe);
  Check(existing.sources.size() == 1 && existing.taken.size() == 2 &&
            existing.sources[0].first == "metal",
        "ExistingOf reads sources and taken names off a Recipe");

  const BuiltTerm threshold = BuildTerm(
      TermKind{ThresholdTerm{MaterialChannel::kRoughness, 0.5f, 1.0f}},
      MaskPresets{}, Existing{});
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
      MaterialiseTerm(presets.presets[0], Existing{});
  Check(materialised.edits.size() == 1 && materialised.expression == "@r",
        "MaterialiseTerm reuses the preset expression and names its source");
  Check(TermLabel("@r", presets, Existing{}) == "leather",
        "TermLabel recognises a materialised preset by its expression");

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
      TermLabelOf(TermKind{PartitionTerm{32}}, presets, geometry);
  Check(label == "body", "TermLabelOf names a partition from the mesh facts");

  return test::Finish("studio_termtemplates");
}
