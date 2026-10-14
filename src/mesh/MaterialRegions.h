// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/GaussianMixture.h"
#include "mesh/Islands.h"
#include "mesh/MaterialClusters.h"
#include "mesh/Mesh.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::uint16_t kNoChart = 0xFFFF;
inline constexpr std::uint32_t kMinRasterSide = 8;
inline constexpr std::uint32_t kMaxRasterSide = 1024;
inline constexpr std::int32_t kNoRegion = -1;
inline constexpr std::int16_t kNoGroup = -1;
inline constexpr std::size_t kTexelAxes = 8;
inline constexpr std::size_t kMinCoveredTexels = 64;
inline constexpr std::size_t kMaxGroups = kMaxMixtureComponents;
inline constexpr std::size_t kWhiteningFloats =
    kTexelAxes * (kTexelAxes + 1) / 2;

struct MaterialRaster {
  std::uint32_t side = 0;
  std::vector<MaterialTexel> texels;
  std::vector<std::uint16_t> charts;
};

struct SegmentWeights {
  float luma = 1.0f;
  float chroma = 0.75f;
  float roughness = 1.7f;
  float metallic = 3.0f;
  float occlusion = 1.4f;
  float reflectance = 0.4f;
  float detail = 0.1f;
};

struct SegmentSettings {
  std::uint32_t segments = 650;
  float compactness = 10.0f;
  SegmentWeights weights;
};

struct Regions {
  std::uint32_t side = 0;
  std::vector<std::int32_t> ofTexel;
  std::int32_t count = 0;
};

using TexelAxes = std::array<double, kTexelAxes>;

struct Standardisation {
  TexelAxes centre{};
  TexelAxes spread{};
};

struct MaterialGroup {
  std::int16_t id = kNoGroup;
  Gaussian gaussian;
  float share = 0.0f;
};

struct GroupMerge {
  float misclassified = 0.0f;
  std::int16_t kept = kNoGroup;
  std::int16_t absorbed = kNoGroup;
};

struct GroupSettings {
  float merge = 0.3f;
  std::uint32_t seed = 1;
};

struct MaterialGroups {
  Standardisation standardisation;
  std::size_t fitted = 0;
  std::vector<MaterialGroup> groups;
  std::vector<GroupMerge> merges;
  std::vector<std::int16_t> ofTexel;
};

struct GroupScoring {
  std::int16_t id = kNoGroup;
  std::array<float, kTexelAxes> mean{};
  std::array<float, kWhiteningFloats> whitening{};
  float logScale = 0.0f;
  float logShare = 0.0f;
};

inline constexpr std::uint32_t kLabelledStride = 4;

struct LabelledTexels {
  std::vector<MaterialTexel> texels;
  std::vector<std::int16_t> labels;
};

struct GroupProbabilities {
  std::uint32_t side = 0;
  std::vector<std::array<std::uint8_t, kMaxGroups>> ofTexel;
};

inline constexpr std::uint16_t kMaxPickChart = 254;

struct ChartCover {
  std::uint8_t group = 0;
  std::uint16_t chart = 0;
  float cover = 0.0f;
  [[nodiscard]] bool operator==(const ChartCover &) const = default;
};

struct MaterialGrouping {
  ClusterSettings settings;
  LabelledTexels labelled;
  MaterialGroups groups;
  std::vector<GroupScoring> scoring;
  GroupProbabilities smoothed;
  MaterialAnalysis analysis;
  std::vector<ChartCover> covers;
};

[[nodiscard]] std::expected<MaterialRaster, std::string>
RasterOf(std::uint32_t a_side, std::vector<MaterialTexel> a_texels,
         std::vector<std::uint16_t> a_charts);
[[nodiscard]] TexelAxes AxesOfTexel(const MaterialTexel &a_texel) noexcept;
[[nodiscard]] Regions SegmentRaster(const MaterialRaster &a_raster,
                                    const SegmentSettings &a_settings);
[[nodiscard]] std::expected<MaterialGroups, std::string>
GroupRegions(const MaterialRaster &a_raster, const Regions &a_regions,
             const GroupSettings &a_settings);
[[nodiscard]] std::vector<std::int16_t>
LabelTexels(const MaterialRaster &a_raster, const MaterialGroups &a_groups);
[[nodiscard]] std::vector<std::uint16_t>
ChartRasterOf(const MeshData &a_mesh, const MeshAnalysis &a_analysis,
              std::uint32_t a_side);
[[nodiscard]] std::vector<GroupScoring>
ScoringOf(const MaterialGroups &a_groups);
[[nodiscard]] float OddsWeightOf(const ClusterSettings &a_settings) noexcept;
[[nodiscard]] std::vector<std::int16_t>
LabelWithOdds(const MaterialRaster &a_raster, const MaterialGroups &a_groups,
              const std::vector<GroupScoring> &a_scoring,
              const GroupProbabilities &a_odds, float a_oddsWeight);
[[nodiscard]] float ScoreOf(const GroupScoring &a_group,
                            const Standardisation &a_standardisation,
                            const MaterialTexel &a_texel) noexcept;
[[nodiscard]] GroupProbabilities
SmoothedProbabilities(const MaterialRaster &a_raster,
                      const MaterialGroups &a_groups, std::uint32_t a_radius);
[[nodiscard]] std::vector<ChartCover>
ChartCoversOf(const MaterialRaster &a_raster,
              const std::vector<std::int16_t> &a_labels);
[[nodiscard]] MaterialAnalysis
AnalysisOf(const MaterialRaster &a_raster,
           const std::vector<std::int16_t> &a_labels);
[[nodiscard]] std::expected<MaterialGrouping, std::string>
GroupMaterial(const MaterialSample &a_sample,
              std::vector<std::uint16_t> a_charts,
              const ClusterSettings &a_settings);
}
