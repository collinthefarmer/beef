#include "TermStack.h"

#include "Expression.h"

#include <format>

namespace WornEnchantmentPBR::Studio
{
	std::string_view TermOpName(TermOp a_op) noexcept
	{
		switch (a_op) {
		case TermOp::kSet:
			return "set";
		case TermOp::kAnd:
			return "and";
		case TermOp::kOr:
			return "or";
		case TermOp::kNot:
			return "not";
		}
		return "?";
	}

	std::optional<TermOp> ParseTermOp(std::string_view a_name) noexcept
	{
		for (const auto op : { TermOp::kSet, TermOp::kAnd, TermOp::kOr, TermOp::kNot }) {
			if (TermOpName(op) == a_name) {
				return op;
			}
		}
		return std::nullopt;
	}

	// ------------------------------------------------------------- build

	std::string BuildRegion(std::span<const Term> a_terms, std::optional<std::size_t> a_solo, const std::set<std::size_t>& a_muted)
	{
		std::string built;
		for (std::size_t i = 0; i < a_terms.size() && i < kMaxTerms; ++i) {
			const auto& term = a_terms[i];
			const bool  shown = a_solo ? *a_solo == i : !a_muted.contains(i);
			if (!shown || term.text.empty()) {
				continue;
			}
			std::string next;
			if (built.empty()) {
				next = std::format("({})", term.text);
			} else {
				switch (term.op) {
				case TermOp::kSet:
				case TermOp::kAnd:
					next = std::format("{} * ({})", built, term.text);
					break;
				case TermOp::kOr:
					next = std::format("max({}, ({}))", built, term.text);
					break;
				case TermOp::kNot:
					next = std::format("{} * (1 - ({}))", built, term.text);
					break;
				}
			}
			if (next.size() > kMaxExpressionLength) {
				break;
			}
			built = std::move(next);
		}
		// One term alone is written bare, so a kept single-ingredient region
		// reads as its ingredient.
		if (!built.empty() && built.front() == '(' && built.back() == ')') {
			std::size_t depth = 0;
			bool        whole = true;
			for (std::size_t i = 0; i + 1 < built.size(); ++i) {
				depth += built[i] == '(' ? 1 : 0;
				depth -= built[i] == ')' ? 1 : 0;
				if (depth == 0) {
					whole = false;
					break;
				}
			}
			if (whole) {
				built = built.substr(1, built.size() - 2);
			}
		}
		return built;
	}

	// ------------------------------------------------------------- parse

	namespace
	{
		// The index of the '(' that the ')' at a_close matches, or nothing.
		std::optional<std::size_t> OpenOf(std::string_view a_text, std::size_t a_close)
		{
			std::size_t depth = 0;
			for (std::size_t i = a_close + 1; i-- > 0;) {
				if (a_text[i] == ')') {
					++depth;
				} else if (a_text[i] == '(') {
					--depth;
					if (depth == 0) {
						return i;
					}
				}
			}
			return std::nullopt;
		}

		// The index of the ')' that the '(' at a_open matches, or nothing.
		std::optional<std::size_t> CloseOf(std::string_view a_text, std::size_t a_open)
		{
			std::size_t depth = 0;
			for (std::size_t i = a_open; i < a_text.size(); ++i) {
				if (a_text[i] == '(') {
					++depth;
				} else if (a_text[i] == ')') {
					--depth;
					if (depth == 0) {
						return i;
					}
				}
			}
			return std::nullopt;
		}

		// "(T)" with the parentheses matching each other: T, else nothing.
		std::optional<std::string_view> Wrapped(std::string_view a_text)
		{
			if (a_text.size() < 2 || a_text.front() != '(' || a_text.back() != ')') {
				return std::nullopt;
			}
			const auto close = CloseOf(a_text, 0);
			if (!close || *close != a_text.size() - 1) {
				return std::nullopt;
			}
			return a_text.substr(1, a_text.size() - 2);
		}

		constexpr std::string_view kAnd = " * ";
		constexpr std::string_view kNotOpen = "1 - (";
		constexpr std::string_view kOrOpen = "max(";

		// The chain as Build writes it, last operation first; nothing when
		// the text is not in that shape.
		bool ParseChain(std::string_view a_text, std::vector<Term>& a_out, std::size_t a_depth)
		{
			if (a_depth > kMaxTerms || a_text.empty()) {
				return false;
			}
			// or: max(chain, (T))
			if (a_text.starts_with(kOrOpen) && a_text.back() == ')' && CloseOf(a_text, kOrOpen.size() - 1) == a_text.size() - 1) {
				const auto  inner = a_text.substr(kOrOpen.size(), a_text.size() - kOrOpen.size() - 1);
				std::size_t depth = 0;
				for (std::size_t i = 0; i + 1 < inner.size(); ++i) {
					depth += inner[i] == '(' ? 1 : 0;
					depth -= inner[i] == ')' ? 1 : 0;
					if (depth == 0 && inner[i] == ',' && inner[i + 1] == ' ') {
						const auto right = Wrapped(inner.substr(i + 2));
						if (!right || !ParseChain(inner.substr(0, i), a_out, a_depth + 1)) {
							return false;
						}
						a_out.push_back(Term{ TermOp::kOr, std::string{ *right }, {} });
						return true;
					}
				}
				return false;
			}
			// set, and, not: the last group "(X)" and what precedes it
			if (a_text.back() != ')') {
				return false;
			}
			const auto open = OpenOf(a_text, a_text.size() - 1);
			if (!open) {
				return false;
			}
			const auto group = a_text.substr(*open + 1, a_text.size() - *open - 2);
			const auto prefix = a_text.substr(0, *open);
			if (prefix.empty()) {
				a_out.push_back(Term{ TermOp::kSet, std::string{ group }, {} });
				return true;
			}
			if (!prefix.ends_with(kAnd)) {
				return false;
			}
			const auto before = prefix.substr(0, prefix.size() - kAnd.size());
			if (group.starts_with(kNotOpen)) {
				if (const auto inner = Wrapped(group.substr(kNotOpen.size() - 1))) {
					if (!ParseChain(before, a_out, a_depth + 1)) {
						return false;
					}
					a_out.push_back(Term{ TermOp::kNot, std::string{ *inner }, {} });
					return true;
				}
			}
			if (!ParseChain(before, a_out, a_depth + 1)) {
				return false;
			}
			a_out.push_back(Term{ TermOp::kAnd, std::string{ group }, {} });
			return true;
		}
	}

	std::optional<std::vector<Term>> ParseRegion(std::string_view a_text)
	{
		if (a_text.size() > kMaxExpressionLength) {
			return std::nullopt;
		}
		std::vector<Term> terms;
		if (ParseChain(a_text, terms, 0) && terms.size() <= kMaxTerms) {
			return terms;
		}
		return std::vector<Term>{ Term{ TermOp::kSet, std::string{ a_text }, {} } };
	}
}
