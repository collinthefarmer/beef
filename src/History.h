#pragma once

// Undo and redo for one recipe as a history of whole recipes: a recipe is a
// small plain record, so every edit pushes the recipe as it was, and undo
// restores it through the same path as any edit. Capped, so a long session
// cannot grow without bound. Engine-free; the manager holds one per recipe.

#include "Recipe.h"

#include <cstddef>
#include <deque>
#include <optional>

namespace WornEnchantmentPBR::Studio
{
	class EditHistory
	{
	public:
		static constexpr std::size_t kCap = 100;

		// The recipe as it was before an edit; forgets any redo.
		void Push(Recipe a_before);
		// The recipe to restore, given the current one (which becomes the
		// redo); none when there is nothing to undo.
		[[nodiscard]] std::optional<Recipe> Undo(const Recipe& a_current);
		[[nodiscard]] std::optional<Recipe> Redo(const Recipe& a_current);
		void                                Clear() noexcept;

		[[nodiscard]] std::size_t UndoDepth() const noexcept { return past_.size(); }
		[[nodiscard]] std::size_t RedoDepth() const noexcept { return future_.size(); }

	private:
		std::deque<Recipe> past_;    // oldest first
		std::deque<Recipe> future_;  // the next redo last
	};
}
