#include "engine/Clock.h"

#include "PCH.h"

namespace BetterEnchantmentEffects {
std::uint32_t NowMS() { return RE::GetDurationOfApplicationRunTime(); }
}
