#include "Region.h"
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
		Check(TermOpName(TermOp::kNot) == "not" && ParseTermOp("or") == TermOp::kOr && !ParseTermOp("xor"), "op names");
	}
}

int main()
{
	Builds();
	return test::Finish("region");
}
