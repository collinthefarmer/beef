#pragma once

// The studio page: a target and slot picker over the stack and the
// inspector, drawn from the Studio view models under the current mode's
// layout; and the board, the grid of every slot on every surface, which
// the Recipes page draws as the overview of what a recipe writes. Each frame it takes
// the snapshot once, builds the view models from it and the page state,
// draws them through the widgets, and turns what the widgets return into
// RecipeEdit records for the manager or View and MenuState changes.

#include "Snapshot.h"

namespace WornEnchantmentPBR::Studio
{
	void __stdcall RenderStudio();

	// The board for the current selection, with the geometry it is viewed on.
	void DrawBoardPage(const Snapshot& a_snapshot);
}
