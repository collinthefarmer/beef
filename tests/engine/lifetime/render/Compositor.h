// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include <memory>

namespace BetterEnchantmentEffects {
struct RenderOutput {
  std::shared_ptr<int> target;
};
struct GeometryInputs {
  std::shared_ptr<int> render;
  std::shared_ptr<int> material;
  RE::NiPointer<RE::BSGeometry> geometry;
  RE::NiPointer<RE::NiAVObject> root;
};
}
