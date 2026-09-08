#include "TermStack.h"
#include "Expression.h"
#include "test_support.h"

#include <string>

using namespace WornEnchantmentPBR;
using namespace WornEnchantmentPBR::Studio;
using test::Check;

namespace
{
	Term T(TermOp a_op, const char* a_text)
	{
		return Term{ a_op, a_text, {} };
	}

	void RoundTrips(const std::vector<Term>& a_terms, const std::string& a_what)
	{
		const auto built = BuildRegion(a_terms);
		const auto parsed = ParseRegion(built);
		Check(parsed.has_value(), a_what + ": parses");
		if (!parsed) {
			return;
		}
		Check(*parsed == a_terms, a_what + ": round trips from '" + built + "'");
		Check(BuildRegion(*parsed) == built, a_what + ": builds the same text again");
		Check(Program::Parse(built).has_value(), a_what + ": the built text is a valid expression");
	}

	void Builds()
	{
		Check(BuildRegion({}).empty(), "no terms build nothing");
		Check(BuildRegion(std::vector{ T(TermOp::kSet, "@a") }) == "@a", "one term is bare");
		Check(BuildRegion(std::vector{ T(TermOp::kSet, "@a * @b") }) == "@a * @b", "one compound term is bare");
		Check(BuildRegion(std::vector{ T(TermOp::kSet, "@a"), T(TermOp::kAnd, "@b") }) == "(@a) * (@b)", "and is the product");
		Check(BuildRegion(std::vector{ T(TermOp::kSet, "@a"), T(TermOp::kOr, "@b") }) == "max((@a), (@b))", "or is max");
		Check(BuildRegion(std::vector{ T(TermOp::kSet, "@a"), T(TermOp::kNot, "@b") }) == "(@a) * (1 - (@b))", "not is the complement");
		Check(BuildRegion(std::vector{ T(TermOp::kSet, "@a"), T(TermOp::kSet, "@b") }) == "(@a) * (@b)", "a set past the first reads as and");
		Check(BuildRegion(std::vector{ T(TermOp::kSet, "@a"), T(TermOp::kOr, "@b"), T(TermOp::kAnd, "@c") }) == "max((@a), (@b)) * (@c)", "and after or");
		Check(BuildRegion(std::vector{ T(TermOp::kSet, "@a"), T(TermOp::kAnd, "@b"), T(TermOp::kOr, "@c") }) == "max((@a) * (@b), (@c))", "or after and");

		const std::vector terms{ T(TermOp::kSet, "@a"), T(TermOp::kAnd, "@b"), T(TermOp::kNot, "@c") };
		Check(BuildRegion(terms, 1) == "@b", "solo shows one term alone");
		Check(BuildRegion(terms, std::nullopt, { 0 }) == "(@b) * (1 - (@c))", "a muted first term drops out and the next leads");
		Check(BuildRegion(terms, std::nullopt, { 0, 1, 2 }).empty(), "everything muted builds nothing");
		Check(BuildRegion(terms, 1, { 1 }) == "@b", "solo wins over a mute on the same term");
		Check(BuildRegion(terms, terms.size()).empty(), "a solo past the end shows nothing");
		Check(BuildRegion(std::vector{ T(TermOp::kSet, ""), T(TermOp::kAnd, "@b") }) == "@b", "an empty term is skipped");
		Check(BuildRegion(std::vector{ T(TermOp::kSet, "(@a)") }) == "(@a)", "a wrapped single term keeps its own parentheses");

		std::vector<Term> longTerms;
		for (std::size_t i = 0; i < kMaxTerms; ++i) {
			longTerms.push_back(Term{ TermOp::kAnd, "@" + std::string(120, 'n'), {} });
		}
		const auto capped = BuildRegion(longTerms);
		Check(!capped.empty() && capped.size() <= kMaxExpressionLength, "sixty-four long terms build to text within the expression length");
		Check(Program::Parse(capped).has_value(), "the capped text is a valid expression");
		const auto cappedTerms = ParseRegion(capped);
		Check(cappedTerms && cappedTerms->size() > 1 && cappedTerms->size() < kMaxTerms, "the capped text parses back to the terms that fit");
	}

	void Parses()
	{
		RoundTrips({ T(TermOp::kSet, "@a") }, "one term");
		RoundTrips({ T(TermOp::kSet, "@partition * @bones") }, "one compound term");
		RoundTrips({ T(TermOp::kSet, "@a"), T(TermOp::kAnd, "@b") }, "and");
		RoundTrips({ T(TermOp::kSet, "@a"), T(TermOp::kOr, "@b") }, "or");
		RoundTrips({ T(TermOp::kSet, "@a"), T(TermOp::kNot, "@b") }, "not");
		RoundTrips({ T(TermOp::kSet, "@partition * @bones"), T(TermOp::kAnd, "(1 - @metallic) * smoothstep(0.35, 0.6, @roughness)"), T(TermOp::kNot, "smoothstep(0.2, 0.5, @slope) * (1 - smoothstep(0.4, 0.6, @relief))"), T(TermOp::kOr, "@straps") }, "the brief's four");
		RoundTrips({ T(TermOp::kSet, "max(@a, @b)"), T(TermOp::kAnd, "min(@c, @d)") }, "terms that call max and min themselves");
		RoundTrips({ T(TermOp::kSet, "@a"), T(TermOp::kOr, "@b"), T(TermOp::kOr, "@c"), T(TermOp::kAnd, "@d"), T(TermOp::kNot, "@e") }, "a longer chain");

		const auto raw = ParseRegion("@a + @b");
		Check(raw && raw->size() == 1 && raw->front() == T(TermOp::kSet, "@a + @b"), "a hand-written sum is one raw term");
		const auto bare = ParseRegion("max(@a, @b)");
		Check(bare && bare->size() == 1 && bare->front() == T(TermOp::kSet, "max(@a, @b)"), "a bare max is one raw term, not an or");
		const auto product = ParseRegion("@a * (1 - @b)");
		Check(product && product->size() == 1 && product->front() == T(TermOp::kSet, "@a * (1 - @b)"), "a bare product is one raw term: only wrapped groups make a chain");
		const auto empty = ParseRegion("");
		Check(empty && empty->size() == 1 && empty->front().text.empty(), "empty text is one empty term");
		const auto unbalanced = ParseRegion("(@a * (@b)");
		Check(unbalanced && unbalanced->size() == 1 && unbalanced->front().text == "(@a * (@b)", "unbalanced parentheses are one raw term");
		const auto closeOnly = ParseRegion(")");
		Check(closeOnly && closeOnly->size() == 1, "a lone close is one raw term");
		Check(!ParseRegion(std::string(kMaxExpressionLength + 1, 'a')), "text past the expression length is refused");

		std::string chain = "(@t0)";
		for (std::size_t i = 1; i <= kMaxTerms + 4; ++i) {
			chain += " * (@t" + std::to_string(i) + ")";
		}
		const auto capped = ParseRegion(chain);
		Check(capped && capped->size() == 1, "a chain past the term cap loads as one raw term");

		Check(TermOpName(TermOp::kNot) == "not" && ParseTermOp("or") == TermOp::kOr && !ParseTermOp("xor"), "op names");
	}
}

int main()
{
	Builds();
	Parses();
	return test::Finish("mask stack");
}
