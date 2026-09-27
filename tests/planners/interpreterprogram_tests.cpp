// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/InterpreterProgram.h"
#include "test_support.h"

#include <algorithm>
#include <format>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
std::expected<InterpreterProgram, std::string>
Compile(const RecipeGraph &graph, std::string_view name,
        InterpreterLimits limits = {}) {
  const auto index = graph.FindNodeIndex(name);
  return InterpreterProgram::Compile(graph, {index.value_or(graph.Size()), 0},
                                     limits);
}
bool Error(const std::expected<InterpreterProgram, std::string> &result,
           std::string_view message) {
  return !result && result.error().contains(message);
}
}
int main() {
  {
    Recipe recipe;
    recipe.signals = {{"gain", ConstantSignal{0.5f}}};
    recipe.sources = {{"image", MaterialSource{MaterialChannel::kRoughness}}};
    recipe.curves = {{"shape", "x * x + mean"}};
    recipe.masks = {{"mixed", "@shape(@image) * @gain + time"}};
    const auto graph = RecipeGraph::Compile(recipe);
    const auto program = Compile(graph, "mixed");
    Check(program.has_value(),
          "mixed texture, value, time and function inputs compile");
    if (program) {
      Check(program->Inputs().size() == 3 && program->TextureCount() == 1 &&
                program->FunctionLookups().size() == 1,
            "input and lookup requests describe the complete resource demand");
      const auto *texture = Get<InterpreterTextureInput>(program->Inputs()[0]);
      const auto *gain = Get<InterpreterValueInput>(program->Inputs()[1]);
      const auto *time = Get<InterpreterValueInput>(program->Inputs()[2]);
      Check(
          texture && texture->output.node == *graph.FindNodeIndex("image") &&
              texture->slot == 0 && gain &&
              gain->output == *graph.FindSignalOutput("gain") && time,
          "logical inputs retain graph output identity in operand-slot order");
      const auto *timeNode = time ? graph.NodeAt(time->output.node) : nullptr;
      const auto *external =
          timeNode ? Get<ExternalInput>(timeNode->kind) : nullptr;
      Check(external && Is<TimeInput>(external->source),
            "clock input uses the ordinary value binding path");
      Check(program->FunctionLookups()[0].function ==
                    *graph.FindFunction("shape") &&
                program->FunctionLookups()[0].arguments.size() == 1 &&
                program->FunctionLookups()[0].arguments.front().parameter == 1,
            "lookup requests bind ordinary graph argument references");
      const std::array<int, 6> expected{3, 4, 3, 12, 3, 10};
      Check(program->Instructions().size() == expected.size(),
            "mixed expression emits the expected instruction count");
      bool encoding = program->Instructions().size() == expected.size();
      for (std::size_t i = 0; encoding && i < expected.size(); ++i)
        encoding =
            static_cast<int>(program->Instructions()[i].opcode) == expected[i];
      Check(encoding, "instruction numbers match the shader ABI independently "
                      "of recipe opcodes");
      InterpreterLimits limits;
      limits.instructions = expected.size();
      limits.inputs = 3;
      limits.textures = 1;
      limits.lookups = 1;
      limits.stack = 2;
      Check(Compile(graph, "mixed", limits).has_value(),
            "exact backend capacity is accepted");
      --limits.instructions;
      Check(Error(Compile(graph, "mixed", limits),
                  "mask mixed: instruction limit"),
            "instruction failures name the expression");
      limits = {};
      limits.inputs = 2;
      Check(Error(Compile(graph, "mixed", limits), "input slot limit"),
            "input capacity is checked before resource preparation");
      limits = {};
      limits.textures = 0;
      Check(Error(Compile(graph, "mixed", limits), "texture slot limit"),
            "texture capacity is checked before resource preparation");
      limits = {};
      limits.lookups = 0;
      Check(Error(Compile(graph, "mixed", limits), "function lookup limit"),
            "lookup capacity is checked before resource preparation");
    }
  }
  {
    Recipe recipe;
    recipe.masks = {{"width", "length([1,2] + 3)"}, {"stack", "[1,2,3]"}};
    const auto graph = RecipeGraph::Compile(recipe);
    const auto program = Compile(graph, "width");
    Check(program && program->ResultType() == ValueType::kScalar &&
              program->Instructions().back().components == 2,
          "vector reduction retains operand width after scalar broadcasting");
    if (program) {
      const auto add = std::ranges::find_if(
          program->Instructions(), [](const InterpreterInstruction &i) {
            return i.opcode == InterpreterOpcode::kAdd;
          });
      Check(add != program->Instructions().end() && add->components == 2,
            "vec2 arithmetic requests clearing the unused shader lane");
    }
    InterpreterLimits limits;
    limits.stack = 2;
    Check(Error(Compile(graph, "stack", limits), "stack depth limit"),
          "temporary stack demand is bounded even for a single vector result");
    limits.stack = 3;
    Check(Compile(graph, "stack", limits).has_value(),
          "exact stack capacity is accepted");
  }
  {
    Recipe recipe;
    std::string expression;
    for (int i = 0; i < 9; ++i) {
      const auto name = std::format("image{}", i);
      recipe.sources.push_back({name, MaterialSource{}});
      if (!expression.empty())
        expression += " + ";
      expression += "@" + name;
    }
    recipe.masks = {{"tooManyTextures", expression}};
    const auto graph = RecipeGraph::Compile(recipe);
    InterpreterLimits limits;
    limits.textures = 100;
    Check(Error(Compile(graph, "tooManyTextures", limits),
                "texture slot limit exceeded (limit 8)"),
          "requested capabilities cannot exceed the actual shader texture "
          "capacity");
  }
  {
    Recipe recipe;
    recipe.signals = {{"constant", ConstantSignal{1.0f}}};
    recipe.masks = {{"disabled", "@absent"}};
    const auto graph = RecipeGraph::Compile(recipe);
    Check(Error(Compile(graph, "constant"), "enabled numeric expression"),
          "unsupported operation nodes are rejected explicitly");
    Check(Error(Compile(graph, "disabled"), "mask disabled"),
          "disabled graphs retain diagnostic origin");
    Check(!InterpreterProgram::Compile(graph, {graph.Size(), 0}),
          "invalid node handles fail safely");
    Check(!InterpreterProgram::Compile(graph,
                                       {*graph.FindNodeIndex("constant"), 1}),
          "invalid output ports fail safely");
  }
  return test::Finish("interpreterprogram");
}
