#pragma once

#include "PCH.h"
#include "Snapshot.h"

namespace WornEnchantmentPBR
{
	void RegisterMenu();

	void RenderHeader(const Studio::Snapshot& a_snapshot);

	void RenderStatus(const Studio::Snapshot& a_snapshot);
}
