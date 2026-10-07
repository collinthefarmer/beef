// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Expression.h"
#include "recipe/ExpressionMath.h"
#include "test_support.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <span>
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

std::string CallText(const ExpressionFunction &a_function) {
  return Match(
      a_function.form,
      [&](const FixedCall &a_call) {
        std::string text = std::string{a_function.name} + "(";
        for (int i = 0; i < a_call.arity; ++i) {
          text += i == 0 ? "1" : ", 1";
        }
        return text + ")";
      },
      [](const RandomCall &) { return std::string{"rand()"}; },
      [](const RandomRangeCall &) { return std::string{"randrange(0, 1)"}; });
}

void EveryTableFunctionParses() {
  for (const ExpressionFunction &function : ExpressionFunctions()) {
    const std::string text = CallText(function);
    Check(Program::Parse(text).has_value(), "the table call parses: " + text);
    Check(function.usage.starts_with(std::string{function.name} + "("),
          "the usage starts with the call: " + std::string{function.usage});
  }
  Check(Program::Parse("randrange(0, 1, 0.5)").has_value(),
        "randrange takes an optional seed");
  Check(!Program::Parse("randrange(0)").has_value(),
        "randrange needs a start and a stop");
  Check(Program::Parse("rand(1)").has_value(), "rand takes an optional salt");
  Check(!Program::Parse("rand(1, 2)").has_value(),
        "rand takes at most one salt");
  Check(!Program::Parse("clamp(1, 2)").has_value(),
        "a fixed call checks its argument count");
}

bool SameCode(const Program &a_left, const Program &a_right) {
  return std::ranges::equal(
      a_left.Code(), a_right.Code(),
      [](const Program::Node &a_l, const Program::Node &a_r) {
        return a_l.op == a_r.op && a_l.number == a_r.number &&
               a_l.index == a_r.index;
      });
}

void HashProgramMatchesTheParsedHash() {
  const Program built = Program::Hash("value", "salt");
  const auto parsed = Program::Parse("hash(@value, @salt)");
  Check(parsed && SameCode(built, *parsed) &&
            std::ranges::equal(built.References(), parsed->References()),
        "Program::Hash builds the same code as the parsed hash");
}

void EachRandCallGetsTheNextSalt() {
  const auto twice = Program::Parse("rand() + rand()");
  std::vector<float> salts;
  if (twice) {
    const std::span<const Program::Node> code = twice->Code();
    for (std::size_t i = 1; i < code.size(); ++i) {
      if (code[i - 1].op == Program::Op::kIdentity &&
          code[i].op == Program::Op::kNumber) {
        salts.push_back(code[i].number);
      }
    }
  }
  Check(salts == std::vector<float>{0.0f, 1.0f},
        "two rand() calls in one expression get salts 0 and 1");
  Program::Inputs pinned;
  pinned.identity = 0.25f;
  const auto before = Program::Parse("rand(7)");
  const auto after = Program::Parse("rand() * 0 + rand(7)");
  const auto alone = Program::Parse("rand()");
  if (before && after && alone) {
    const float pinnedValue = AsScalar(before->Evaluate(pinned));
    const float firstFree = AsScalar(alone->Evaluate(pinned));
    Check(Near(AsScalar(after->Evaluate(pinned)), pinnedValue),
          "rand(7) keeps its value after a free rand() call");
    Check(!Near(pinnedValue, firstFree),
          "a salted rand differs from the first free rand");
  } else {
    Check(false, "the salted rand expressions parse");
  }
  const auto again = Program::Parse("rand()");
  Check(again && again->Code().size() > 1 && again->Code()[1].number == 0.0f,
        "each parse starts its salts at 0");
}
}

int main() {
  EveryTableFunctionParses();
  HashProgramMatchesTheParsedHash();
  EachRandCallGetsTheNextSalt();
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

  {
    bool permutes = true;
    for (int total = 1; total <= static_cast<int>(kMaxShuffleTotal); ++total) {
      for (int trial = 0; trial < 24; ++trial) {
        const float seed = static_cast<float>(trial) * 0.0417f - 0.3f;
        std::vector<bool> seen(static_cast<std::size_t>(total), false);
        for (int index = 0; index < total; ++index) {
          const float position = ShufflePosition(
              static_cast<float>(index), static_cast<float>(total), seed);
          const auto slot = static_cast<std::size_t>(position);
          if (!(position >= 0.0f) || position >= static_cast<float>(total) ||
              seen[slot]) {
            permutes = false;
            break;
          }
          seen[slot] = true;
        }
      }
    }
    Check(permutes, "shuffle orders every total up to the cap without repeats");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    Check(ShufflePosition(5.0f, 5.0f, 0.4f) == kShuffleOutside &&
              ShufflePosition(-1.0f, 5.0f, 0.4f) == kShuffleOutside &&
              ShufflePosition(nan, 5.0f, 0.4f) == kShuffleOutside &&
              ShufflePosition(inf, 5.0f, 0.4f) == kShuffleOutside,
          "indices outside the order sit past every position");
    Check(ShufflePosition(0.0f, 0.0f, 0.4f) == kShuffleOutside &&
              ShufflePosition(0.0f, nan, 0.4f) == kShuffleOutside,
          "an empty or unreadable total orders nothing");
    Check(ShufflePosition(3.0f, inf, 0.4f) < kMaxShuffleTotal &&
              ShufflePosition(300.0f, 1.0e9f, 0.4f) == kShuffleOutside,
          "totals above the cap order only the first 251 indices");
    Check(ShufflePosition(2.0f, 7.0f, nan) == ShufflePosition(2.0f, 7.0f, 0.0f),
          "an unreadable seed orders like seed 0");
    Check(ShufflePosition(2.9f, 7.0f, 0.4f) ==
              ShufflePosition(2.0f, 7.0f, 0.4f),
          "fractional indices order like their floor");
    Check(Near(Scalar("shuffle(0, 1, 0.8)"), 0.0f),
          "a one-item order puts its item first");
    Check(Near(Scalar("shuffle(9, 4, 0.2) < 2"), 0.0f),
          "an index outside the order is never among the first k");
    Check(!Program::Parse("shuffle([1, 2, 3], 4, 0.2)")
               .and_then([](Program p) -> std::expected<Program, std::string> {
                 const auto typed = p.Check(AllScalars);
                 if (!typed)
                   return std::unexpected(typed.error());
                 return p;
               })
               .has_value(),
          "shuffle takes scalars");
  }

  {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float value = HashUnit(3.0f, 1.0f);
    Check(value >= 0.0f && value < 1.0f && value == HashUnit(3.0f, 1.0f),
          "hash is a repeatable 0..1 value");
    Check(value != HashUnit(3.0f, 2.0f) && value != HashUnit(4.0f, 1.0f),
          "hash depends on both the value and the salt");
    Check(HashUnit(-0.0f, 1.0f) == HashUnit(0.0f, 1.0f) &&
              HashUnit(nan, 1.0f) == HashUnit(0.0f, 1.0f),
          "negative zero and unreadable values hash like zero");
    std::array<int, 16> buckets{};
    constexpr int kSamples = 16000;
    for (int i = 0; i < kSamples; ++i) {
      const float h = HashUnit(static_cast<float>(i), 7.0f);
      buckets[static_cast<std::size_t>(std::min(15.0f, h * 16.0f))]++;
    }
    Check(
        std::ranges::all_of(buckets, [](int n) { return n > 850 && n < 1150; }),
        "hash spreads consecutive values evenly");

    bool inRange = true;
    std::array<bool, 3> reached{};
    for (int i = 0; i <= 100; ++i) {
      const float r = RandomInRange(1.0f, 4.0f, static_cast<float>(i) / 100.0f);
      inRange = inRange && r >= 1.0f && r <= 3.0f && r == std::floor(r);
      if (r >= 1.0f && r <= 3.0f)
        reached[static_cast<std::size_t>(r) - 1] = true;
    }
    Check(inRange && std::ranges::all_of(reached, [](bool b) { return b; }),
          "randrange stays below stop and reaches every whole number");
    Check(RandomInRange(5.0f, 5.0f, 0.5f) == 5.0f &&
              RandomInRange(5.0f, 2.0f, 0.5f) == 5.0f &&
              RandomInRange(nan, 4.0f, 0.5f) == 0.0f &&
              RandomInRange(1.0f, 4.0f, nan) == 1.0f,
          "an empty range gives start and unreadable inputs stay bounded");
    Check(Near(Scalar("randrange(1, 4, 0.999)"), 3.0f) &&
              Near(Scalar("randrange(1.7, 4.2, 0)"), 1.0f),
          "randrange floors its bounds and reads its seed");
    Check(Rejected("rand(1, 2)") && Rejected("randrange(1)") &&
              Rejected("randrange(1, 2, 3, 4)"),
          "rand and randrange reject the wrong argument counts");
    const auto twice = Program::Parse("rand() - rand()");
    Check(twice && twice->UsesIdentity() && !twice->Constant(),
          "an expression with rand() is never folded as a constant");
    if (twice) {
      Program::Inputs inputs;
      inputs.identity = 0.375f;
      Check(AsScalar(twice->Evaluate(inputs)) != 0.0f,
            "each rand() call in an expression gets its own value");
    }
    Check(!ParseCurve("x * rand()") && !ParseCurve("randrange(0, 4) * x") &&
              ParseCurve("randrange(0, 4, x)"),
          "a curve cannot use rand() but can seed randrange from x");
  }

  {
    const auto Close = [](const Vec3 &a_left, const Vec3 &a_right) {
      return Near(a_left.x, a_right.x) && Near(a_left.y, a_right.y) &&
             Near(a_left.z, a_right.z);
    };
    Check(Close(HueColor(0.0f), {1.0f, 0.0f, 0.0f}) &&
              Close(HueColor(1.0f / 3.0f), {0.0f, 1.0f, 0.0f}) &&
              Close(HueColor(2.0f / 3.0f), {0.0f, 0.0f, 1.0f}) &&
              Close(HueColor(1.0f), HueColor(0.0f)),
          "hue walks red, green, blue and wraps every turn");
    const Vec3 colour{0.15f, 0.39f, 1.0f};
    Check(Close(ShiftHue(colour, 0.0f), colour) &&
              Close(ShiftHue(colour, 1.0f), colour),
          "a whole number of turns leaves a colour unchanged");
    Check(Close(ShiftHue({1.0f, 0.0f, 0.0f}, 1.0f / 3.0f), {0.0f, 1.0f, 0.0f}),
          "a third of a turn moves red to green");
    Check(Close(ShiftHue({0.4f, 0.4f, 0.4f}, 0.2f), {0.4f, 0.4f, 0.4f}),
          "grey has no hue to turn");
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(Close(HueColor(nan), HueColor(0.0f)) &&
              Close(ShiftHue(colour, nan), colour),
          "an unreadable number of turns counts as none");
    const auto typed = [](const char *a_text) {
      const auto program = Program::Parse(a_text);
      return program ? program->Check(AllScalars)
                     : std::expected<ValueType, std::string>{
                           std::unexpected(program.error())};
    };
    Check(typed("hue(0.5)") == ValueType::kVec3 &&
              typed("hueShift([1, 0, 0], 0.25)") == ValueType::kVec3,
          "hue and hueShift give colours");
    Check(!typed("hue([1, 2, 3])") && !typed("hueShift(1, 0.25)") &&
              !typed("hueShift([1, 0, 0], [1, 2, 3])"),
          "hue takes turns and hueShift takes a colour and turns");
  }

  return test::Finish("expression");
}
