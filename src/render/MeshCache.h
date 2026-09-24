// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "mesh/Islands.h"
#include "mesh/Mesh.h"
#include "mesh/MeshFacts.h"
#include "render/MeshReader.h"
#include "render/TextureLab.h"

#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>

namespace BetterEnchantmentEffects {
struct MeshEntry {
  RE::NiPointer<RE::BSGeometry> geometry;
  MeshIdentity identity;
  std::shared_ptr<const MeshData> mesh;
  std::string problem;
  MeshFacts facts;
  MeshAnalysis analysis;
  std::map<BakeKey, std::shared_ptr<TextureLab::RenderTarget>> bakes;
  std::uint32_t lastUsedMS = 0;
};

class MeshCache {
public:
  [[nodiscard]] std::expected<std::shared_ptr<MeshEntry>, std::string>
  Get(RE::BSGeometry *a_geometry, std::uint32_t a_nowMS, bool a_verbose);
  [[nodiscard]] std::shared_ptr<const MeshEntry>
  Cached(RE::BSGeometry *a_geometry) const noexcept;
  void Sweep(std::uint32_t a_nowMS, std::uint32_t a_maxAgeMS,
             std::span<RE::BSGeometry *const> a_keep, bool a_verbose);
  void Clear() noexcept;

private:
  std::unordered_map<RE::BSGeometry *, std::shared_ptr<MeshEntry>> entries_;
};
}
