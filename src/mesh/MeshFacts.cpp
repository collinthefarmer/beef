// GPL-3.0-only with the additional permission in COPYING.md.
#include "mesh/MeshFacts.h"

#include "mesh/Mesh.h"
#include "recipe/Words.h"

#include <algorithm>
#include <map>
#include <string>

namespace BetterEnchantmentEffects {
namespace {
std::string SlotLabel(std::uint32_t a_slot) {
  const auto name = BipedSlotName(BipedSlot{a_slot});
  return name ? std::string{*name} : std::to_string(a_slot);
}
}

std::vector<SlotCoverage> SlotsOf(const MeshData &a_mesh) {
  std::vector<SlotCoverage> slots;
  for (const MeshPartition &partition : a_mesh.partitions) {
    if (partition.slot == MeshPartition::kNoSlot) {
      continue;
    }
    const std::uint32_t slot = partition.slot;
    if (SlotCoverage *existing = FindBy(slots, slot, &SlotCoverage::slot)) {
      existing->triangles += partition.triangles.size();
      continue;
    }
    SlotCoverage coverage;
    coverage.slot = slot;
    coverage.name = SlotLabel(slot);
    coverage.triangles = partition.triangles.size();
    slots.push_back(std::move(coverage));
  }
  return slots;
}

std::vector<BoneCoverage> BonesOf(const MeshData &a_mesh) {
  std::map<std::string, float> weight;
  std::size_t vertices = 0;
  for (const MeshPartition &partition : a_mesh.partitions) {
    for (const MeshVertex &vertex : partition.vertices) {
      ++vertices;
      AddBoneWeights(weight, vertex, partition);
    }
  }
  std::vector<BoneCoverage> bones;
  bones.reserve(weight.size());
  for (const auto &[name, sum] : weight) {
    bones.push_back(BoneCoverage{
        name, vertices > 0 ? sum / static_cast<float>(vertices) : 0.0f});
  }
  std::ranges::sort(
      bones, [](const BoneCoverage &a_first, const BoneCoverage &a_second) {
        return a_first.coverage > a_second.coverage;
      });
  return bones;
}

MeshFacts FactsOf(const MeshData &a_mesh) {
  return MeshFacts{SlotsOf(a_mesh), BonesOf(a_mesh)};
}
}
