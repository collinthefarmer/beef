#pragma once

#include "PCH.h"

namespace BetterEnchantmentEffects
{
	void RegisterEventSinks();

	void WatchAnimationEvents(RE::Actor* a_actor);
	void UnwatchAnimationEvents(RE::Actor* a_actor);
}
