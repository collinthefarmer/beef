// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/LiveActor.h"
#include "test_support.h"

#include <memory>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  LiveActor actor;
  actor.instances.resize(1);
  actor.instances[0].light = std::make_unique<LightBinding>();
  actor.plan.placements.resize(2);
  actor.placements.resize(2);
  for (auto &placement : actor.placements) {
    PlacedOutput output;
    output.active = true;
    output.rendered = true;
    output.stack = std::make_unique<RenderOutput>();
    output.stack->target = std::make_shared<int>(42);
    placement.outputs.push_back(std::move(output));
  }
  actor.pieces.resize(1);
  actor.pieces[0].geometries.resize(2);
  auto &lost = actor.pieces[0].geometries[0];
  auto &other = actor.pieces[0].geometries[1];
  lost.placements = {PlacementId{0}, PlacementId{99}};
  other.placements = {PlacementId{1}};
  lost.name = "lost";
  lost.geometry = std::make_shared<RE::BSGeometry>();
  lost.property = std::make_shared<RE::BSLightingShaderProperty>();
  lost.inputs.geometry = lost.geometry;
  lost.inputs.root = std::make_shared<RE::NiAVObject>();
  lost.inputs.material = std::make_shared<int>(1);
  lost.inputs.render = std::make_shared<int>(9);
  const std::weak_ptr geometry = lost.geometry;
  const std::weak_ptr root = lost.inputs.root;
  const std::weak_ptr material = lost.inputs.material;
  const std::weak_ptr render = lost.inputs.render;
  const std::weak_ptr stack = actor.placements[0].outputs[0].stack->target;
  const auto external = actor.placements[0].outputs[0].stack->target;
  const std::weak_ptr otherStack = actor.placements[1].outputs[0].stack->target;
  int detached = 0;
  const auto detach = [&] {
    Check(!stack.expired() && !render.expired() && !geometry.expired(),
          "bindings retire before their resource producers and geometry");
    ++detached;
  };
  lost.shell = std::make_unique<ShellBinding>();
  lost.shell->onDestroy = detach;
  lost.material = std::make_unique<MaterialBinding>();
  lost.material->onDestroy = detach;
  RetireGeometry(actor, lost);
  Check(detached == 2 && lost.lost && geometry.expired() && root.expired() &&
            material.expired() && render.expired(),
        "lost geometry drops its bindings, inputs, and cached resources");
  const auto &output = actor.placements[0].outputs[0];
  Check(!output.stack && !output.rendered && output.renderFailed &&
            output.active && !output.problem.empty() && lost.name == "lost",
        "retirement keeps diagnostic identity and reports the released output");
  Check(actor.placements.size() == 2 && lost.placements.size() == 2 &&
            ResolvePlacement(actor, PlacementId{1}) && !otherStack.expired() &&
            actor.instances[0].light && !other.lost,
        "retirement preserves sibling indices, resources, and shared instance "
        "lights");
  Check(!stack.expired() && *external == 42,
        "external consumers keep their lease after geometry retirement");
  RetireGeometry(actor, lost);
  Check(
      detached == 2,
      "repeated retirement is harmless and bounds-checks stale placement IDs");
  RetireActorEffects(actor);
  Check(actor.instances.empty() && actor.pieces.empty() &&
            actor.placements.empty() && otherStack.expired() &&
            !stack.expired(),
        "full retirement releases remaining state without revoking external "
        "leases");
  return test::Finish("engine_liveretirement");
}
