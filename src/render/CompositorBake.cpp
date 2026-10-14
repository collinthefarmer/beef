// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/MaterialGrouper.h"
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
Compositor::MeshOf(RE::BSGeometry *a_geometry, MeshUse a_use) {
  return meshes_.Get(a_geometry, nowMS_, GetSettings().verboseLogging, a_use);
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

Compositor::MaterialKey
Compositor::MaterialKeyOf(const MaterialInputs &a_material,
                          std::uint64_t a_meshHash) {
  return {TextureIdentity(a_material.rmaos),
          TextureIdentity(a_material.diffuse), a_meshHash};
}

std::uint64_t
Compositor::MeshHashOf(RE::BSGeometry *a_geometry) const noexcept {
  const std::shared_ptr<const MeshEntry> entry = CachedMesh(a_geometry);
  return entry ? entry->hash : 0;
}

namespace {
void Settle(Compositor::MaterialRecord &a_record) {
  if (!a_record.ticket) {
    return;
  }
  std::optional<GroupingResult> result =
      MaterialGrouper::GetSingleton().Poll(*a_record.ticket);
  if (!result) {
    return;
  }
  a_record.ticket.reset();
  if (*result) {
    a_record.grouping = **result;
  } else {
    a_record.problem = result->error();
  }
}
}

const Compositor::MaterialRecord &
Compositor::AnalyseMaterial(const MaterialInputs &a_material,
                            const std::shared_ptr<MeshEntry> &a_mesh) {
  auto &record = materials_.Get(
      MaterialKeyOf(a_material, a_mesh ? a_mesh->hash : 0), nowMS_);
  Settle(record);
  if (record.ticket || record.grouping || !record.problem.empty()) {
    return record;
  }
  if (!RealTexture(a_material.rmaos) || !RealTexture(a_material.diffuse)) {
    record.problem =
        std::format("RMAOS {}; diffuse {}", DescribeTexture(a_material.rmaos),
                    DescribeTexture(a_material.diffuse));
    return record;
  }
  TextureLab *lab = TextureLab::GetSingleton();
  if (!lab) {
    record.problem = "the texture lab is not available";
    return record;
  }
  auto sample =
      lab->SampleMaterial({a_material.rmaos.get(), a_material.diffuse.get(),
                           a_material.normal.get()});
  if (!sample) {
    record.problem = "the maps could not be read back";
    return record;
  }
  GroupingJob job;
  job.settings = ClusterSettings{};
  if (a_mesh) {
    job.mesh = a_mesh->mesh;
    job.analysis = a_mesh->analysis;
  }
  job.key = GroupingKeyOf(*sample, a_mesh ? a_mesh->hash : 0, job.settings);
  job.sample = std::move(*sample);
  record.ticket = GroupingTicket{std::move(job)};
  Settle(record);
  if (GetSettings().verboseLogging) {
    logger::info("material '{}': sampled, grouping on the worker",
                 a_material.rmaos->name.c_str() ? a_material.rmaos->name.c_str()
                                                : "?");
  }
  return record;
}

const Compositor::MaterialRecord *
Compositor::CachedMaterial(const MaterialInputs &a_material,
                           std::uint64_t a_meshHash) {
  const MaterialKey key = MaterialKeyOf(a_material, a_meshHash);
  if (!materials_.Find(key)) {
    return nullptr;
  }
  MaterialRecord &record = materials_.Get(key, nowMS_);
  Settle(record);
  return &record;
}

void Compositor::SweepMaterials(std::uint32_t a_nowMS,
                                std::span<const MaterialKey> a_keep) {
  materials_.Sweep(a_nowMS, kMaterialMaxAgeMS, kMaxUnusedMaterials, a_keep);
}

void Compositor::ClearMaterials() noexcept { materials_.Clear(); }

std::optional<TextureView> Compositor::FindStepOutput(const std::string &a_key,
                                                      std::uint64_t a_nowMS) {
  return stepOutputs_.Find(a_key, a_nowMS);
}

void Compositor::PublishStepOutput(const std::string &a_key,
                                   const TextureView &a_view,
                                   std::uint64_t a_nowMS) {
  stepOutputs_.Publish(a_key, a_view, a_nowMS);
}

void Compositor::SweepStepOutputs(std::uint64_t a_nowMS) {
  stepOutputs_.Sweep(a_nowMS);
}

void Compositor::ClearStepOutputs() noexcept { stepOutputs_.Clear(); }
}
