// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <optional>
#include <string_view>

namespace RE {
class Setting;
}

namespace BetterEnchantmentEffects {
inline constexpr std::string_view kVanityDelaySetting =
    "fAutoVanityModeDelay:Camera";
inline constexpr float kVanityNever = 1.0e9f;

struct IdleCameraHold {
  float delay = 0.0f;
};

[[nodiscard]] RE::Setting *VanityDelay();
[[nodiscard]] std::optional<IdleCameraHold> HoldIdleCamera();
void ReleaseIdleCamera(const IdleCameraHold &a_hold);
}
