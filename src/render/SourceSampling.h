#pragma once
#include "render/Compositor.h"

namespace BetterEnchantmentEffects {
[[nodiscard]] std::string ImageCacheKey(std::string_view a_path);
[[nodiscard]] bool IsNonPlaceholderTexture(const TextureRef &a_texture);
[[nodiscard]] TextureRef MaterialTexture(MaterialMap a_map,
                                         const MaterialInputs &a_material);
[[nodiscard]] TextureLab::LayerInput
ResolveSampling(const PreparedSource &a_source, const SignalState &a_signals);
}
