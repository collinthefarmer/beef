#pragma once

#include "PCH.h"
#include "Snapshot.h"

namespace WornEnchantmentPBR
{
	// SKSE Menu Framework pages (no-op when the framework is not installed).
	void RegisterMenu();

	// The header every page but the studio starts with: the status line.
	void RenderHeader(const Studio::Snapshot& a_snapshot);

	// The status line.
	void RenderStatus(const Studio::Snapshot& a_snapshot);
}
