#pragma once

#include "PCH.h"

namespace WornEnchantmentPBR
{
	// Per-frame driver: a vtable write on PlayerCharacter::Update. No
	// trampoline, no offsets; only the Address Library vtable entry.
	void InstallHooks();
}
