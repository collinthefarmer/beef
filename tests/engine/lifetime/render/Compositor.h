#pragma once

#include "PCH.h"
#include <memory>

namespace BetterEnchantmentEffects {
struct RenderedStack {
  std::shared_ptr<int> target;
};
struct GeometryInputs {
  std::shared_ptr<int> material;
  RE::NiPointer<RE::BSGeometry> geometry;
  RE::NiPointer<RE::NiAVObject> root;
  std::shared_ptr<int> masks;
  std::shared_ptr<int> ripples;
  std::shared_ptr<int> derived;
};
}
