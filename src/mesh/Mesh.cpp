#include "mesh/Mesh.h"

#include "recipe/Words.h"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <functional>
#include <utility>

namespace BetterEnchantmentEffects {
float HalfToFloat(std::uint16_t a_half) noexcept {
  const std::uint32_t sign = (a_half & 0x8000u) << 16;
  const std::uint32_t exponent = (a_half >> 10) & 0x1Fu;
  const std::uint32_t mantissa = a_half & 0x3FFu;
  std::uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa == 0) {
      bits = sign;
    } else {
      std::uint32_t m = mantissa;
      int e = -1;
      do {
        ++e;
        m <<= 1;
      } while ((m & 0x400u) == 0);
      bits = sign | ((127 - 15 - e) << 23) | ((m & 0x3FFu) << 13);
    }
  } else if (exponent == 31) {
    bits = sign | 0x7F800000u | (mantissa << 13);
  } else {
    bits = sign | ((exponent + 112) << 23) | (mantissa << 13);
  }
  return std::bit_cast<float>(bits);
}

namespace {
float ReadFloat(std::span<const std::uint8_t> a_bytes,
                std::size_t a_at) noexcept {
  float value = 0.0f;
  std::memcpy(&value, a_bytes.data() + a_at, sizeof(float));
  return value;
}

std::uint16_t ReadU16(std::span<const std::uint8_t> a_bytes,
                      std::size_t a_at) noexcept {
  std::uint16_t value = 0;
  std::memcpy(&value, a_bytes.data() + a_at, sizeof(std::uint16_t));
  return value;
}

float Biased(std::uint8_t a_byte) noexcept {
  return static_cast<float>(a_byte) / 255.0f * 2.0f - 1.0f;
}
}

std::optional<MeshVertex> DecodeVertex(std::span<const std::uint8_t> a_bytes,
                                       const VertexLayout &a_layout,
                                       std::size_t a_index) noexcept {
  if (!a_layout.position || a_layout.stride == 0) {
    return std::nullopt;
  }
  const std::size_t base = a_index * a_layout.stride;
  if (base + a_layout.stride > a_bytes.size()) {
    return std::nullopt;
  }
  const auto fits = [&](std::uint32_t a_offset, std::size_t a_size) {
    return a_offset + a_size <= a_layout.stride;
  };
  MeshVertex v;
  if (!fits(*a_layout.position, 12)) {
    return std::nullopt;
  }
  v.position = Vec3{ReadFloat(a_bytes, base + *a_layout.position),
                    ReadFloat(a_bytes, base + *a_layout.position + 4),
                    ReadFloat(a_bytes, base + *a_layout.position + 8)};
  if (a_layout.uv && fits(*a_layout.uv, 4)) {
    v.uv = Vec2{HalfToFloat(ReadU16(a_bytes, base + *a_layout.uv)),
                HalfToFloat(ReadU16(a_bytes, base + *a_layout.uv + 2))};
  }
  if (a_layout.normal && fits(*a_layout.normal, 4)) {
    const auto at = base + *a_layout.normal;
    v.normal = Vec3{Biased(a_bytes[at]), Biased(a_bytes[at + 1]),
                    Biased(a_bytes[at + 2])};
  }
  if (a_layout.skinning && fits(*a_layout.skinning, 12)) {
    const auto at = base + *a_layout.skinning;
    for (std::size_t i = 0; i < 4; ++i) {
      v.weights[i] = HalfToFloat(ReadU16(a_bytes, at + i * 2));
      v.bones[i] = a_bytes[at + 8 + i];
    }
  }
  return v;
}

std::vector<std::array<std::uint32_t, 3>>
TrianglesWithin(std::span<const std::array<std::uint32_t, 3>> a_triangles,
                std::size_t a_vertexCount) {
  std::vector<std::array<std::uint32_t, 3>> kept;
  kept.reserve(a_triangles.size());
  for (const auto &triangle : a_triangles) {
    if (triangle[0] < a_vertexCount && triangle[1] < a_vertexCount &&
        triangle[2] < a_vertexCount) {
      kept.push_back(triangle);
    }
  }
  return kept;
}

std::optional<MeshPartition> DecodePartition(const RawPartition &a_raw) {
  MeshPartition partition;
  partition.slot = a_raw.slot;
  partition.boneNames = a_raw.boneNames;
  partition.vertices.reserve(a_raw.vertexCount);
  for (std::uint32_t i = 0; i < a_raw.vertexCount; ++i) {
    const std::optional<MeshVertex> vertex =
        DecodeVertex(a_raw.vertexBytes, a_raw.layout, i);
    if (!vertex) {
      return std::nullopt;
    }
    partition.vertices.push_back(*vertex);
  }
  const std::size_t indexCount =
      static_cast<std::size_t>(a_raw.triangleCount) * 3;
  if (indexCount * sizeof(std::uint16_t) > a_raw.indexBytes.size()) {
    return std::nullopt;
  }
  std::vector<std::array<std::uint32_t, 3>> triangles;
  triangles.reserve(a_raw.triangleCount);
  for (std::uint32_t t = 0; t < a_raw.triangleCount; ++t) {
    const std::size_t at =
        static_cast<std::size_t>(t) * 3 * sizeof(std::uint16_t);
    triangles.push_back({ReadU16(a_raw.indexBytes, at),
                         ReadU16(a_raw.indexBytes, at + 2),
                         ReadU16(a_raw.indexBytes, at + 4)});
  }
  partition.triangles = TrianglesWithin(triangles, partition.vertices.size());
  return partition;
}

MeshBound MeasureBound(std::span<const MeshPartition> a_partitions) noexcept {
  Vec3 lo{};
  Vec3 hi{};
  bool any = false;
  for (const auto &partition : a_partitions) {
    for (const auto &vertex : partition.vertices) {
      const Vec3 &p = vertex.position;
      if (!any) {
        lo = hi = p;
        any = true;
        continue;
      }
      lo = Vec3{std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
      hi = Vec3{std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
  }
  if (!any) {
    return {};
  }
  MeshBound bound;
  bound.center =
      Vec3{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f, (lo.z + hi.z) * 0.5f};
  for (const auto &partition : a_partitions) {
    for (const auto &vertex : partition.vertices) {
      const Vec3 &p = vertex.position;
      const float dx = p.x - bound.center.x, dy = p.y - bound.center.y,
                  dz = p.z - bound.center.z;
      bound.radius =
          std::max(bound.radius, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
  }
  return bound;
}

namespace {
bool Wanted(const MeshPartition &a_partition, const BakeKind &a_kind) {
  const auto *partition = std::get_if<PartitionBake>(&a_kind);
  return !partition ||
         a_partition.slot == std::to_underlying(partition->bipedSlot);
}

std::array<float, 3> ValueOf(const MeshVertex &a_vertex,
                             const MeshPartition &a_partition,
                             const MeshData &a_mesh, const BakeKind &a_kind) {
  return Match(
      a_kind,
      [&](const PositionBake &) {
        const auto at = [](float p) {
          return std::clamp(p / (2.0f * kPositionFrame) + 0.5f, 0.0f, 1.0f);
        };
        return std::array{at(a_vertex.position.x), at(a_vertex.position.y),
                          at(a_vertex.position.z)};
      },
      [&](const LocalPositionBake &) {
        const float scale = a_mesh.radius > 0.0f ? 0.5f / a_mesh.radius : 0.0f;
        const auto at = [&](float p, float c) {
          return std::clamp((p - c) * scale + 0.5f, 0.0f, 1.0f);
        };
        return std::array{at(a_vertex.position.x, a_mesh.center.x),
                          at(a_vertex.position.y, a_mesh.center.y),
                          at(a_vertex.position.z, a_mesh.center.z)};
      },
      [&](const NormalBake &) {
        const auto at = [](float n) {
          return std::clamp(n * 0.5f + 0.5f, 0.0f, 1.0f);
        };
        return std::array{at(a_vertex.normal.x), at(a_vertex.normal.y),
                          at(a_vertex.normal.z)};
      },
      [&](const UvBake &) {
        return std::array{a_vertex.uv.x, a_vertex.uv.y, 0.0f};
      },
      [&](const PartitionBake &) { return std::array{1.0f, 1.0f, 1.0f}; },
      [&](const BoneWeightBake &bake) {
        float sum = 0.0f;
        for (std::size_t i = 0; i < 4; ++i) {
          const auto bone = a_vertex.bones[i];
          if (bone < a_partition.boneNames.size() &&
              std::ranges::contains(bake.bones, a_partition.boneNames[bone])) {
            sum += a_vertex.weights[i];
          }
        }
        sum = std::clamp(sum, 0.0f, 1.0f);
        return std::array{sum, sum, sum};
      },
      [&](const ComponentIdBake &) { return std::array{1.0f, 1.0f, 1.0f}; },
      [&](const ChartIdBake &) { return std::array{1.0f, 1.0f, 1.0f}; });
}

std::string NeedsAnalysis(const BakeKind &a_kind) {
  return Match(
      a_kind,
      [](const ComponentIdBake &) {
        return std::string{"componentId needs the mesh analysis"};
      },
      [](const ChartIdBake &) {
        return std::string{"chartId needs the mesh analysis"};
      },
      [](const PositionBake &) { return std::string{}; },
      [](const LocalPositionBake &) { return std::string{}; },
      [](const NormalBake &) { return std::string{}; },
      [](const UvBake &) { return std::string{}; },
      [](const PartitionBake &) { return std::string{}; },
      [](const BoneWeightBake &) { return std::string{}; });
}

}

BakeBuffers BuildBake(const MeshData &a_mesh, const BakeKind &a_kind) {
  BakeBuffers out;
  out.vector = std::holds_alternative<PositionBake>(a_kind) ||
               std::holds_alternative<LocalPositionBake>(a_kind) ||
               std::holds_alternative<NormalBake>(a_kind) ||
               std::holds_alternative<UvBake>(a_kind);
  if (std::holds_alternative<LocalPositionBake>(a_kind) &&
      a_mesh.radius <= 0.0f) {
    out.problem = "the mesh has no bound to map positions into";
    return out;
  }
  if (auto problem = NeedsAnalysis(a_kind); !problem.empty()) {
    out.problem = std::move(problem);
    return out;
  }
  for (const auto &partition : a_mesh.partitions) {
    if (!Wanted(partition, a_kind)) {
      continue;
    }
    const auto base = static_cast<std::uint32_t>(out.vertices.size());
    for (const auto &vertex : partition.vertices) {
      BakeVertex bv;
      bv.u = vertex.uv.x;
      bv.v = vertex.uv.y;
      const auto value = ValueOf(vertex, partition, a_mesh, a_kind);
      bv.value[0] = value[0];
      bv.value[1] = value[1];
      bv.value[2] = value[2];
      out.vertices.push_back(bv);
    }
    for (const auto &triangle : partition.triangles) {
      if (triangle[0] < partition.vertices.size() &&
          triangle[1] < partition.vertices.size() &&
          triangle[2] < partition.vertices.size()) {
        out.indices.push_back(base + triangle[0]);
        out.indices.push_back(base + triangle[1]);
        out.indices.push_back(base + triangle[2]);
      }
    }
  }
  if (out.indices.empty()) {
    out.vertices.clear();
    if (const auto *partition = std::get_if<PartitionBake>(&a_kind)) {
      const auto name = BipedSlotName(partition->bipedSlot);
      out.problem = std::format("no partition in biped slot {}{}",
                                std::to_underlying(partition->bipedSlot),
                                name ? std::format(" ({})", *name) : "");
    } else {
      out.problem = "the mesh has no triangles to bake";
    }
  }
  return out;
}

namespace {
BakeBuffers
ScalarBake(const MeshData &a_mesh,
           const std::function<float(const MeshVertex &)> &a_value) {
  BakeBuffers out;
  for (const auto &partition : a_mesh.partitions) {
    const auto base = static_cast<std::uint32_t>(out.vertices.size());
    for (const auto &vertex : partition.vertices) {
      BakeVertex bv;
      bv.u = vertex.uv.x;
      bv.v = vertex.uv.y;
      bv.value[0] = bv.value[1] = bv.value[2] =
          std::clamp(a_value(vertex), 0.0f, 1.0f);
      out.vertices.push_back(bv);
    }
    for (const auto &triangle : partition.triangles) {
      if (triangle[0] < partition.vertices.size() &&
          triangle[1] < partition.vertices.size() &&
          triangle[2] < partition.vertices.size()) {
        out.indices.push_back(base + triangle[0]);
        out.indices.push_back(base + triangle[1]);
        out.indices.push_back(base + triangle[2]);
      }
    }
  }
  if (out.indices.empty()) {
    out.vertices.clear();
    out.problem = "the mesh has no triangles to bake";
  }
  return out;
}
}

BakeBuffers BuildDistanceBake(const MeshData &a_mesh, const Vec3 &a_from) {
  return ScalarBake(a_mesh, [&](const MeshVertex &v) {
    const float dx = v.position.x - a_from.x, dy = v.position.y - a_from.y,
                dz = v.position.z - a_from.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz) / kDistanceFrame;
  });
}

std::uint64_t HashBytes(std::span<const std::uint8_t> a_bytes,
                        std::uint64_t a_seed) noexcept {
  constexpr std::uint64_t kPrime = 0x100000001b3ull;
  std::uint64_t hash = a_seed;
  for (const auto byte : a_bytes) {
    hash ^= byte;
    hash *= kPrime;
  }
  return hash;
}

namespace {
std::string Definition(const BakeKind &a_kind) {
  return Match(
      a_kind, [](const PositionBake &) { return std::string{"position"}; },
      [](const LocalPositionBake &) { return std::string{"localPosition"}; },
      [](const NormalBake &) { return std::string{"normal"}; },
      [](const UvBake &) { return std::string{"uv"}; },
      [](const PartitionBake &p) {
        return std::format("partition {}", std::to_underlying(p.bipedSlot));
      },
      [](const BoneWeightBake &b) {
        std::vector<std::string> sorted = b.bones;
        std::ranges::sort(sorted);
        std::string names;
        for (const auto &bone : sorted) {
          names += (names.empty() ? "" : ", ") + bone;
        }
        return std::format("boneWeight [{}]", names);
      },
      [](const ComponentIdBake &) { return std::string{"componentId"}; },
      [](const ChartIdBake &) { return std::string{"chartId"}; });
}
}

std::string DefinitionOf(const BakeKind &a_kind) {
  return std::format("bake {}", Definition(a_kind));
}

std::string DefinitionOf(const DistanceSource &a_distance) {
  return std::format("distance from node {}", a_distance.from);
}

}
