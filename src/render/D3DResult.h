#pragma once

#include "PCH.h"

#include <cstdint>

namespace BetterEnchantmentEffects {
[[nodiscard]] constexpr bool Failed(std::int32_t a_hr) noexcept {
  return a_hr < 0;
}

[[nodiscard]] inline RE::NiTexture::RendererData *
DataOf(RE::NiSourceTexture *a_texture) {
  return a_texture ? reinterpret_cast<RE::NiTexture::RendererData *>(
                         a_texture->rendererTexture)
                   : nullptr;
}
}
