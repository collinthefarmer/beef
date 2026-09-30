// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include "render/Compositor.h"

#include <string_view>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
std::vector<const LiveGeometry *> GeometriesNamed(const LiveActor &a_state,
                                                  std::string_view a_name) {
  std::vector<const LiveGeometry *> named;
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &bound : piece.geometries) {
      if (bound.name == a_name && !bound.lost) {
        named.push_back(&bound);
      }
    }
  }
  return named;
}

void ReportMeshProblems(const LiveGeometry &a_bound) {
  Compositor *compositor = Compositor::GetSingleton();
  if (!compositor) {
    return;
  }
  if (const std::expected<std::shared_ptr<MeshEntry>, std::string> mesh =
          compositor->MeshOf(a_bound.geometry.get());
      !mesh) {
    logger::warn("mesh '{}': {}", a_bound.name, mesh.error());
  }
  if (const Compositor::MaterialRecord &material =
          compositor->AnalyseMaterial(a_bound.inputs.material);
      !material.sample) {
    logger::warn("material of '{}': {}", a_bound.name, material.problem);
  }
}
}

void Manager::RequestMesh(RE::FormID a_actorID, std::string a_geometry) {
  PostTask([this, a_actorID, name = std::move(a_geometry)] {
    const auto it = applied_.find(a_actorID);
    if (it == applied_.end()) {
      return;
    }
    for (const LiveGeometry *bound : GeometriesNamed(it->second, name)) {
      if (bound) {
        ReportMeshProblems(*bound);
      }
    }
  });
}
}
