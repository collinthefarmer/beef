#pragma once

#include "PCH.h"
#include "Snapshot.h"

namespace BetterEnchantmentEffects
{
	void RegisterMenu();

	void RenderHeader(const Studio::Snapshot& a_snapshot);

	void RenderStatus(const Studio::Snapshot& a_snapshot);
}
