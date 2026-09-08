#include "History.h"

namespace WornEnchantmentPBR::Studio
{
	void EditHistory::Push(Recipe a_before)
	{
		past_.push_back(std::move(a_before));
		if (past_.size() > kCap) {
			past_.pop_front();
		}
		future_.clear();
	}

	std::optional<Recipe> EditHistory::Undo(const Recipe& a_current)
	{
		if (past_.empty()) {
			return std::nullopt;
		}
		Recipe restored = std::move(past_.back());
		past_.pop_back();
		future_.push_back(a_current);
		return restored;
	}

	std::optional<Recipe> EditHistory::Redo(const Recipe& a_current)
	{
		if (future_.empty()) {
			return std::nullopt;
		}
		Recipe restored = std::move(future_.back());
		future_.pop_back();
		past_.push_back(a_current);
		if (past_.size() > kCap) {
			past_.pop_front();
		}
		return restored;
	}

	void EditHistory::Rename(std::string_view a_id)
	{
		for (auto& recipe : past_) {
			recipe.id = a_id;
		}
		for (auto& recipe : future_) {
			recipe.id = a_id;
		}
	}

	void EditHistory::Clear() noexcept
	{
		past_.clear();
		future_.clear();
	}
}
