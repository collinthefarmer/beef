// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Recipe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct MaterialTexel {
  float roughness = 0.0f;
  float metallic = 0.0f;
  float occlusion = 0.0f;
  float reflectance = 0.0f;
  float luma = 0.0f;
  Vec3 diffuse{};
  float detail = 0.0f;
  [[nodiscard]] bool operator==(const MaterialTexel &) const = default;
};

struct Chroma {
  float orangeBlue = 0.5f;
  float greenMagenta = 0.5f;
};
[[nodiscard]] Chroma ChromaOf(const Vec3 &a_diffuse) noexcept;

inline constexpr std::array<Vec3, kMaxMaterialClusters> kClusterPalette{
    Vec3{0.24f, 0.64f, 0.91f}, Vec3{0.91f, 0.79f, 0.24f},
    Vec3{0.42f, 0.83f, 0.35f}, Vec3{0.83f, 0.37f, 0.83f},
    Vec3{0.96f, 0.55f, 0.37f}, Vec3{0.37f, 0.88f, 0.96f},
    Vec3{0.69f, 0.69f, 0.69f}, Vec3{0.91f, 0.31f, 0.31f}};

inline constexpr std::size_t kMaxSampleTexels = 256 * 256;
struct MaterialSample {
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::vector<MaterialTexel> texels;
  [[nodiscard]] bool operator==(const MaterialSample &) const = default;
};

struct MaterialPixels {
  std::uint32_t side = 0;
  std::vector<std::uint8_t> rmaos;
  std::vector<std::uint8_t> diffuse;
  [[nodiscard]] bool operator==(const MaterialPixels &) const = default;
};

struct MaterialCluster {
  std::uint8_t id = 0;
  MaterialTexel centroid;
  float share = 0.0f;
  std::string description;
  [[nodiscard]] bool operator==(const MaterialCluster &) const = default;
};

struct MaterialAnalysis {
  std::vector<MaterialCluster> clusters;
  [[nodiscard]] bool operator==(const MaterialAnalysis &) const = default;
};

[[nodiscard]] MaterialSample MaterialSampleOf(const MaterialPixels &a_pixels);
[[nodiscard]] std::string DescribeTexel(const MaterialTexel &a_texel);
[[nodiscard]] std::optional<std::uint8_t>
PickCluster(const MaterialAnalysis &a_analysis, float a_pick,
            float a_areaBias) noexcept;
[[nodiscard]] std::optional<float>
PickValueOf(const MaterialAnalysis &a_analysis, std::uint8_t a_cluster,
            float a_areaBias) noexcept;
}
