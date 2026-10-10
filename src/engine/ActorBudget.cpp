// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/ActorBudget.h"

#include "engine/ActorResize.h"
#include "render/RenderInstance.h"

#include <algorithm>
#include <memory>

namespace BetterEnchantmentEffects {
const std::vector<HeldTarget> &HeldNow(const LiveActor &a_state,
                                       const PlanTargets &a_targets) noexcept {
  return a_state.holding == IntermediateHolding::kReleased
             ? a_targets.whenReleased
             : a_targets.withIntermediates;
}

std::vector<const RenderInstance *> InstancesOf(const LiveActor &a_state) {
  std::vector<const RenderInstance *> instances;
  for (const LivePiece &piece : a_state.pieces) {
    for (const LiveGeometry &geometry : piece.geometries) {
      if (geometry.inputs.render &&
          !std::ranges::contains(instances, geometry.inputs.render.get())) {
        instances.push_back(geometry.inputs.render.get());
      }
    }
  }
  if (const RenderInstance *resizing = ResizingInstance(a_state)) {
    instances.push_back(resizing);
  }
  return instances;
}

std::size_t ReleaseIntermediates(LiveActor &a_state) {
  const std::shared_ptr<RenderInstance> render = AttachedRender(a_state);
  if (!render) {
    return 0;
  }
  a_state.holding = IntermediateHolding::kReleased;
  return render->ReleaseIntermediates();
}
}
