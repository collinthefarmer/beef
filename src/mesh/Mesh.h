// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "mesh/TextureSize.h"
#include "recipe/Recipe.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
struct MeshVertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
  std::array<std::uint16_t, 4> bones{};
  std::array<float, 4> weights{};
  [[nodiscard]] bool operator==(const MeshVertex &) const = default;
};

struct MeshPartition {
  static constexpr std::uint16_t kNoSlot = 0xFFFF;

  std::uint16_t slot = kNoSlot;
  std::vector<std::string> boneNames;
  std::vector<MeshVertex> vertices;
  std::vector<std::array<std::uint32_t, 3>> triangles;
  [[nodiscard]] bool operator==(const MeshPartition &) const = default;
};

inline void AddBoneWeights(std::map<std::string, float> &a_weights,
                           const MeshVertex &a_vertex,
                           const MeshPartition &a_partition) {
  for (std::size_t slot = 0; slot < a_vertex.bones.size(); ++slot) {
    const std::uint16_t bone = a_vertex.bones[slot];
    const float weight = a_vertex.weights[slot];
    if (bone < a_partition.boneNames.size() && std::isfinite(weight) &&
        weight > 0.0f) {
      a_weights[a_partition.boneNames[bone]] += weight;
    }
  }
}

struct MeshData {
  Vec3 center;
  float radius = 0.0f;
  std::vector<MeshPartition> partitions;
  std::string origin;
  std::uint64_t hash = 0;
  [[nodiscard]] bool operator==(const MeshData &) const = default;
};

struct MeshBound {
  Vec3 center;
  float radius = 0.0f;
};
[[nodiscard]] MeshBound
MeasureBound(std::span<const MeshPartition> a_partitions) noexcept;

inline constexpr std::uint64_t kHashBasis = 0xcbf29ce484222325ull;
[[nodiscard]] std::uint64_t
HashBytes(std::span<const std::uint8_t> a_bytes,
          std::uint64_t a_seed = kHashBasis) noexcept;
[[nodiscard]] std::uint64_t
HashPartitionsAndBound(const MeshData &a_mesh, std::uint64_t a_seed) noexcept;

struct VertexLayout {
  std::uint32_t stride = 0;
  std::optional<std::uint32_t> position;
  std::optional<std::uint32_t> uv;
  std::optional<std::uint32_t> normal;
  std::optional<std::uint32_t> skinning;
};

[[nodiscard]] float HalfToFloat(std::uint16_t a_half) noexcept;

[[nodiscard]] std::optional<MeshVertex>
DecodeVertex(std::span<const std::uint8_t> a_bytes,
             const VertexLayout &a_layout, std::size_t a_index) noexcept;

[[nodiscard]] std::vector<std::array<std::uint32_t, 3>>
TrianglesWithin(std::span<const std::array<std::uint32_t, 3>> a_triangles,
                std::size_t a_vertexCount);

struct RawPartition {
  std::span<const std::uint8_t> vertexBytes;
  std::span<const std::uint8_t> indexBytes;
  VertexLayout layout;
  std::uint32_t vertexCount = 0;
  std::uint32_t triangleCount = 0;
  std::uint16_t slot = MeshPartition::kNoSlot;
  std::vector<std::string> boneNames;
};

[[nodiscard]] std::optional<MeshPartition>
DecodePartition(const RawPartition &a_raw);

struct BakeVertex {
  float u = 0.0f;
  float v = 0.0f;
  float value[3]{};
  [[nodiscard]] bool operator==(const BakeVertex &) const = default;
};

struct BakeBuffers {
  std::vector<BakeVertex> vertices;
  std::vector<std::uint32_t> indices;
  bool vector = false;
  std::string problem;
  [[nodiscard]] bool operator==(const BakeBuffers &) const = default;
};

inline constexpr float kPositionFrame = 128.0f;
[[nodiscard]] BakeBuffers BuildBake(const MeshData &a_mesh,
                                    const BakeKind &a_kind);

inline constexpr float kDistanceFrame = 256.0f;
[[nodiscard]] BakeBuffers BuildDistanceBake(const MeshData &a_mesh,
                                            const Vec3 &a_from);

[[nodiscard]] std::string DefinitionOf(const BakeKind &a_kind);
[[nodiscard]] std::string DefinitionOf(const DistanceSource &a_distance);
[[nodiscard]] std::string DistanceBakeIdentity(const Vec3 &origin);

struct BakeKey {
  std::string definition;
  std::uint32_t pixels = 0;
  [[nodiscard]] auto operator<=>(const BakeKey &) const = default;
};
[[nodiscard]] inline BakeKey KeyOf(std::string a_definition,
                                   TextureSize a_size) {
  return BakeKey{std::move(a_definition), a_size.Pixels()};
}
}
