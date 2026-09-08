#pragma once

#include "TermKind.h"

#include <cstddef>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace WornEnchantmentPBR::Studio
{
	enum class TermOp
	{
		kSet,
		kAnd,
		kOr,
		kNot,
	};
	[[nodiscard]] std::string_view       TermOpName(TermOp a_op) noexcept;
	[[nodiscard]] std::optional<TermOp>  ParseTermOp(std::string_view a_name) noexcept;

	struct Term
	{
		TermOp             op = TermOp::kSet;
		std::string        text;
		std::string        label;
		TermKind         kind;
		[[nodiscard]] bool operator==(const Term&) const = default;
	};

	inline constexpr std::size_t kMaxTerms = 64;
	inline constexpr std::string_view kExpressionLabel = "expression";

	[[nodiscard]] std::string BuildRegion(std::span<const Term> a_terms, std::optional<std::size_t> a_solo = std::nullopt, const std::set<std::size_t>& a_muted = {});

}
