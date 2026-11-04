// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "engine/LiveActor.h"
#include "recipe/Recipe.h"
#include "render/Binding.h"
#include "render/TextureRef.h"

#include <array>

namespace BetterEnchantmentEffects {
struct SlotWrite {
  Slot slot = Slot::kEmissive;
  TextureRef texture;
  bool shown = false;
  std::array<float, kScalarFieldCount> scalars{};
  Vec3 color{1.0f, 1.0f, 1.0f};

  [[nodiscard]] float Of(ScalarField a_field) const {
    return scalars[IndexOf(a_field)];
  }
  [[nodiscard]] float Shown(ScalarField a_field) const {
    return shown ? Of(a_field) : 0.0f;
  }
};

[[nodiscard]] SlotWrite EmptyWrite(Slot a_slot);
void WriteSlot(SlotTarget &a_target, const SlotWrite &a_write);
void ConcealStackSlots(LiveActor &a_state);
}
