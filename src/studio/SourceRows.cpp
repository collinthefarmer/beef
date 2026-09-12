#include "studio/Rows.h"

#include "recipe/Words.h"
#include "studio/FieldParsing.h"

#include <format>
#include <limits>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] std::string OnOff(bool a_on) { return a_on ? "on" : "off"; }

[[nodiscard]] std::string PointText(const Vec3 &a_point) {
  return LiteralColorText(a_point);
}

}

SourceRow SourceRowOf(const Source &a_source, std::size_t a_references) {
  SourceRow row;
  row.name = a_source.name;
  row.kind = std::string{SourceKindName(a_source.kind)};
  row.references = a_references;
  Match(
      a_source.kind,
      [&](const ImageSource &a_image) {
        row.path = a_image.path;
        row.channel = std::string{ImageChannelName(a_image.channel)};
        row.space = std::string{ImageSpaceName(a_image.space)};
        row.scroll = a_image.scroll ? Vec2ParamText(*a_image.scroll) : "";
        row.tile = a_image.tile ? Vec2ParamText(*a_image.tile) : "";
        row.mirrorU = OnOff(a_image.mirror[0]);
        row.mirrorV = OnOff(a_image.mirror[1]);
        row.transpose = OnOff(a_image.transpose);
        row.mip = ParamText(a_image.mip);
      },
      [&](const MaterialSource &a_material) {
        row.material = std::string{MaterialChannelName(a_material.channel)};
      },
      [&](const BakeSource &a_bake) {
        row.bake = std::string{BakeKindName(a_bake.bake)};
        if (const PartitionBake *partition = Get<PartitionBake>(a_bake.bake)) {
          const auto name = BipedSlotName(partition->slot);
          row.partition =
              name ? std::string{*name} : std::to_string(partition->slot);
        }
        if (const BoneWeightBake *bones = Get<BoneWeightBake>(a_bake.bake)) {
          for (const auto &bone : bones->bones) {
            row.bones += (row.bones.empty() ? "" : ", ") + bone;
          }
        }
      },
      [&](const UvSource &a_uv) {
        row.axis = std::string{UvAxisName(a_uv.axis)};
      },
      [&](const DistanceSource &a_distance) {
        row.from = Match(
            a_distance.from, [](const std::string &a_node) { return a_node; },
            [](const Vec3 &a_point) { return PointText(a_point); });
      },
      [&](const RippleSource &a_ripple) {
        row.trigger = "@" + a_ripple.trigger.name;
        row.speed = ParamText(a_ripple.speed);
        row.width = ParamText(a_ripple.width);
        row.decay = ParamText(a_ripple.decay);
        row.shape = std::string{RippleShapeName(a_ripple.shape)};
      },
      [&](const MaterialClustersSource &a_clusters) {
        row.clusters = std::to_string(a_clusters.clusters);
        row.weights = std::format(
            "{}, {}, {}, {}, {}", ParamText(a_clusters.roughness),
            ParamText(a_clusters.metallic), ParamText(a_clusters.occlusion),
            ParamText(a_clusters.reflectance), ParamText(a_clusters.luma));
        row.seed = std::to_string(a_clusters.seed);
        row.iterations = std::to_string(a_clusters.iterations);
      });
  return row;
}

namespace {
[[nodiscard]] std::optional<SourceKind> ImageKindOf(const SourceRow &a_row) {
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

[[nodiscard]] std::optional<SourceKind> MaterialKindOf(const SourceRow &a_row) {
  const auto channel = ParseMaterialChannel(a_row.material);
  return channel ? std::optional<SourceKind>{MaterialSource{*channel}}
                 : std::nullopt;
}

[[nodiscard]] std::optional<SourceKind> BakeKindOf(const SourceRow &a_row) {
  auto bake = DefaultBakeKind(a_row.bake);
  if (!bake) {
    return std::nullopt;
  }
  if (PartitionBake *partition = Get<PartitionBake>(*bake)) {
    const auto slot = BipedSlotFromName(a_row.partition);
    if (!slot) {
      return std::nullopt;
    }
    partition->slot = *slot;
  }
  if (BoneWeightBake *bones = Get<BoneWeightBake>(*bake)) {
    bones->bones = SplitNames(a_row.bones);
  }
  return SourceKind{BakeSource{*bake}};
}

[[nodiscard]] std::optional<SourceKind> UvKindOf(const SourceRow &a_row) {
  const auto axis = ParseUvAxis(a_row.axis);
  return axis ? std::optional<SourceKind>{UvSource{*axis}} : std::nullopt;
}

[[nodiscard]] std::optional<SourceKind> DistanceKindOf(const SourceRow &a_row) {
  DistanceSource distance;
  if (const auto point = LiteralColor(a_row.from)) {
    distance.from = *point;
  } else {
    distance.from = a_row.from;
  }
  return SourceKind{distance};
}

[[nodiscard]] std::optional<SourceKind> RippleKindOf(const SourceRow &a_row) {
  RippleSource ripple;
  if (!a_row.trigger.starts_with('@') || a_row.trigger.size() < 2) {
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

[[nodiscard]] std::optional<SourceKind> ClustersKindOf(const SourceRow &a_row) {
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
  clusters.clusters = static_cast<std::uint8_t>(*count);
  clusters.roughness = (*weights)[0];
  clusters.metallic = (*weights)[1];
  clusters.occlusion = (*weights)[2];
  clusters.reflectance = (*weights)[3];
  clusters.luma = (*weights)[4];
  clusters.seed = *seed;
  clusters.iterations = *iterations;
  return SourceKind{clusters};
}
}

std::optional<SourceKind> SourceKindOf(const SourceRow &a_row) {
  if (a_row.kind == "image") {
    return ImageKindOf(a_row);
  }
  if (a_row.kind == "material") {
    return MaterialKindOf(a_row);
  }
  if (a_row.kind == "bake") {
    return BakeKindOf(a_row);
  }
  if (a_row.kind == "uv") {
    return UvKindOf(a_row);
  }
  if (a_row.kind == "distance") {
    return DistanceKindOf(a_row);
  }
  if (a_row.kind == "ripple") {
    return RippleKindOf(a_row);
  }
  if (a_row.kind == "materialClusters") {
    return ClustersKindOf(a_row);
  }
  return std::nullopt;
}

}
