#pragma once

#include "PCH.h"
#include "Snapshot.h"

namespace WornEnchantmentPBR
{
	// SKSE Menu Framework pages (no-op when the framework is not installed).
	void RegisterMenu();

	// The header every page but the studio starts with: the status line.
	void RenderHeader(const Studio::Snapshot& a_snapshot);

	// The status line, and the controls the studio places in its own
	// tables: the piece and recipe selection (kept in Studio::State), freeze
	// and scrub, isolate this recipe. Each draws one control; the caller
	// sets the item width where one applies.
	void RenderStatus(const Studio::Snapshot& a_snapshot);
	void SelectionCombo(const Studio::Snapshot& a_snapshot, const char* a_label);
	void RecipeCombo(const Studio::PieceRow* a_piece, const char* a_label);
	void IsolateCheckbox(const Studio::RecipeRow* a_recipe, const char* a_label);
	void FreezeCheckbox(const Studio::RecipeRow* a_recipe, const char* a_label);
	// The scrubber shows the recipe's clock while it runs and the scrub while
	// frozen; grabbing it freezes.
	void ScrubSlider(const Studio::RecipeRow* a_recipe, const char* a_label);
}
