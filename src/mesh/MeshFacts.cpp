#include "mesh/MeshFacts.h"

#include "mesh/Mesh.h"
#include "recipe/Words.h"

#include <algorithm>
#include <map>
#include <string>

namespace BetterEnchantmentEffects {
namespace {
std::string SlotLabel(std::uint32_t a_slot) {
  for (const BipedSlotSpec &spec : kBipedSlots) {
    if (spec.slot == a_slot) {
      return std::string{spec.name};
    }
  }
  return std::to_string(a_slot);
}
}

std::vector<SlotCoverage> SlotsOf(const MeshData &a_mesh) {
  std::vector<SlotCoverage> slots;
  for (const MeshPartition &partition : a_mesh.partitions) {
    if (partition.slot == MeshPartition::kNoSlot) {
      continue;
    }
    const std::uint32_t slot = partition.slot;
    const auto it = std::ranges::find(slots, slot, &SlotCoverage::slot);
    if (it != slots.end()) {
      it->triangles += partition.triangles.size();
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
      for (std::size_t i = 0; i < 4; ++i) {
        if (vertex.weights[i] <= 0.0f ||
            vertex.bones[i] >= partition.boneNames.size()) {
          continue;
        }
        weight[partition.boneNames[vertex.bones[i]]] += vertex.weights[i];
      }
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
