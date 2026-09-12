#pragma once

#include "engine/LiveActor.h"

#include <cstddef>
#include <cstdint>

namespace BetterEnchantmentEffects {
[[nodiscard]] std::uint32_t NowMS();
[[nodiscard]] SlotTarget *TargetFor(LiveGeometry &a_bound, Surface a_surface);
[[nodiscard]] PlacedOutput *OutputAt(LivePlacement &a_placement,
                                     std::size_t a_index);
}
