// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/Compositor.h"

#include "SettingsFile.h"
#include "render/MeshCache.h"

#include <algorithm>
#include <cstdint>
#include <format>
#include <ranges>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
bool RealTexture(const TextureRef &a_texture) {
  const auto extent = TextureLab::ExtentOf(a_texture.get());
  return extent && extent->width > 4 && extent->height > 4;
}

}

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

const Compositor::MaterialRecord &
Compositor::AnalyseMaterial(const MaterialInputs &a_material) {
  const auto key =
      std::make_pair(a_material.rmaos.get(), a_material.diffuse.get());
  auto &record = materials_.Get(key, nowMS_);
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
  return materials_.Find(
      std::make_pair(a_material.rmaos.get(), a_material.diffuse.get()));
}

void Compositor::SweepMaterials(std::uint32_t a_nowMS,
                                std::span<const MaterialKey> a_keep) {
  materials_.Sweep(a_nowMS, kMaterialMaxAgeMS, kMaxUnusedMaterials, a_keep);
}

void Compositor::ClearMaterials() noexcept { materials_.Clear(); }
}
