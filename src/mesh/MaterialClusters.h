#pragma once

#include "recipe/Recipe.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct MaterialTexel {
  float roughness = 0.0f;
  float metallic = 0.0f;
  float occlusion = 0.0f;
  float reflectance = 0.0f;
  float luma = 0.0f;
  [[nodiscard]] bool operator==(const MaterialTexel &) const = default;
};

inline constexpr std::size_t kMaxSampleTexels = 64 * 64;
struct MaterialSample {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<MaterialTexel> texels;
  [[nodiscard]] bool operator==(const MaterialSample &) const = default;
};

struct MaterialCluster {
  std::uint8_t id = 0;
  MaterialTexel centroid;
  float share = 0.0f;
  std::string description;
  [[nodiscard]] bool operator==(const MaterialCluster &) const = default;
};

struct MaterialAnalysis {
  ClusterSettings settings;
  std::vector<MaterialCluster> clusters;
  [[nodiscard]] bool operator==(const MaterialAnalysis &) const = default;
};

[[nodiscard]] MaterialAnalysis
ClusterMaterial(const MaterialSample &a_sample,
                const ClusterSettings &a_settings);
[[nodiscard]] std::uint8_t
NearestCluster(const MaterialTexel &a_texel,
               const MaterialAnalysis &a_analysis) noexcept;
[[nodiscard]] std::string DescribeTexel(const MaterialTexel &a_texel);
}
