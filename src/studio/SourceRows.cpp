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
  Match(
      a_image.texture,
      [&](const ImageFile &a_file) { image.path = a_file.path; },
      [&](const EffectShaderImage &a_shader) {
        image.effectShader =
            std::string{NameOf(kEffectShaderTextures, a_shader.texture)};
      });
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

[[nodiscard]] MeshSourceRow BakeRowOf(const MeshSource &a_bake) {
  MeshSourceRow bake;
  bake.bake = std::string{BakeKindName(a_bake.bake)};
  if (const PartitionBake *partition = Get<PartitionBake>(a_bake.bake)) {
    bake.partition = PartitionText(partition->bipedSlot);
  }
  if (const BoneWeightBake *bones = Get<BoneWeightBake>(a_bake.bake)) {
    for (const auto &bone : bones->bones) {
      bake.bones += (bake.bones.empty() ? "" : ", ") + bone;
    }
  }
  if (const GeometryBake *geometry = Get<GeometryBake>(a_bake.bake)) {
    bake.geometry = geometry->geometry;
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
  ripple.direction = Vec3ParamText(a_ripple.direction);
  return ripple;
}

[[nodiscard]] MaterialGroupsSourceRow
ClustersRowOf(const MaterialGroupsSource &a_clusters) {
  const ClusterSettings &s = a_clusters.settings;
  MaterialGroupsSourceRow clusters;
  clusters.merge = ParamText(s.merge);
  clusters.smoothing = std::to_string(s.smoothing);
  clusters.pick = a_clusters.pick ? ParamText(*a_clusters.pick) : "";
  clusters.areaBias = ParamText(a_clusters.areaBias);
  clusters.palette = a_clusters.palette;
  return clusters;
}

[[nodiscard]] PickSourceRow PickRowOf(const PickSource &a_pick) {
  PickSourceRow pick;
  pick.seed = ParamText(a_pick.seed);
  const PickLevels levels = SupportedLevels(a_pick).value_or(PickLevels{});
  if (levels.geometry) {
    pick.geometryLevel = true;
    pick.geometries = ParamText(levels.geometry->count);
  }
  if (levels.groups) {
    pick.areaBias = ParamText(levels.groups->areaBias);
    pick.merge = ParamText(levels.groups->settings.merge);
    pick.smoothing = std::to_string(levels.groups->settings.smoothing);
  }
  if (levels.chart) {
    pick.chartLevel = true;
    pick.charts = ParamText(levels.chart->count);
    pick.minCover = ParamText(levels.chart->minCover);
    pick.minShare = ParamText(levels.chart->minShare);
  }
  return pick;
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
      [](const MeshSource &a_bake) -> SourceRowKind {
        return BakeRowOf(a_bake);
      },
      [](const DistanceSource &a_distance) -> SourceRowKind {
        return DistanceRowOf(a_distance);
      },
      [](const RippleSource &a_ripple) -> SourceRowKind {
        return RippleRowOf(a_ripple);
      },
      [](const MaterialGroupsSource &a_clusters) -> SourceRowKind {
        return ClustersRowOf(a_clusters);
      },
      [](const PickSource &a_pick) -> SourceRowKind {
        return PickRowOf(a_pick);
      });
  return row;
}

namespace {
[[nodiscard]] std::optional<ImageTexture>
ImageTextureOf(const ImageSourceRow &a_row);

[[nodiscard]] std::optional<SourceKind>
ImageKindOf(const ImageSourceRow &a_row) {
  ImageSource image;
  const std::optional<ImageTexture> texture = ImageTextureOf(a_row);
  if (!texture) {
    return std::nullopt;
  }
  image.texture = *texture;
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

[[nodiscard]] std::optional<ImageTexture>
ImageTextureOf(const ImageSourceRow &a_row) {
  if (a_row.effectShader.empty()) {
    return ImageTexture{ImageFile{a_row.path}};
  }
  const auto texture = FromName(kEffectShaderTextures, a_row.effectShader);
  if (!texture) {
    return std::nullopt;
  }
  return ImageTexture{EffectShaderImage{*texture}};
}

[[nodiscard]] std::optional<SourceKind>
MaterialKindOf(const MaterialSourceRow &a_row) {
  const auto channel = ParseMaterialChannel(a_row.material);
  return channel ? std::optional<SourceKind>{MaterialSource{*channel}}
                 : std::nullopt;
}

[[nodiscard]] std::optional<SourceKind> BakeKindOf(const MeshSourceRow &a_row) {
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
  if (GeometryBake *geometry = Get<GeometryBake>(*bake)) {
    geometry->geometry = a_row.geometry;
  }
  return SourceKind{MeshSource{*bake}};
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
  const auto direction = ParseVec3Param(a_row.direction);
  if (!speed || !width || !decay || !shape || !direction) {
    return std::nullopt;
  }
  ripple.speed = *speed;
  ripple.width = *width;
  ripple.decay = *decay;
  ripple.shape = *shape;
  ripple.direction = *direction;
  return SourceKind{ripple};
}

[[nodiscard]] std::optional<SourceKind>
ClustersKindOf(const MaterialGroupsSourceRow &a_row) {
  MaterialGroupsSource clusters;
  const auto merge = ParseParam(a_row.merge);
  const auto *mergeValue = merge ? Get<float>(*merge) : nullptr;
  const auto smoothing = WholeNumber(a_row.smoothing, kMaxClusterSmoothing);
  if (!mergeValue || !(*mergeValue >= 0.0f) || *mergeValue > 1.0f ||
      !smoothing) {
    return std::nullopt;
  }
  clusters.settings.merge = *mergeValue;
  clusters.settings.smoothing = *smoothing;
  clusters.palette = a_row.palette;
  const auto bias = ParseParam(a_row.areaBias);
  const auto *biasValue = bias ? Get<float>(*bias) : nullptr;
  if (!bias ||
      (biasValue && (*biasValue < 0.0f || *biasValue > kMaxAreaBias))) {
    return std::nullopt;
  }
  clusters.areaBias = *bias;
  if (!a_row.pick.empty()) {
    const auto pick = ParseParam(a_row.pick);
    const auto *pickValue = pick ? Get<float>(*pick) : nullptr;
    if (!pick || (pickValue && (*pickValue < 0.0f || *pickValue > 1.0f))) {
      return std::nullopt;
    }
    clusters.pick = *pick;
  }
  return SourceKind{clusters};
}

[[nodiscard]] std::optional<float> UnitNumber(const std::string &a_text) {
  const auto parsed = ParseParam(a_text);
  const auto *value = parsed ? Get<float>(*parsed) : nullptr;
  if (!value || !(*value >= 0.0f) || *value > 1.0f) {
    return std::nullopt;
  }
  return *value;
}

[[nodiscard]] std::optional<SourceKind> PickKindOf(const PickSourceRow &a_row) {
  const auto seed = ParseParam(a_row.seed);
  const auto geometries = ParseParam(a_row.geometries);
  const auto areaBias = ParseParam(a_row.areaBias);
  const auto merge = UnitNumber(a_row.merge);
  const auto smoothing = WholeNumber(a_row.smoothing, kMaxClusterSmoothing);
  const auto charts = ParseParam(a_row.charts);
  const auto minCover = UnitNumber(a_row.minCover);
  const auto minShare = ParseParam(a_row.minShare);
  if (!seed || !geometries || !areaBias || !merge || !smoothing || !charts ||
      !minCover || !minShare) {
    return std::nullopt;
  }
  PickSource pick;
  pick.seed = *seed;
  pick.levels.clear();
  if (a_row.geometryLevel) {
    pick.levels.push_back(GeometryLevel{*geometries});
  }
  pick.levels.push_back(GroupLevel{*areaBias, {*merge, *smoothing}});
  if (a_row.chartLevel) {
    pick.levels.push_back(ChartLevel{*charts, *minCover, *minShare});
  }
  return SourceKind{pick};
}
}

std::optional<SourceKind> SourceKindOf(const SourceRow &a_row) {
  return Match(
      a_row.kind,
      [](const ImageSourceRow &a_row) { return ImageKindOf(a_row); },
      [](const MaterialSourceRow &a_row) { return MaterialKindOf(a_row); },
      [](const MeshSourceRow &a_row) { return BakeKindOf(a_row); },
      [](const DistanceSourceRow &a_row) { return DistanceKindOf(a_row); },
      [](const RippleSourceRow &a_row) { return RippleKindOf(a_row); },
      [](const MaterialGroupsSourceRow &a_row) {
        return ClustersKindOf(a_row);
      },
      [](const PickSourceRow &a_row) { return PickKindOf(a_row); });
}

}
