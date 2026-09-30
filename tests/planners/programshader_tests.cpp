// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ProgramShader.h"
#include "test_support.h"

#include <cstdlib>
#include <format>
#include <set>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
std::optional<FieldProgram> CompileMask(std::string_view expression,
                                        float constant) {
  Recipe recipe;
  recipe.signals = {{"a", ConstantSignal{constant}}};
  recipe.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
  recipe.masks = {{"m", std::string{expression}}};
  const auto graph = RecipeGraph::Compile(recipe);
  const auto node = graph.FindNodeIndex("m");
  if (!node)
    return std::nullopt;
  auto program = FieldProgram::Compile(graph, {*node, 0});
  if (!program)
    return std::nullopt;
  return *program;
}

std::size_t StackSlots(const std::string &text) {
  std::size_t slots = 0;
  while (text.find(std::format("\tfloat3 s{} = 0;", slots)) !=
         std::string::npos)
    ++slots;
  return slots;
}

void MatchGolden(const std::string &name, const std::string &text) {
  const auto golden = test::Fixtures() / "shaders" / (name + ".hlsl");
  if (std::getenv("BEEF_UPDATE")) {
    Check(test::WriteFile(golden, text), "golden " + name + " rewritten");
    return;
  }
  Check(text == test::ReadFile(golden),
        name + " matches its golden (BEEF_UPDATE=1 rewrites it)");
}
}

int main() {
  using Op = ProgramOpcode;
  const std::set<ProgramOpcode> every{
      Op::kNumber,   Op::kMakeVec2,   Op::kMakeVec3, Op::kInput,
      Op::kLookup,   Op::kNeg,        Op::kNot,      Op::kAdd,
      Op::kSub,      Op::kMul,        Op::kDiv,      Op::kLt,
      Op::kGt,       Op::kLe,         Op::kGe,       Op::kEq,
      Op::kNe,       Op::kAnd,        Op::kOr,       Op::kIf,
      Op::kAbs,      Op::kMin,        Op::kMax,      Op::kClamp,
      Op::kSaturate, Op::kFloor,      Op::kCeil,     Op::kFrac,
      Op::kSqrt,     Op::kPow,        Op::kSin,      Op::kCos,
      Op::kStep,     Op::kSmoothstep, Op::kLerp,     Op::kLength,
      Op::kDistance, Op::kDot,        Op::kCross,    Op::kNormalize,
      Op::kQuantize, Op::kSplat};
  std::set<ProgramOpcode> tabled;
  for (const auto &entry : OpcodeStatements()) {
    Check(!entry.statement.empty(), "every opcode has a statement");
    tabled.insert(entry.opcode);
  }
  test::Equal(tabled.size(), OpcodeStatements().size(),
              "each opcode appears once in the table");
  Check(tabled == every, "the table covers every interpreter opcode");
  test::Equal(InterpreterZRule(),
              std::string{"if (components == 2 && op != 38 && op != 39 && "
                          "op != 40) r.z = 0;\n"},
              "length, distance and dot keep z for two components");

  const std::array expressions{
      "saturate(pow(@s, 2) * @a + lerp(@s, 1, @a))",
      "if(@s > @a, sin(@s), cos(@a)) + smoothstep(0, 1, frac(@s * 3))",
      "length([@s, @a]) + dot([@s, @a, 1], [1, 2, 3])",
      "normalize(cross([@s, 1, @a], [1, @s, 0.5]))",
      "clamp(@s / @a, -1, 1) * step(0.5, @s) - abs(floor(@s) - ceil(@a))",
  };
  for (const auto *expression : expressions) {
    const auto program = CompileMask(expression, 0.25f);
    const auto other = CompileMask(expression, 0.75f);
    Check(program && other, std::format("{} compiles", expression));
    if (!program || !other)
      continue;
    const auto text = GenerateProgramShader(*program);
    test::Equal(
        StackSlots(text), program->StackSize(),
        std::format("{} declares one local per stack slot", expression));
    Check(text == GenerateProgramShader(*other),
          std::format("{} shares its shader across numbers", expression));
    Check(text.find("code[") != std::string::npos &&
              text.find(kGeneratedProgramEntry) != std::string::npos,
          std::format("{} reads numbers from the constants", expression));
  }

  MatchGolden("interpreter-switch", InterpreterSwitch());
  MatchGolden("sample-scalar",
              GenerateProgramShader(FieldProgram::Sample(ValueType::kScalar)));
  MatchGolden("absolute-difference",
              GenerateProgramShader(FieldProgram::AbsoluteDifference()));
  MatchGolden("map", GenerateProgramShader(FieldProgram::Map()));
  if (const auto program = CompileMask(expressions[0], 0.25f))
    MatchGolden("expression", GenerateProgramShader(*program));
  return test::Finish("program shader");
}
