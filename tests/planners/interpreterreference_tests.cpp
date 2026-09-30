// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/InterpreterReference.h"
#include "recipe/RecipeGraph.h"
#include "test_support.h"

#include <cmath>
#include <cstdio>
#include <random>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
constexpr std::array kExpressions{
    "@a + @b * 2",
    "@a - @b / 3",
    "@a / @b",
    "max(@a, @b) - min(@a, @b)",
    "clamp(@a, 0, 1) + saturate(@b * 0.5)",
    "abs(@a) + floor(@b) + ceil(@a) + frac(@b)",
    "sqrt(abs(@a)) * @b",
    "sin(@a) + cos(@b)",
    "pow(abs(@a) + 0.25, @b)",
    "pow(@a, 2) + pow(@a, 3) + pow(@a, -1)",
    "pow(@a, @b) + pow(@a * 0, @b * 0) + pow(1, @b / 0)",
    "lerp(@a, @b, 0.3) + step(@a, @b)",
    "smoothstep(-1, 1, @a) * @b",
    "if(@a > @b, @a, @b) + if(@a <= @b, 1, 0)",
    "length([@a, @b]) + dot([@a, @b], [2, 3])",
    "distance([@a, @b, 1], [1, @a, @b])",
    "normalize([@a, @b, 0.5])",
    "cross([@a, 1, @b], [1, @b, @a])",
    "-@a * (@b == @a) + (@a != @b)",
};

bool Close(float actual, float expected) {
  if (std::isnan(actual) || std::isnan(expected))
    return std::isnan(actual) == std::isnan(expected);
  return std::fabs(actual - expected) <=
         1e-5f * std::max(1.0f, std::fabs(expected));
}
}

int main() {
  std::mt19937 random{7};
  std::uniform_real_distribution<float> value{-3.0f, 3.0f};
  std::size_t compared = 0, mismatched = 0;
  for (const auto *text : kExpressions) {
    Recipe recipe;
    recipe.signals = {{"a", ConstantSignal{0.0f}}, {"b", ConstantSignal{0.0f}}};
    recipe.masks = {{"m", text}};
    const auto graph = RecipeGraph::Compile(recipe);
    const auto node = graph.FindNodeIndex("m");
    const auto *expression = node ? graph.ExpressionAt(*node) : nullptr;
    const auto program = node ? InterpreterProgram::Compile(graph, {*node, 0})
                              : std::expected<InterpreterProgram, std::string>{
                                    std::unexpected("missing")};
    Check(expression && program.has_value(),
          std::string{"compiles for the interpreter: "} + text);
    if (!expression || !program)
      continue;
    for (int trial = 0; trial < 200; ++trial) {
      std::vector<Value> refs;
      std::vector<Vec3> inputs;
      for (const auto &input : program->Inputs()) {
        const auto *bound = Get<InterpreterValueInput>(input);
        const float v = value(random);
        refs.push_back(v);
        inputs.push_back({v, v, v});
        static_cast<void>(bound);
      }
      Program::Inputs cpu;
      cpu.refs = refs;
      const auto expected = AsVec3(expression->program.Evaluate(cpu));
      const auto actual = EvaluateInterpreter(*program, {inputs, {}});
      const auto components = TypeOf(expression->program.Evaluate(cpu));
      const bool same =
          Close(actual.x, expected.x) &&
          (components == ValueType::kScalar || Close(actual.y, expected.y)) &&
          (components != ValueType::kVec3 || Close(actual.z, expected.z));
      ++compared;
      if (!same) {
        if (++mismatched <= 8)
          std::printf("mismatch %s a=%g b=%g: gpu %g %g %g cpu %g %g %g\n",
                      text, AsScalar(refs[0]),
                      refs.size() > 1 ? AsScalar(refs[1]) : 0.0f, actual.x,
                      actual.y, actual.z, expected.x, expected.y, expected.z);
      }
    }
  }
  Check(compared > 3000, "the cross-check runs every expression");
  test::Equal(mismatched, std::size_t{0},
              "the reference interpreter matches the recipe evaluator");

  Check(QuantizeUnorm8({0.5f, 0.0f, 1.0f}) == Vec3{128.0f / 255.0f, 0.0f, 1.0f},
        "quantize rounds to the nearest 8-bit step, ties to even");
  Check(QuantizeUnorm8({-1.0f, 2.0f, std::nanf("")}) == Vec3{0.0f, 1.0f, 0.0f},
        "quantize clamps and maps NaN to zero, like UNORM8 storage");
  const std::array<Vec3, 2> pair{Vec3{0.25f, 1.0f, 0.5f},
                                 Vec3{0.75f, 0.5f, 0.5f}};
  Check(EvaluateInterpreter(InterpreterProgram::AbsoluteDifference(),
                            {pair, {}}) == Vec3{0.5f, 0.5f, 0.0f},
        "the fusion check's difference program computes |a - b|");
  return test::Finish("interpreter reference");
}
