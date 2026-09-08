#pragma once

#include "PCH.h"

namespace WornEnchantmentPBR
{
	void RegisterEventSinks();

	void WatchAnimationEvents(RE::Actor* a_actor);
	void UnwatchAnimationEvents(RE::Actor* a_actor);
}
