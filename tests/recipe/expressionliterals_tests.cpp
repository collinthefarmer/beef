// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Expression.h"
#include "test_support.h"

#include <array>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

int main() {
  {
    const std::string text = "- 2 + 2 * [1e-3, .5, --4]";
    const auto program = Program::Parse(text);
    Check(program.has_value(),
          "literal metadata uses the existing expression parser");
    if (program) {
      const auto literals = program->NumericLiterals();
      Check(literals.size() == 5,
            "every numeric occurrence has its own source span");
      if (literals.size() == 5) {
        const std::array<std::string_view, 5> tokens{"- 2", "2", "1e-3", ".5",
                                                     "--4"};
        const std::array<float, 5> values{-2.0f, 2.0f, 0.001f, 0.5f, 4.0f};
        for (std::size_t i = 0; i < literals.size(); ++i) {
          const NumericLiteral &literal = literals[i];
          Check(
              text.substr(literal.offset, literal.length) == tokens[i] &&
                  Near(literal.value, values[i]),
              "source spans preserve numeric spelling and direct unary signs");
        }
      }
    }
  }
  {
    const std::string text = "-(2 + 3) - 4 + not 5 + pi + time";
    const auto program = Program::Parse(text);
    Check(program && program->NumericLiterals().size() == 4,
          "named constants and variables are not editable numeric literals");
    if (program && program->NumericLiterals().size() == 4) {
      const auto literals = program->NumericLiterals();
      Check(Near(literals[0].value, 2.0f) && Near(literals[2].value, 4.0f) &&
                Near(literals[3].value, 5.0f),
            "group negation, subtraction, and logical not remain outside the "
            "literal");
    }
    Check(!Program::Parse("2 + )"),
          "invalid formulas cannot produce selectable operand metadata");
  }
  {
    const std::string text = " 2 + 2 * 2 ";
    const NumericLiteralSelection selected{text, 1};
    const auto replaced = ReplaceNumericLiteral(text, selected, "3 + 4");
    Check(replaced && *replaced == " 2 + (3 + 4) * 2 ",
          "replacement changes one occurrence and preserves all surrounding "
          "text");
    if (replaced) {
      const auto program = Program::Parse(*replaced);
      Check(program && Near(AsScalar(program->Evaluate({})), 16.0f),
            "replacement grouping preserves the original operator context");
    }
    Check(!ReplaceNumericLiteral("2 + 2 * 2", selected, "4"),
          "even whitespace changes invalidate an old operand selection");
    Check(!ReplaceNumericLiteral(text, NumericLiteralSelection{text, 99}, "4"),
          "missing operand indices are rejected before indexing");
    Check(!ReplaceNumericLiteral(text, selected, "@"),
          "malformed replacement expressions are refused");
    Check(!ReplaceNumericLiteral(text, selected, ""),
          "an operand cannot be replaced by empty text");
  }
  {
    const std::string text = "- 2 + 2 * 3";
    const auto original = Program::Parse(text);
    const auto replaced = ReplaceNumericLiteral(
        text, NumericLiteralSelection{text, 0}, "@strength");
    Check(original && replaced,
          "a directly signed literal can be promoted to a reference");
    if (original && replaced) {
      const auto promoted = Program::Parse(*replaced);
      const std::array<Value, 1> values{
          original->NumericLiterals().front().value};
      Check(
          promoted && promoted->References().size() == 1 &&
              promoted->References().front() == "strength" &&
              Near(AsScalar(promoted->Evaluate({.refs = values, .curves = {}})),
                   AsScalar(original->Evaluate({}))),
          "promoting a negative operand keeps its sign exactly once");
      Check(*replaced == "@strength + 2 * 3",
            "promotion leaves equal literals elsewhere unchanged");
    }
  }
  {
    std::string expression = "-2 * 3";
    for (std::size_t i = 0; i < 100; ++i) {
      const auto changed = ReplaceNumericLiteral(
          expression, NumericLiteralSelection{expression, 0}, "-4");
      Check(changed && *changed == "-4 * 3",
            "repeated numeric tuning does not accumulate grouping or hit the "
            "depth limit");
      if (changed) {
        expression = *changed;
      }
    }
  }
  {
    const std::string text = "1";
    const std::string longReplacement(kMaxExpressionLength + 1, '1');
    Check(!ReplaceNumericLiteral(text, NumericLiteralSelection{text, 0},
                                 longReplacement),
          "replacement respects the parser's expression length limit");
    const auto curve = ParseCurve("-2 * x + 3");
    Check(curve && curve->NumericLiterals().size() == 2,
          "curve parsing shares numeric source metadata");
  }
  return test::Finish("expression literals");
}
