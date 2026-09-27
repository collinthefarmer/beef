// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once
#include "planners/TextureIdentity.h"
#include "render/Compositor.h"

namespace BetterEnchantmentEffects {
[[nodiscard]] bool IsNonPlaceholderTexture(const TextureRef &a_texture);
[[nodiscard]] TextureRef MaterialTexture(MaterialMap a_map,
                                         const MaterialInputs &a_material);
}
