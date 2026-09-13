#include "render/Compositor.h"

#include "SettingsFile.h"

#include <algorithm>
#include <format>
#include <ranges>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
bool RealTexture(const TextureRef &a_texture) {
  const auto extent = TextureLab::ExtentOf(a_texture.get());
  return extent && extent->width > 4 && extent->height > 4;
}

std::string DescribeTexture(const TextureRef &a_texture) {
  if (!a_texture) {
    return "the material has no texture in this slot";
  }
  const auto *data = reinterpret_cast<const RE::NiTexture::RendererData *>(
      a_texture->rendererTexture);
  const char *name = a_texture->name.c_str() ? a_texture->name.c_str() : "";
  if (!data) {
    return std::format("'{}' is not resident (no renderer data)", name);
  }
  if (!data->resourceView) {
    return std::format("'{}' has no shader resource view", name);
  }
  const auto extent = TextureLab::ExtentOf(a_texture.get());
  if (!extent) {
    return std::format("'{}' is not a 2D texture", name);
  }
  return std::format("'{}' is {}x{}, a placeholder", name, extent->width,
                     extent->height);
}

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

std::expected<std::shared_ptr<MeshEntry>, std::string>
Compositor::MeshOf(RE::BSGeometry *a_geometry) {
  return meshes_.Get(a_geometry, nowMS_, GetSettings().verboseLogging);
}

std::shared_ptr<const MeshEntry>
Compositor::CachedMesh(RE::BSGeometry *a_geometry) const noexcept {
  return meshes_.Cached(a_geometry);
}

bool Compositor::MeshSweepDue(std::uint32_t a_nowMS) const noexcept {
  return a_nowMS - lastSweepMS_ >= kMeshSweepMS;
}

void Compositor::SweepMeshes(std::uint32_t a_nowMS,
                             std::span<RE::BSGeometry *const> a_bound) {
  lastSweepMS_ = a_nowMS;
  meshes_.Sweep(a_nowMS, kMeshMaxAgeMS, a_bound, GetSettings().verboseLogging);
}

void Compositor::ClearMeshes() noexcept { meshes_.Clear(); }

std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string>
Compositor::BakeInto(MeshEntry &a_entry, const std::string &a_key,
                     TextureSize a_size,
                     const std::function<BakeBuffers()> &a_buffers) {
  if (const auto it = a_entry.bakes.find(a_key); it != a_entry.bakes.end()) {
    return it->second;
  }
  auto *lab = TextureLab::GetSingleton();
  if (!lab->Init() || !lab->BakingAvailable()) {
    return std::unexpected(
        "the bake pass is unavailable (see the log at start)");
  }
  const auto buffers = a_buffers();
  if (!buffers.problem.empty()) {
    return std::unexpected(buffers.problem);
  }
  auto target = lab->Acquire(a_size);
  if (!target) {
    return std::unexpected("no render target available");
  }
  if (!lab->BakeMesh(*target, buffers)) {
    return std::unexpected("the bake pass failed");
  }
  if (GetSettings().verboseLogging) {
    logger::info("bake '{}' on '{}' at {} px", KeyDefinition(a_key),
                 a_entry.geometry && a_entry.geometry->name.c_str()
                     ? a_entry.geometry->name.c_str()
                     : "?",
                 a_size.Pixels());
  }
  a_entry.bakes[a_key] = target;
  return target;
}

std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string>
Compositor::PrepareBake(const BakeSource &a_bake,
                        const GeometryInputs &a_inputs, TextureSize a_size) {
  const auto entry = MeshOf(a_inputs.geometry.get());
  if (!entry) {
    return std::unexpected(entry.error());
  }
  return BakeInto(**entry, BakeKeyOf(a_bake.bake, a_size), a_size, [&] {
    if (Is<ComponentIdBake>(a_bake.bake)) {
      return BuildIslandBake(*(*entry)->mesh, (*entry)->analysis,
                             IslandSource::kComponent);
    }
    if (Is<ChartIdBake>(a_bake.bake)) {
      return BuildIslandBake(*(*entry)->mesh, (*entry)->analysis,
                             IslandSource::kChart);
    }
    return BuildBake(*(*entry)->mesh, a_bake.bake);
  });
}

std::expected<std::shared_ptr<TextureLab::RenderTarget>, std::string>
Compositor::PrepareDistance(const DistanceSource &a_distance,
                            const GeometryInputs &a_inputs,
                            TextureSize a_size) {
  const auto entry = MeshOf(a_inputs.geometry.get());
  if (!entry) {
    return std::unexpected(entry.error());
  }
  std::optional<Vec3> from = Match(
      a_distance.from, [&](const Vec3 &point) { return std::optional{point}; },
      [&](const std::string &node) {
        return NodeBindPosition(a_inputs.geometry.get(), a_inputs.root.get(),
                                node);
      });
  if (!from) {
    return std::unexpected(std::format("node '{}' was not found on the wearer",
                                       Get<std::string>(a_distance.from)
                                           ? *Get<std::string>(a_distance.from)
                                           : ""));
  }
  return BakeInto(**entry, DistanceKeyOf(a_distance, a_size), a_size,
                  [&] { return BuildDistanceBake(*(*entry)->mesh, *from); });
}

const Compositor::MaterialRecord &
Compositor::AnalyseMaterial(const MaterialInputs &a_material) {
  const auto key =
      std::make_pair(a_material.rmaos.get(), a_material.diffuse.get());
  auto &record = materials_[key];
  if (record.sample || !record.problem.empty()) {
    return record;
  }
  record.rmaos = a_material.rmaos;
  record.diffuse = a_material.diffuse;
  if (!RealTexture(a_material.rmaos) || !RealTexture(a_material.diffuse)) {
    record.problem =
        std::format("RMAOS {}; diffuse {}", DescribeTexture(a_material.rmaos),
                    DescribeTexture(a_material.diffuse));
    return record;
  }
  const bool verbose = GetSettings().verboseLogging;
  if (verbose) {
    logger::info("material '{}': sampling", a_material.rmaos->name.c_str()
                                                ? a_material.rmaos->name.c_str()
                                                : "?");
  }
  auto sample = TextureLab::GetSingleton()->SampleMaterial(
      a_material.rmaos.get(), a_material.diffuse.get());
  if (!sample) {
    record.problem = "the maps could not be read back";
    return record;
  }
  record.sample = std::make_shared<const MaterialSample>(std::move(*sample));
  record.analysis = std::make_shared<const MaterialAnalysis>(
      ClusterMaterial(*record.sample, ClusterSettings{}));
  if (verbose) {
    logger::info("material '{}': sampled {}x{}, {} clusters",
                 a_material.rmaos->name.c_str() ? a_material.rmaos->name.c_str()
                                                : "?",
                 record.sample->width, record.sample->height,
                 record.analysis->clusters.size());
  }
  return record;
}

const Compositor::MaterialRecord *
Compositor::CachedMaterial(const MaterialInputs &a_material) const noexcept {
  const auto it = materials_.find(
      std::make_pair(a_material.rmaos.get(), a_material.diffuse.get()));
  return it == materials_.end() ? nullptr : &it->second;
}

void Compositor::ClearMaterials() noexcept { materials_.clear(); }
}
