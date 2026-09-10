#pragma once

#include "Core.h"
#include "mesh/Mesh.h"
#include "recipe/Recipe.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
enum class IslandSource {
  kComponent,
  kChart,
};
struct IslandSourceSpec {
  IslandSource value;
  std::string_view name;
  std::string_view plainName;
  BakeKind bake;
};
inline constexpr IslandSourceSpec kIslandSources[]{
    {IslandSource::kComponent, "component", "part", ComponentIdBake{}},
    {IslandSource::kChart, "chart", "chart", ChartIdBake{}},
};
[[nodiscard]] std::string_view IslandSourceName(IslandSource a_source) noexcept;
[[nodiscard]] std::string_view
PlainIslandSourceName(IslandSource a_source) noexcept;
[[nodiscard]] SourceKind IslandBakeOf(IslandSource a_source) noexcept;

inline constexpr std::uint16_t kNoIsland = 0xFFFF;
inline constexpr std::uint16_t kMaxIslands = 255;

struct MeshIsland {
  IslandSource source = IslandSource::kComponent;
  std::uint16_t id = 0;
  std::size_t triangles = 0;
  float share = 0.0f;
  std::string dominantBone;
  float dominantShare = 0.0f;
  Vec3 centroid;
  std::optional<std::uint16_t> twin;
  [[nodiscard]] bool operator==(const MeshIsland &) const = default;
};

struct MeshAnalysis {
  std::vector<MeshIsland> islands;
  std::vector<std::uint16_t> componentOf;
  std::vector<std::uint16_t> chartOf;
  std::uint16_t components = 0;
  std::uint16_t charts = 0;
  [[nodiscard]] bool operator==(const MeshAnalysis &) const = default;
};

inline constexpr float kChartWeldUv = 1.0f / 4096.0f;

[[nodiscard]] MeshAnalysis AnalyseMesh(const MeshData &a_mesh);

[[nodiscard]] BakeBuffers BuildIslandBake(const MeshData &a_mesh,
                                          const MeshAnalysis &a_analysis,
                                          IslandSource a_source);
}
