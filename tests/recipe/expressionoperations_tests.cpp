// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Expression.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;

namespace {
Program Parse(std::string_view text) {
  auto program = Program::Parse(text);
  test::Check(program.has_value(), std::string{"parses: "} + std::string{text});
  return program.value_or(Program{});
}
void Result(std::string_view text, Value expected) {
  const auto program = Parse(text);
  const auto type = program.Check(
      [](std::string_view) { return std::optional{ValueType::kScalar}; });
  test::Check(type && *type == TypeOf(expected),
              std::string{text} + " checked type");
  const auto actual = program.Evaluate({});
  test::Check(TypeOf(actual) == TypeOf(expected) && actual == expected,
              std::string{text} + " evaluated value and shape");
}
void Error(std::string_view text, std::string_view expected) {
  const auto type =
      Parse(text).Check([](std::string_view) -> std::optional<ValueType> {
        return std::nullopt;
      });
  test::Check(!type && type.error() == expected,
              std::string{text} + " diagnostic");
}
void Arithmetic() {
  Result("10 - [2, 4]", Vec2{8, 6});
  Result("[12, 18, 24] / 6", Vec3{2, 3, 4});
  Result("24 / [2, 4, 6]", Vec3{12, 6, 4});
  Result("pow([2, 3], 3)", Vec2{8, 27});
  Result("step([3, 5], 4)", Vec2{1, 0});
  Result("clamp([1, 5, 9], 7, 3)", Vec3{3, 5, 7});
  Result("lerp([2, 4], 10, 0.25)", Vec2{4, 5.5f});
  Result("smoothstep(2, 6, [2, 4, 6])", Vec3{0, 0.5f, 1});
  Result("smoothstep(3, 3, [2, 3])", Vec2{0, 1});
  Result("length([3, 4])", 5.0f);
  Result("length([1, 2, 2])", 3.0f);
  Result("distance([1, 1], [4, 5])", 5.0f);
  Result("distance([2, 3, 6], [0, 0, 0])", 7.0f);
  Result("dot([1, 2], [3, 4])", 11.0f);
  Result("dot([1, 2, 3], [4, 5, 6])", 32.0f);
  Result("cross([1, 0, 0], [0, 1, 0])", Vec3{0, 0, 1});
  Result("cross([0, 1, 0], [1, 0, 0])", Vec3{0, 0, -1});
  Result("normalize([3, 4])", Vec2{0.6f, 0.8f});
  Result("normalize([0, 0, 0])", Vec3{0, 0, 0});
  Result("sqrt([-1, 9])", Vec2{0, 3});
  Result("frac([-1.25, 2.25])", Vec2{0.75f, 0.25f});
  Result("1 / 0.0000001", 0.0f);
  Result("pow(-1, 0.5)", 0.0f);
  Result("1 == 1.0000001", 1.0f);
  Result("1 != 1.0000001", 0.0f);
  Result("-2 and 3", 0.0f);
  Result("-2 or -3", 0.0f);
  Result("not -2", 1.0f);
  Result("if(-1, [2, 3], [4, 5])", Vec2{4, 5});
  Result("if(1, [2, 3], [4, 5])", Vec2{2, 3});
}
void Diagnostics() {
  Error("[1, 2] - [3, 4, 5]", "'operator' mixes vec2 with vec3");
  Error("clamp([1, 2], [3, 4, 5], 0)", "'function' mixes vec2 with vec3");
  Error("lerp(0, [1, 2], [3, 4, 5])", "'function' mixes vec2 with vec3");
  Error("if(1, [1, 2], [3, 4, 5])", "'if' mixes vec2 with vec3");
  Error("if([1, 2], [1, 2], [3, 4, 5])", "if() takes a scalar condition");
  Error("[[1, 2], 3]", "vector components must be scalars");
  Error("not [1, 2]", "comparisons and logic take scalars");
  Error("[1, 2] < 3", "comparisons and logic take scalars");
  Error("3 and [1, 2]", "comparisons and logic take scalars");
  Error("length(2)", "length() takes a vector");
  Error("distance([1, 2], 3)", "distance() takes two vectors of the same size");
  Error("distance([1, 2], [3, 4, 5])",
        "distance() takes two vectors of the same size");
  Error("dot([1, 2], 3)", "dot() takes two vectors of the same size");
  Error("dot([1, 2], [3, 4, 5])", "dot() takes two vectors of the same size");
  Error("cross([1, 2], [3, 4])", "cross() takes two vec3s");
  Error("cross([1, 2, 3], 4)", "cross() takes two vec3s");
  Error("normalize(2)", "normalize() takes a vector");
  Error("@curve([1, 2])", "curve '@curve' takes a scalar");
  Error("[1, 2] + @missing", "unknown row '@missing'");
  Error("([1, 2] + [3, 4, 5]) + @missing", "'operator' mixes vec2 with vec3");
  test::Check(Parse("[1, 2] + [3, 4, 5]").Evaluate({}) == Value{0.0f},
              "unchecked dimension mismatch falls back to zero");
  test::Check(Parse("distance([1, 2], [3, 4, 5])").Evaluate({}) == Value{0.0f},
              "unchecked distance dimension mismatch falls back to zero");
}
void ConditionalCompatibility() {
  for (const auto condition : {"0", "1"}) {
    const auto program = Parse(std::string{"if("} + condition + ", 2, [3, 4])");
    test::Check(program.Check({}) == ValueType::kVec2,
                "mixed branches join to vec2");
    const Value expected =
        condition == std::string_view{"1"} ? Value{2.0f} : Value{Vec2{3, 4}};
    test::Check(program.Evaluate({}) == expected,
                "legacy conditional returns selected branch without coercion");
  }
}
void StackBoundary() {
  std::string expression = "1+2";
  for (int i = 0; i < 31; ++i) {
    expression = "lerp(1,2," + expression + ")";
  }
  Result(expression, 34.0f);
}

void Inputs() {
  test::Check(Program{}.Evaluate({}) == Value{0.0f},
              "empty program evaluates to zero");
  test::Check(Program{}.Check({}) == ValueType::kScalar,
              "empty program checks as scalar");
  test::Check(Parse("@missing + 2").Evaluate({}) == Value{2.0f},
              "missing reference is zero");
  const auto caller = Parse("@curve(3)");
  test::Check(caller.Evaluate({}) == Value{3.0f}, "missing curve is identity");
  const Program *curves[]{nullptr};
  Program::Inputs inputs;
  inputs.curves = curves;
  test::Check(caller.Evaluate(inputs) == Value{3.0f}, "null curve is identity");
  const auto curve = Parse("x + mean + time");
  curves[0] = &curve;
  inputs.mean = 0.25f;
  inputs.time = 8;
  inputs.x = Vec2{4, 6};
  test::Check(caller.Evaluate(inputs) == Value{3.25f},
              "curve forwards argument and mean with default time");
  const auto variables = Parse("x + mean + time");
  test::Check(variables.Check({}, ValueType::kVec2) == ValueType::kVec2,
              "x type is supplied by caller");
  test::Check(variables.Evaluate(inputs) == Value{Vec2{12.25f, 14.25f}},
              "x, mean and time inputs retain shape");
}
}
int main() {
  Arithmetic();
  Diagnostics();
  Inputs();
  ConditionalCompatibility();
  StackBoundary();
  return test::Finish("expression operations");
}
