#pragma once

#include "PCH.h"

namespace WornEnchantmentPBR
{
	// plugin.c 2257-2334: TESEquipEvent, TESObjectLoadedEvent, NiNodeUpdateEvent.
	void RegisterEventSinks();

	// The actor's animation graph events reach the bus as `anim.<tag>` with the
	// event's payload string as the trigger's arg. Watching is per actor and
	// is renewed on every apply, because a reloaded 3D brings a new graph.
	void WatchAnimationEvents(RE::Actor* a_actor);
	void UnwatchAnimationEvents(RE::Actor* a_actor);
}
