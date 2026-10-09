// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "planners/References.h"

namespace BetterEnchantmentEffects {
template <> struct IdentityShape<RE::NiSourceTexture> {
  using type = NamedTexture;
};
template <> struct IdentityShape<RE::BSGeometry> {
  using type = MeshInstall;
};
}
