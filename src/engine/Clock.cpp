// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/Clock.h"

#include "PCH.h"

namespace BetterEnchantmentEffects {
std::uint32_t NowMS() { return RE::GetDurationOfApplicationRunTime(); }
}
