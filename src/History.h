#pragma once

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

		void Push(Recipe a_before);
		[[nodiscard]] std::optional<Recipe> Undo(const Recipe& a_current);
		[[nodiscard]] std::optional<Recipe> Redo(const Recipe& a_current);
		void                                Clear() noexcept;

		[[nodiscard]] std::size_t UndoDepth() const noexcept { return past_.size(); }
		[[nodiscard]] std::size_t RedoDepth() const noexcept { return future_.size(); }

	private:
		std::deque<Recipe> past_;
		std::deque<Recipe> future_;
	};
}
