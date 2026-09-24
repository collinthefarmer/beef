// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "PCH.h"
#include "mesh/Mesh.h"

#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
[[nodiscard]] std::expected<std::shared_ptr<const MeshData>, std::string>
ReadMesh(RE::BSGeometry *a_geometry);

struct MeshIdentity {
  const void *skinPartition = nullptr;
  std::vector<const void *> buffers;
  std::uint32_t vertices = 0;

  [[nodiscard]] bool operator==(const MeshIdentity &) const = default;
};
[[nodiscard]] std::optional<MeshIdentity>
IdentityOf(RE::BSGeometry *a_geometry);

struct GpuComparison {
  std::size_t differing = 0;
  std::size_t total = 0;
};
[[nodiscard]] std::optional<GpuComparison>
CompareWithGpu(RE::BSGeometry *a_geometry);

[[nodiscard]] std::optional<Vec3> NodeBindPosition(RE::BSGeometry *a_geometry,
                                                   RE::NiAVObject *a_root,
                                                   std::string_view a_node);

[[nodiscard]] Vec3 ToRootSpace(RE::NiAVObject *a_root, const Vec3 &a_world);
}
