#include "recipe/Expression.h"
#include "test_support.h"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace {
float Scalar(const char *a_text) {
  auto program = Program::Parse(a_text);
  Check(program.has_value(), std::string{"parses: "} + a_text);
  if (!program) {
    return std::numeric_limits<float>::quiet_NaN();
  }
  return AsScalar(program->Evaluate(Program::Inputs{}));
}

bool Rejected(const char *a_text) {
  return !Program::Parse(a_text).has_value();
}

std::optional<ValueType> AllScalars(std::string_view) {
  return ValueType::kScalar;
}
}

int main() {
  Check(kMaxExpressionDepth == 32, "the expression depth bound is declared");
  Check(kMaxExpressionOps == 256, "the expression op bound is declared");
  Check(kMaxExpressionLength == 4096,
        "the expression length bound is declared");

  const Program empty;
  Check(empty.OpCount() == 0, "a default program has no code");
  Check(empty.Constant(), "a default program reads no rows");
  Check(!empty.UsesTime() && !empty.UsesX() && !empty.UsesMean(),
        "a default program uses no free variables");

  Check(Near(Scalar("1 + 2 * 3"), 7.0f), "multiply binds tighter than add");
  Check(Near(Scalar("(1 + 2) * 3"), 9.0f), "parentheses group");
  Check(Near(Scalar("2 - 3 - 4"), -5.0f), "subtraction is left associative");
  Check(Near(Scalar("-5"), -5.0f), "unary minus");
  Check(Near(Scalar("- -5"), 5.0f), "double negation");
  Check(Near(Scalar("10 / 4"), 2.5f), "division");
  Check(Near(Scalar("1 / 0"), 0.0f), "division by zero is zero");
  Check(Near(Scalar("5 / (2 - 2)"), 0.0f),
        "division by a computed zero is zero");
  Check(Near(Scalar("pi"), 3.14159265f), "pi is a constant");

  Check(Near(Scalar("abs(-3)"), 3.0f), "abs");
  Check(Near(Scalar("min(2, 5)"), 2.0f), "min");
  Check(Near(Scalar("max(2, 5)"), 5.0f), "max");
  Check(Near(Scalar("clamp(5, 0, 1)"), 1.0f), "clamp above");
  Check(Near(Scalar("clamp(-2, 0, 1)"), 0.0f), "clamp below");
  Check(Near(Scalar("saturate(2)"), 1.0f), "saturate clamps high");
  Check(Near(Scalar("saturate(-1)"), 0.0f), "saturate clamps low");
  Check(Near(Scalar("floor(2.7)"), 2.0f), "floor");
  Check(Near(Scalar("ceil(2.1)"), 3.0f), "ceil");
  Check(Near(Scalar("frac(2.25)"), 0.25f), "frac");
  Check(Near(Scalar("sqrt(9)"), 3.0f), "sqrt");
  Check(Near(Scalar("sqrt(-4)"), 0.0f), "sqrt of a negative is zero");
  Check(Near(Scalar("pow(2, 3)"), 8.0f), "pow");
  Check(Near(Scalar("sin(0)"), 0.0f), "sin");
  Check(Near(Scalar("cos(0)"), 1.0f), "cos");
  Check(Near(Scalar("step(0.5, 0.7)"), 1.0f), "step above edge");
  Check(Near(Scalar("step(0.5, 0.2)"), 0.0f), "step below edge");
  Check(Near(Scalar("smoothstep(0, 1, 0.5)"), 0.5f), "smoothstep midpoint");
  Check(Near(Scalar("lerp(0, 10, 0.5)"), 5.0f), "lerp");
  Check(Near(Scalar("if(1, 2, 3)"), 2.0f), "if takes the true branch");
  Check(Near(Scalar("if(0, 2, 3)"), 3.0f), "if takes the false branch");

  Check(Near(Scalar("3 < 5"), 1.0f), "less than");
  Check(Near(Scalar("3 > 5"), 0.0f), "greater than");
  Check(Near(Scalar("3 <= 3"), 1.0f), "less or equal");
  Check(Near(Scalar("4 >= 5"), 0.0f), "greater or equal");
  Check(Near(Scalar("2 == 2"), 1.0f), "equal");
  Check(Near(Scalar("2 != 2"), 0.0f), "not equal");
  Check(Near(Scalar("1 and 0"), 0.0f), "and");
  Check(Near(Scalar("1 or 0"), 1.0f), "or");
  Check(Near(Scalar("not 0"), 1.0f), "not of false");
  Check(Near(Scalar("not 5"), 0.0f), "not of true");

  {
    auto program = Program::Parse("[1, 2, 3] + [4, 5, 6]");
    Check(program.has_value(), "a vector sum parses");
    const Vec3 v = AsVec3(program->Evaluate(Program::Inputs{}));
    Check(v == Vec3{5.0f, 7.0f, 9.0f}, "vectors add component-wise");
  }
  {
    auto program = Program::Parse("2 * [1, 2, 3]");
    Check(program.has_value(), "a scalar times a vector parses");
    const Vec3 v = AsVec3(program->Evaluate(Program::Inputs{}));
    Check(v == Vec3{2.0f, 4.0f, 6.0f}, "a scalar broadcasts over a vector");
  }
  {
    auto program = Program::Parse("[3, 4]");
    Check(program.has_value(), "a two-component vector parses");
    const Vec2 v = AsVec2(program->Evaluate(Program::Inputs{}));
    Check(v == Vec2{3.0f, 4.0f}, "a vec2 literal");
  }
  {
    auto program = Program::Parse("sin([0, 0, 0])");
    Check(program.has_value(), "a unary function over a vector parses");
    const Vec3 v = AsVec3(program->Evaluate(Program::Inputs{}));
    Check(v == Vec3{0.0f, 0.0f, 0.0f}, "unary functions map over components");
  }

  {
    auto program = Program::Parse("@a + @b");
    Check(program.has_value(), "a program that reads rows parses");
    Check(program->References().size() == 2, "two distinct rows are interned");
    Check(!program->Constant(), "a program that reads rows is not constant");
    const std::vector<Value> refs{Value{3.0f}, Value{4.0f}};
    Program::Inputs in;
    in.refs = refs;
    Check(Near(AsScalar(program->Evaluate(in)), 7.0f),
          "row values feed the evaluator in interned order");
  }
  {
    auto program = Program::Parse("time + 1");
    Check(program.has_value() && program->UsesTime(),
          "time is a free variable");
    Program::Inputs in;
    in.time = 5.0f;
    Check(Near(AsScalar(program->Evaluate(in)), 6.0f),
          "time feeds the evaluator");
  }

  {
    auto curve = Program::Parse("x * 10");
    Check(curve.has_value() && curve->UsesX(), "a curve uses x");
    auto program = Program::Parse("@f(2)");
    Check(program.has_value() && program->Curves().size() == 1,
          "a curve call is interned");
    const std::vector<const Program *> curves{&curve.value()};
    Program::Inputs in;
    in.curves = curves;
    Check(Near(AsScalar(program->Evaluate(in)), 20.0f),
          "a curve call applies the curve to its argument");
  }
  {
    auto curve = ParseCurve("x * 2");
    Check(curve.has_value() && !curve->UsesMean(),
          "an x-only curve needs no source mean");
    Check(Near(ApplyCurve(curve.value(), 3.0f, 0.1f),
               ApplyCurve(curve.value(), 3.0f, 0.9f)),
          "an x-only curve is independent of the source mean");
    Check(Near(ApplyCurve(curve.value(), 3.0f), 6.0f),
          "ApplyCurve evaluates the curve at x");
    auto meanCurve = ParseCurve("mean * 2");
    Check(meanCurve.has_value() && meanCurve->UsesMean(),
          "a curve may read mean");
    Check(Near(ApplyCurve(meanCurve.value(), 0.0f, 0.25f), 0.5f),
          "ApplyCurve passes mean through");
  }
  Check(!ParseCurve("@a").has_value(), "a curve may not read rows");
  Check(!ParseCurve("@f(x)").has_value(), "a curve may not call a curve");

  {
    auto scalar = Program::Parse("1 + 2");
    Check(scalar.has_value(), "a scalar program parses");
    auto type = scalar->Check(AllScalars);
    Check(type.has_value() && *type == ValueType::kScalar,
          "a scalar program checks as scalar");
  }
  {
    auto vec = Program::Parse("[1, 2, 3]");
    auto type = vec->Check(AllScalars);
    Check(type.has_value() && *type == ValueType::kVec3,
          "a vec3 literal checks as vec3");
  }
  {
    auto mixed = Program::Parse("[1, 2] + [1, 2, 3]");
    Check(mixed.has_value(), "a size-mismatched vector sum still parses");
    Check(!mixed->Check(AllScalars).has_value(),
          "mixing vec2 with vec3 fails the check");
  }
  {
    auto ref = Program::Parse("@row");
    auto asVec3 = [](std::string_view) -> std::optional<ValueType> {
      return ValueType::kVec3;
    };
    auto type = ref->Check(asVec3);
    Check(type.has_value() && *type == ValueType::kVec3,
          "a row takes the type the typer reports");
    auto unknown = [](std::string_view) -> std::optional<ValueType> {
      return std::nullopt;
    };
    Check(!ref->Check(unknown).has_value(), "an unknown row fails the check");
  }
  {
    auto badVector = Program::Parse("[[1, 2], 3]");
    Check(badVector.has_value(), "a nested-vector component still parses");
    Check(!badVector->Check(AllScalars).has_value(),
          "vector components must be scalars");
  }

  {
    auto program = Program::Parse("time + x + mean + @a + @b(x)");
    Check(program.has_value(), "a program using every free variable parses");
    Check(program->UsesTime() && program->UsesX() && program->UsesMean(),
          "free-variable flags are set");
    Check(program->References().size() == 1 && program->Curves().size() == 1,
          "rows and curves are separated");
    Check(!program->Constant(), "such a program is not constant");
  }
  Check(Program::Parse("1 + 2").value().Constant(),
        "a literal program is constant");

  Check(Rejected(""), "an empty expression is rejected");
  Check(Rejected("   "), "whitespace-only is rejected");
  Check(Rejected("^"), "a bare caret is rejected");
  Check(Rejected("2 ^ 3"), "there is no power operator");
  Check(Rejected("(1 + 2"), "an unbalanced open paren is rejected");
  Check(Rejected("1 + 2)"), "an unbalanced close paren is rejected");
  Check(Rejected("1.2.3"), "a malformed number is rejected");
  Check(Rejected("@"), "a bare reference marker is rejected");
  Check(Rejected("foo"), "an unknown name is rejected");
  Check(Rejected("abs"), "a function without arguments is rejected");
  Check(Rejected("abs(1, 2)"), "wrong arity is rejected");
  Check(Rejected("#$%"), "garbage characters are rejected");
  Check(Rejected("[1, 2, 3, 4]"), "a four-component vector is rejected");
  Check(Rejected("[1]"), "a one-component vector is rejected");
  Check(Rejected("1 2"), "two adjacent atoms are rejected");

  {
    const std::string tooLong(kMaxExpressionLength + 1, '1');
    Check(Rejected(tooLong.c_str()),
          "an over-long expression is rejected by the length bound");
  }
  {
    std::string deep;
    for (std::size_t i = 0; i < kMaxExpressionDepth + 8; ++i) {
      deep += '(';
    }
    deep += '1';
    for (std::size_t i = 0; i < kMaxExpressionDepth + 8; ++i) {
      deep += ')';
    }
    Check(Rejected(deep.c_str()),
          "an over-nested expression is rejected by the depth bound");
  }
  {
    std::string many = "1";
    for (std::size_t i = 0; i < kMaxExpressionOps; ++i) {
      many += " + 1";
    }
    auto program = Program::Parse(many);
    Check(!program.has_value(),
          "an over-long op sequence is rejected by the op bound");
    if (program) {
      Check(program->OpCount() <= kMaxExpressionOps,
            "no accepted program exceeds the op bound");
    }
  }

  {
    Check(ExpressionSummary("@a + @b") == "@a + @b",
          "a short expression is returned unchanged");
    Check(ExpressionSummary("  @a\n +\t @b  ") == "@a + @b",
          "runs of whitespace collapse to single spaces and trim");
    const std::string summary =
        ExpressionSummary("@source * 0.5 + @other * 0.25 + @third", 20);
    Check(summary.find("\xe2\x80\xa6") != std::string::npos &&
              summary.starts_with("@source") && summary.ends_with("@third"),
          "a long expression elides its middle, keeping head and tail");
    Check(ExpressionSummary("", 20).empty(),
          "an empty expression summarises to empty");
  }

  return test::Finish("expression");
}
