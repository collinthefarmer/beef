// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "engine/LiveActor.h"

#include <unordered_map>

namespace BetterEnchantmentEffects {
void EmitTargetHolders(const std::unordered_map<RE::FormID, LiveActor> &a_live);
}
