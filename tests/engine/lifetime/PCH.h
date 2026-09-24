// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>
#include <memory>

namespace RE {
using FormID = std::uint32_t;
using ActorHandle = std::uint32_t;
struct BSGeometry {};
struct BSLightingShaderProperty {};
struct NiAVObject {};
template <class T> using NiPointer = std::shared_ptr<T>;
}
