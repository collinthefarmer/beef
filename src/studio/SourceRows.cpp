// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Rows.h"

#include "recipe/Words.h"
#include "studio/FieldParsing.h"

#include <format>
#include <limits>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] std::string OnOff(bool a_on) { return a_on ? "on" : "off"; }

[[nodiscard]] std::string PointText(const Vec3 &a_point) {
  return LiteralColorText(a_point);
}

[[nodiscard]] std::string PartitionText(BipedSlot a_slot) {
  const auto name = BipedSlotName(a_slot);
  return name ? std::string{*name} : std::to_string(std::to_underlying(a_slot));
}

[[nodiscard]] ImageSourceRow ImageRowOf(const ImageSource &a_image) {
  ImageSourceRow image;
  image.path = a_image.path;
  image.channel = std::string{ImageChannelName(a_image.channel)};
  image.space = std::string{ImageSpaceName(a_image.space)};
  image.scroll = a_image.scroll ? Vec2ParamText(*a_image.scroll) : "";
  image.tile = a_image.tile ? Vec2ParamText(*a_image.tile) : "";
  image.mirrorU = OnOff(a_image.mirror[0]);
  image.mirrorV = OnOff(a_image.mirror[1]);
  image.transpose = OnOff(a_image.transpose);
  image.mip = ParamText(a_image.mip);
  return image;
}

[[nodiscard]] MaterialSourceRow
MaterialRowOf(const MaterialSource &a_material) {
  return MaterialSourceRow{
      std::string{MaterialChannelName(a_material.channel)}};
}

[[nodiscard]] BakeSourceRow BakeRowOf(const BakeSource &a_bake) {
  BakeSourceRow bake;
  bake.bake = std::string{BakeKindName(a_bake.bake)};
  if (const PartitionBake *partition = Get<PartitionBake>(a_bake.bake)) {
    bake.partition = PartitionText(partition->bipedSlot);
  }
  if (const BoneWeightBake *bones = Get<BoneWeightBake>(a_bake.bake)) {
    for (const auto &bone : bones->bones) {
      bake.bones += (bake.bones.empty() ? "" : ", ") + bone;
    }
  }
  return bake;
}

[[nodiscard]] DistanceSourceRow
DistanceRowOf(const DistanceSource &a_distance) {
  return DistanceSourceRow{a_distance.from};
}

[[nodiscard]] RippleSourceRow RippleRowOf(const RippleSource &a_ripple) {
  RippleSourceRow ripple;
  ripple.trigger = "@" + a_ripple.trigger.name;
  ripple.speed = ParamText(a_ripple.speed);
  ripple.width = ParamText(a_ripple.width);
  ripple.decay = ParamText(a_ripple.decay);
  ripple.shape = std::string{RippleShapeName(a_ripple.shape)};
  return ripple;
}

[[nodiscard]] MaterialClustersSourceRow
ClustersRowOf(const MaterialClustersSource &a_clusters) {
  const ClusterSettings &s = a_clusters.settings;
  MaterialClustersSourceRow clusters;
  clusters.clusters = std::to_string(s.clusters);
  clusters.weights =
      std::format("{}, {}, {}, {}, {}", ParamText(s.weights.roughness),
                  ParamText(s.weights.metallic), ParamText(s.weights.occlusion),
                  ParamText(s.weights.reflectance), ParamText(s.weights.luma));
  clusters.seed = std::to_string(s.seed);
  clusters.iterations = std::to_string(s.iterations);
  return clusters;
}

}

SourceRow SourceRowOf(const Source &a_source, std::size_t a_references) {
  SourceRow row;
  row.type = SourceType(a_source);
  row.name = a_source.name;
  row.references = a_references;
  row.kind = Match(
      a_source.kind,
      [](const ImageSource &a_image) -> SourceRowKind {
        return ImageRowOf(a_image);
      },
      [](const MaterialSource &a_material) -> SourceRowKind {
        return MaterialRowOf(a_material);
      },
      [](const BakeSource &a_bake) -> SourceRowKind {
        return BakeRowOf(a_bake);
      },
      [](const DistanceSource &a_distance) -> SourceRowKind {
        return DistanceRowOf(a_distance);
      },
      [](const RippleSource &a_ripple) -> SourceRowKind {
        return RippleRowOf(a_ripple);
      },
      [](const MaterialClustersSource &a_clusters) -> SourceRowKind {
        return ClustersRowOf(a_clusters);
      });
  return row;
}

namespace {
[[nodiscard]] std::optional<SourceKind>
ImageKindOf(const ImageSourceRow &a_row) {
  ImageSource image;
  image.path = a_row.path;
  const auto channel = ParseImageChannel(a_row.channel);
  const auto space = ParseImageSpace(a_row.space);
  const auto mip = ParseParam(a_row.mip);
  const float *mipValue = mip ? Get<float>(*mip) : nullptr;
  if (!channel || !space || mipValue == nullptr) {
    return std::nullopt;
  }
  image.channel = *channel;
  image.space = *space;
  image.mip = *mipValue;
  if (!a_row.scroll.empty()) {
    const auto scroll = ParseVec2Param(a_row.scroll);
    if (!scroll) {
      return std::nullopt;
    }
    image.scroll = *scroll;
  }
  if (!a_row.tile.empty()) {
    const auto tile = ParseVec2Param(a_row.tile);
    if (!tile) {
      return std::nullopt;
    }
    image.tile = *tile;
  }
  image.mirror = {a_row.mirrorU == "on", a_row.mirrorV == "on"};
  image.transpose = a_row.transpose == "on";
  return SourceKind{image};
}

[[nodiscard]] std::optional<SourceKind>
MaterialKindOf(const MaterialSourceRow &a_row) {
  const auto channel = ParseMaterialChannel(a_row.material);
  return channel ? std::optional<SourceKind>{MaterialSource{*channel}}
                 : std::nullopt;
}

[[nodiscard]] std::optional<SourceKind> BakeKindOf(const BakeSourceRow &a_row) {
  auto bake = DefaultBakeKind(a_row.bake);
  if (!bake) {
    return std::nullopt;
  }
  if (PartitionBake *partition = Get<PartitionBake>(*bake)) {
    const auto slot = BipedSlotFromName(a_row.partition);
    if (!slot) {
      return std::nullopt;
    }
    partition->bipedSlot = *slot;
  }
  if (BoneWeightBake *bones = Get<BoneWeightBake>(*bake)) {
    bones->bones = SplitNames(a_row.bones);
  }
  return SourceKind{BakeSource{*bake}};
}

[[nodiscard]] std::optional<SourceKind>
DistanceKindOf(const DistanceSourceRow &a_row) {
  return SourceKind{DistanceSource{a_row.from}};
}

[[nodiscard]] std::optional<SourceKind>
RippleKindOf(const RippleSourceRow &a_row) {
  RippleSource ripple;
  if (!a_row.trigger.starts_with('@')) {
    return std::nullopt;
  }
  ripple.trigger = Ref{a_row.trigger.substr(1)};
  const auto speed = ParseParam(a_row.speed);
  const auto width = ParseParam(a_row.width);
  const auto decay = ParseParam(a_row.decay);
  const auto shape = ParseRippleShape(a_row.shape);
  if (!speed || !width || !decay || !shape) {
    return std::nullopt;
  }
  ripple.speed = *speed;
  ripple.width = *width;
  ripple.decay = *decay;
  ripple.shape = *shape;
  return SourceKind{ripple};
}

[[nodiscard]] std::optional<SourceKind>
ClustersKindOf(const MaterialClustersSourceRow &a_row) {
  MaterialClustersSource clusters;
  const auto count = WholeNumber(a_row.clusters, kMaxMaterialClusters);
  const auto weights = FiveNumbers(a_row.weights);
  const auto seed =
      WholeNumber(a_row.seed, std::numeric_limits<std::uint32_t>::max());
  const auto iterations = WholeNumber(a_row.iterations, kMaxClusterIterations);
  if (!count || *count < 1 || !weights || !seed || !iterations ||
      *iterations < 1) {
    return std::nullopt;
  }
  clusters.settings.clusters = static_cast<std::uint8_t>(*count);
  clusters.settings.weights =
      ChannelWeights{(*weights)[0], (*weights)[1], (*weights)[2], (*weights)[3],
                     (*weights)[4]};
  clusters.settings.seed = *seed;
  clusters.settings.iterations = *iterations;
  return SourceKind{clusters};
}
}

std::optional<SourceKind> SourceKindOf(const SourceRow &a_row) {
  return Match(
      a_row.kind,
      [](const ImageSourceRow &a_row) { return ImageKindOf(a_row); },
      [](const MaterialSourceRow &a_row) { return MaterialKindOf(a_row); },
      [](const BakeSourceRow &a_row) { return BakeKindOf(a_row); },
      [](const DistanceSourceRow &a_row) { return DistanceKindOf(a_row); },
      [](const RippleSourceRow &a_row) { return RippleKindOf(a_row); },
      [](const MaterialClustersSourceRow &a_row) {
        return ClustersKindOf(a_row);
      });
}

}
