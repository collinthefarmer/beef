#include "studio/Mask.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
Term Termed(TermOp a_op, std::string a_text) {
  Term term;
  term.op = a_op;
  term.text = std::move(a_text);
  return term;
}
}

int main() {
  Check(TermKindName(TermKind{RawTerm{}}) == "raw" &&
            TermKindName(TermKind{PresetTerm{"x"}}) == "preset" &&
            TermKindName(TermKind{ClusterTerm{}}) == "cluster",
        "TermKindName names each alternative");

  Check(TermOpName(TermOp::kOr) == "or" && ParseTermOp("and") == TermOp::kAnd &&
            ParseTermOp("nope") == std::nullopt,
        "TermOpName and ParseTermOp round-trip");

  const Term a = Termed(TermOp::kSet, "a");
  Check(BuildMask(std::vector<Term>{a}) == "a",
        "a single whole-wrapped term is unwrapped");

  const std::vector<Term> pair{a, Termed(TermOp::kAnd, "b")};
  Check(BuildMask(pair) == "(a) * (b)", "kAnd multiplies the operands");

  const std::vector<Term> ored{a, Termed(TermOp::kOr, "b")};
  Check(BuildMask(ored) == "max((a), (b))", "kOr takes the maximum");

  const std::vector<Term> notted{a, Termed(TermOp::kNot, "b")};
  Check(BuildMask(notted) == "(a) * (1 - (b))", "kNot subtracts the operand");

  Check(BuildMask(pair, std::optional<std::size_t>{0}) == "a",
        "solo keeps only the soloed term");
  Check(BuildMask(pair, std::nullopt, std::set<std::size_t>{1}) == "a",
        "a muted term is dropped");

  const std::vector<Term> withEmpty{a, Termed(TermOp::kAnd, "")};
  Check(BuildMask(withEmpty) == "a", "an empty term contributes nothing");

  return test::Finish("studio_mask");
}
