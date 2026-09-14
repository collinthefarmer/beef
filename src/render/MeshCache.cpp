#include "render/MeshCache.h"

#include <algorithm>
#include <format>

namespace BetterEnchantmentEffects {
namespace {
const char *NameOf(RE::BSGeometry *a_geometry) noexcept {
  return a_geometry && a_geometry->name.c_str() ? a_geometry->name.c_str()
                                                : "?";
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
    if (std::ranges::find(a_keep, geometry) != a_keep.end()) {
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
