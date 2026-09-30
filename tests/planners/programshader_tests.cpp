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
std::optional<InterpreterProgram> CompileMask(std::string_view expression,
                                              float constant) {
  Recipe recipe;
  recipe.signals = {{"a", ConstantSignal{constant}}};
  recipe.sources = {{"s", MaterialSource{MaterialChannel::kRoughness}}};
  recipe.masks = {{"m", std::string{expression}}};
  const auto graph = RecipeGraph::Compile(recipe);
  const auto node = graph.FindNodeIndex("m");
  if (!node)
    return std::nullopt;
  auto program = InterpreterProgram::Compile(graph, {*node, 0});
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
  std::set<InterpreterOpcode> opcodes;
  for (const auto &entry : OpcodeStatements()) {
    Check(!entry.statement.empty(), "every opcode has a statement");
    opcodes.insert(entry.opcode);
  }
  test::Equal(opcodes.size(), OpcodeStatements().size(),
              "each opcode appears once in the table");
  test::Equal(opcodes.size(), std::size_t{42},
              "the table covers every interpreter opcode");

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
  MatchGolden("sample-scalar", GenerateProgramShader(InterpreterProgram::Sample(
                                   ValueType::kScalar)));
  MatchGolden("absolute-difference",
              GenerateProgramShader(InterpreterProgram::AbsoluteDifference()));
  MatchGolden("map", GenerateProgramShader(InterpreterProgram::Map()));
  if (const auto program = CompileMask(expressions[0], 0.25f))
    MatchGolden("expression", GenerateProgramShader(*program));
  return test::Finish("program shader");
}
