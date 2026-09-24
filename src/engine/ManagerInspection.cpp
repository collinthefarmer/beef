// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Manager.h"

#include "render/Compositor.h"

#include <utility>

namespace BetterEnchantmentEffects {
void Manager::RequestMesh(RE::FormID a_actorID, std::string a_geometry) {
  PostTask([this, a_actorID, name = std::move(a_geometry)] {
    const auto it = applied_.find(a_actorID);
    if (it == applied_.end()) {
      return;
    }
    for (LivePiece &piece : it->second.pieces) {
      for (LiveGeometry &bound : piece.geometries) {
        if (bound.name != name || bound.lost) {
          continue;
        }
        Compositor *compositor = Compositor::GetSingleton();
        if (const std::expected<std::shared_ptr<MeshEntry>, std::string> mesh =
                compositor->MeshOf(bound.geometry.get());
            !mesh) {
          logger::warn("mesh '{}': {}", name, mesh.error());
        }
        if (const Compositor::MaterialRecord &material =
                compositor->AnalyseMaterial(bound.inputs.material);
            !material.sample) {
          logger::warn("material of '{}': {}", name, material.problem);
        }
      }
    }
  });
}
}
