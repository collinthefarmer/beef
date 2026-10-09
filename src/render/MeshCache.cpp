// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/MeshCache.h"

#include <algorithm>
#include <format>
#include <optional>
#include <string_view>

namespace BetterEnchantmentEffects {
namespace {
void Trim(MeshEntry &a_entry) {
  a_entry.mesh.reset();
  a_entry.analysis.componentOf = {};
  a_entry.analysis.chartOf = {};
}

const char *NameOf(RE::BSGeometry *a_geometry) noexcept {
  return a_geometry && a_geometry->name.c_str() ? a_geometry->name.c_str()
                                                : "?";
}

void LogRead(const char *a_name, const MeshData &a_mesh,
             const MeshAnalysis &a_analysis, RE::BSGeometry *a_geometry) {
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
  if (const auto compared = CompareWithGpu(a_geometry);
      compared && compared->differing > 0) {
    logger::warn("mesh '{}': cpu copy differs from the gpu readback in {} of "
                 "{} bytes",
                 a_name, compared->differing, compared->total);
  }
}
}

std::expected<std::shared_ptr<MeshEntry>, std::string>
MeshCache::Get(RE::BSGeometry *a_geometry, std::uint32_t a_nowMS,
               bool a_verbose, MeshUse a_use) {
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
      entry.lastUsedMS = a_nowMS;
      if (!entry.Read()) {
        return std::unexpected(
            std::format("the mesh could not be read: {}", entry.problem));
      }
      if (a_use == MeshUse::kSummary) {
        return it->second;
      }
      if (!entry.Trimmed()) {
        entry.dataUsed = true;
        return it->second;
      }
      if (a_verbose) {
        logger::info("mesh '{}': reading again for its data", name);
      }
    } else if (a_verbose) {
      logger::info("mesh '{}': buffers changed, reading again", name);
    }
  }
  auto entry = std::make_shared<MeshEntry>();
  entry->geometry = RE::NiPointer{a_geometry};
  entry->identity = std::move(*identity);
  entry->lastUsedMS = a_nowMS;
  entry->readMS = a_nowMS;
  entry->dataUsed = a_use == MeshUse::kData;
  auto read = ReadMesh(a_geometry);
  if (read) {
    entry->mesh = *read;
    entry->facts = FactsOf(*entry->mesh);
    entry->analysis = AnalyseMesh(*entry->mesh);
    entry->hash = entry->mesh->hash;
    entry->partitions = entry->mesh->partitions.size();
    if (a_verbose) {
      LogRead(name, *entry->mesh, entry->analysis, a_geometry);
    }
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
      if (!entry->dataUsed && entry->mesh &&
          a_nowMS - entry->readMS > a_maxAgeMS) {
        Trim(*entry);
      }
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
