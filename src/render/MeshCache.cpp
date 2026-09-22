#include "render/MeshCache.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string_view>

namespace BetterEnchantmentEffects {
namespace {
const char *NameOf(RE::BSGeometry *a_geometry) noexcept {
  return a_geometry && a_geometry->name.c_str() ? a_geometry->name.c_str()
                                                : "?";
}

struct UvBounds {
  std::size_t count = 0;
  float minU = 1.0e9f;
  float minV = 1.0e9f;
  float maxU = -1.0e9f;
  float maxV = -1.0e9f;

  void Add(Vec2 a_uv) {
    ++count;
    minU = std::min(minU, a_uv.x);
    minV = std::min(minV, a_uv.y);
    maxU = std::max(maxU, a_uv.x);
    maxV = std::max(maxV, a_uv.y);
  }
};

char SideOf(std::string_view a_bone) noexcept {
  if (a_bone.find(" L ") != std::string_view::npos) {
    return 'L';
  }
  if (a_bone.find(" R ") != std::string_view::npos) {
    return 'R';
  }
  return '-';
}

float BoxOverlapFraction(const UvBounds &a, const UvBounds &b) noexcept {
  const float w = std::min(a.maxU, b.maxU) - std::max(a.minU, b.minU);
  const float h = std::min(a.maxV, b.maxV) - std::max(a.minV, b.minV);
  if (w <= 0.0f || h <= 0.0f) {
    return 0.0f;
  }
  const float overlap = w * h;
  const float smaller = std::min((a.maxU - a.minU) * (a.maxV - a.minV),
                                 (b.maxU - b.minU) * (b.maxV - b.minV));
  return smaller > 0.0f ? overlap / smaller : 0.0f;
}

[[nodiscard]] std::size_t DominantWeight(const MeshVertex &a_vertex) noexcept {
  std::size_t top = 0;
  for (std::size_t i = 1; i < 4; ++i) {
    if (a_vertex.weights[i] > a_vertex.weights[top]) {
      top = i;
    }
  }
  return top;
}

void LogSideUvSplit(const char *a_name, const MeshData &a_mesh) {
  UvBounds left;
  UvBounds right;
  for (const MeshPartition &partition : a_mesh.partitions) {
    for (const MeshVertex &vertex : partition.vertices) {
      const std::size_t top = DominantWeight(vertex);
      if (vertex.weights[top] <= 0.0f ||
          vertex.bones[top] >= partition.boneNames.size()) {
        continue;
      }
      const char side = SideOf(partition.boneNames[vertex.bones[top]]);
      if (side == 'L') {
        left.Add(vertex.uv);
      } else if (side == 'R') {
        right.Add(vertex.uv);
      }
    }
  }
  if (left.count == 0 && right.count == 0) {
    return;
  }
  if (left.count == 0 || right.count == 0) {
    logger::info("mesh '{}': side split L={} verts R={} verts (one side only)",
                 a_name, left.count, right.count);
    return;
  }
  logger::info(
      "mesh '{}': side split L={} verts uv[{:.3f},{:.3f}]..[{:.3f},{:.3f}] "
      "R={} verts uv[{:.3f},{:.3f}]..[{:.3f},{:.3f}] overlap {:.0f}%",
      a_name, left.count, left.minU, left.minV, left.maxU, left.maxV,
      right.count, right.minU, right.minV, right.maxU, right.maxV,
      BoxOverlapFraction(left, right) * 100.0f);
}

void LogFacingUvSplit(const char *a_name, const MeshData &a_mesh) {
  UvBounds front;
  UvBounds back;
  for (const MeshPartition &partition : a_mesh.partitions) {
    for (const MeshVertex &vertex : partition.vertices) {
      (vertex.position.y >= a_mesh.center.y ? front : back).Add(vertex.uv);
    }
  }
  if (front.count == 0 || back.count == 0) {
    return;
  }
  logger::info("mesh '{}': facing split F={} verts B={} verts overlap {:.0f}%",
               a_name, front.count, back.count,
               BoxOverlapFraction(front, back) * 100.0f);
}

void LogRead(const char *a_name, const MeshData &a_mesh,
             const MeshAnalysis &a_analysis, RE::BSGeometry *a_compare) {
  std::size_t vertices = 0;
  std::size_t triangles = 0;
  for (const auto &partition : a_mesh.partitions) {
    vertices += partition.vertices.size();
    triangles += partition.triangles.size();
  }
  logger::info("mesh '{}': {}, {} partitions, {} vertices, {} triangles, {} "
               "components, {} charts, hash {:016x}",
               a_name, a_mesh.origin, a_mesh.partitions.size(), vertices,
               triangles, a_analysis.components, a_analysis.charts,
               a_mesh.hash);
  if (a_compare) {
    if (const auto compared = CompareWithGpu(a_compare)) {
      logger::info("mesh '{}': cpu copy vs gpu readback: {} of {} bytes differ",
                   a_name, compared->differing, compared->total);
    }
    LogSideUvSplit(a_name, a_mesh);
    LogFacingUvSplit(a_name, a_mesh);
  }
}
}

std::expected<std::shared_ptr<MeshEntry>, std::string>
MeshCache::Get(RE::BSGeometry *a_geometry, std::uint32_t a_nowMS,
               bool a_verbose) {
  if (!a_geometry) {
    return std::unexpected("no geometry to read");
  }
  const auto *name = NameOf(a_geometry);
  auto identity = IdentityOf(a_geometry);
  if (!identity) {
    return std::unexpected("no renderer buffers to read");
  }
  if (const auto it = entries_.find(a_geometry);
      it != entries_.end() && it->second) {
    auto &entry = *it->second;
    if (entry.identity == *identity) {
      if (a_verbose && entry.lastUsedMS != a_nowMS) {
        logger::info("mesh '{}': cached", name);
      }
      entry.lastUsedMS = a_nowMS;
      if (!entry.mesh) {
        return std::unexpected(
            std::format("the mesh could not be read: {}", entry.problem));
      }
      return it->second;
    }
    logger::info("mesh '{}': buffers changed, reading again", name);
  }
  auto entry = std::make_shared<MeshEntry>();
  entry->geometry = RE::NiPointer{a_geometry};
  entry->identity = std::move(*identity);
  entry->lastUsedMS = a_nowMS;
  auto read = ReadMesh(a_geometry);
  if (read) {
    entry->mesh = *read;
    entry->facts = FactsOf(*entry->mesh);
    entry->analysis = AnalyseMesh(*entry->mesh);
    LogRead(name, *entry->mesh, entry->analysis,
            a_verbose ? a_geometry : nullptr);
  } else {
    entry->problem = read.error();
  }
  entries_[a_geometry] = entry;
  if (!entry->mesh) {
    return std::unexpected(
        std::format("the mesh could not be read: {}", entry->problem));
  }
  return entry;
}

std::shared_ptr<const MeshEntry>
MeshCache::Cached(RE::BSGeometry *a_geometry) const noexcept {
  const auto it = entries_.find(a_geometry);
  return it != entries_.end() ? it->second : nullptr;
}

void MeshCache::Sweep(std::uint32_t a_nowMS, std::uint32_t a_maxAgeMS,
                      std::span<RE::BSGeometry *const> a_keep, bool a_verbose) {
  const auto dropped = std::erase_if(entries_, [&](const auto &a_pair) {
    const auto &[geometry, entry] = a_pair;
    if (!entry) {
      return true;
    }
    if (std::ranges::contains(a_keep, geometry)) {
      entry->lastUsedMS = a_nowMS;
      return false;
    }
    return a_nowMS - entry->lastUsedMS > a_maxAgeMS;
  });
  if (a_verbose && dropped > 0) {
    logger::info("mesh cache: dropped {} unused entries, {} kept", dropped,
                 entries_.size());
  }
}

void MeshCache::Clear() noexcept { entries_.clear(); }
}
