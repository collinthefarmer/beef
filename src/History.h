#pragma once

#include "Recipe.h"

#include <cstddef>
#include <deque>
#include <optional>
#include <string_view>
#include <utility>

namespace WornEnchantmentPBR::Studio
{
	template <class T>
	class History
	{
	public:
		static constexpr std::size_t kCap = 100;

		void Push(T a_before)
		{
			past_.push_back(std::move(a_before));
			if (past_.size() > kCap) {
				past_.pop_front();
			}
			future_.clear();
		}

		[[nodiscard]] std::optional<T> Undo(const T& a_current)
		{
			if (past_.empty()) {
				return std::nullopt;
			}
			T restored = std::move(past_.back());
			past_.pop_back();
			future_.push_back(a_current);
			return restored;
		}

		[[nodiscard]] std::optional<T> Redo(const T& a_current)
		{
			if (future_.empty()) {
				return std::nullopt;
			}
			T restored = std::move(future_.back());
			future_.pop_back();
			past_.push_back(a_current);
			if (past_.size() > kCap) {
				past_.pop_front();
			}
			return restored;
		}

		void Clear() noexcept
		{
			past_.clear();
			future_.clear();
		}

		void Rename(std::string_view a_id)
		{
			for (auto& value : past_) {
				value.id = a_id;
			}
			for (auto& value : future_) {
				value.id = a_id;
			}
		}

		[[nodiscard]] std::size_t UndoDepth() const noexcept { return past_.size(); }
		[[nodiscard]] std::size_t RedoDepth() const noexcept { return future_.size(); }

	private:
		std::deque<T> past_;
		std::deque<T> future_;
	};

	using EditHistory = History<Recipe>;
}
