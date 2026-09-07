#pragma once

// A region as a stack of terms, the way a slot's output is a stack of
// layers: each term is one ingredient (a preset, a partition or bone, a
// mask or source, a raw expression) and each term after the first carries
// an op. On soft masks `and` is the product, `or` is max, and `not` is the
// product with the complement, so the stack builds to one expression, which
// is what the file holds; the stack itself is page state. Build writes a
// fixed shape and Parse reads only that shape back, so a hand-written
// expression loads as a single raw term. Engine-free.

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
		kSet,  // the first term; past the first it reads as `and`
		kAnd,
		kOr,
		kNot,
	};
	[[nodiscard]] std::string_view       TermOpName(TermOp a_op) noexcept;
	[[nodiscard]] std::optional<TermOp>  ParseTermOp(std::string_view a_name) noexcept;

	struct Term
	{
		TermOp             op = TermOp::kSet;
		std::string        text;   // the ingredient's expression
		std::string        label;  // where it came from: a preset's name, a @reference, or "expression"
		[[nodiscard]] bool operator==(const Term&) const = default;
	};

	inline constexpr std::size_t kMaxTerms = 64;
	// The label of a term that came from typed text rather than a preset or a reference.
	inline constexpr std::string_view kExpressionLabel = "expression";

	// The expression the stack builds. A soloed index shows that term alone;
	// muted indices and terms with empty text are left out. Empty when no
	// term is shown. The text never exceeds kMaxExpressionLength: a term
	// that would push it past stops the build, and what fits is returned,
	// so the built text always parses back.
	[[nodiscard]] std::string BuildRegion(std::span<const Term> a_terms, std::optional<std::size_t> a_solo = std::nullopt, const std::set<std::size_t>& a_muted = {});

	// The terms an expression in the built shape came from, labels empty;
	// any other expression comes back as one `set` term holding it whole.
	// Nothing for text past the expression length or the term cap.
	[[nodiscard]] std::optional<std::vector<Term>> ParseRegion(std::string_view a_text);
}
