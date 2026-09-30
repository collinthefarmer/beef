// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/ProgramShader.h"

#include <algorithm>
#include <array>
#include <format>
#include <optional>

namespace BetterEnchantmentEffects {
namespace {
using Op = InterpreterOpcode;

constexpr std::array<OpcodeStatement, 42> kStatements{{
    {Op::kNumber, R"(r = c.y;)"},
    {Op::kMakeVec2, R"(r = float3(b.x, a.x, 0);)"},
    {Op::kMakeVec3, R"(r = float3(d.x, b.x, a.x);)"},
    {Op::kInput,
     R"(r = refs[idx].x > 0.5 ? ReadTexture((int)refs[idx].y, uv) : refValues[idx].xyz;)"},
    {Op::kLookup, R"(r = LutAt(idx, a.x);)"},
    {Op::kNeg, R"(r = -a;)"},
    {Op::kNot, R"(r = a.x > 0 ? 0 : 1;)"},
    {Op::kAdd, R"(r = b + a;)"},
    {Op::kSub, R"(r = b - a;)"},
    {Op::kMul, R"(r = b * a;)"},
    {Op::kDiv, R"(r = SafeDiv(b, a);)"},
    {Op::kLt, R"(r = b.x < a.x ? 1 : 0;)"},
    {Op::kGt, R"(r = b.x > a.x ? 1 : 0;)"},
    {Op::kLe, R"(r = b.x <= a.x ? 1 : 0;)"},
    {Op::kGe, R"(r = b.x >= a.x ? 1 : 0;)"},
    {Op::kEq, R"(r = b.x == a.x ? 1 : 0;)"},
    {Op::kNe, R"(r = b.x != a.x ? 1 : 0;)"},
    {Op::kAnd, R"(r = (b.x > 0 ? 1 : 0) * (a.x > 0 ? 1 : 0);)"},
    {Op::kOr, R"(r = max(b.x > 0 ? 1 : 0, a.x > 0 ? 1 : 0);)"},
    {Op::kIf, R"(r = d.x > 0 ? b : a;)"},
    {Op::kAbs, R"(r = abs(a);)"},
    {Op::kMin, R"(r = min(b, a);)"},
    {Op::kMax, R"(r = max(b, a);)"},
    {Op::kClamp, R"(r = clamp(d, b, a);)"},
    {Op::kSaturate, R"(r = saturate(a);)"},
    {Op::kFloor, R"(r = floor(a);)"},
    {Op::kCeil, R"(r = ceil(a);)"},
    {Op::kFrac, R"(r = frac(a);)"},
    {Op::kSqrt, R"(r = sqrt(max(0, a));)"},
    {Op::kPow, R"(r = SafePow(b, a);)"},
    {Op::kSin, R"(r = sin(a);)"},
    {Op::kCos, R"(r = cos(a);)"},
    {Op::kStep,
     R"(r = float3(a.x < b.x ? 0 : 1, a.y < b.y ? 0 : 1, a.z < b.z ? 0 : 1);)"},
    {Op::kSmoothstep, R"(r = smoothstep(d, b, a);)"},
    {Op::kLerp, R"(r = lerp(d, b, a);)"},
    {Op::kLength, R"(r = components == 2 ? length(a.xy) : length(a);)"},
    {Op::kDistance,
     R"(r = components == 2 ? length(b.xy - a.xy) : length(b - a);)"},
    {Op::kDot, R"(r = components == 2 ? dot(b.xy, a.xy) : dot(b, a);)"},
    {Op::kCross, R"(r = cross(b, a);)"},
    {Op::kNormalize,
     R"(r = SafeDiv(a, components == 2 ? length(a.xy) : length(a));)"},
    {Op::kQuantize, R"(r = round(saturate(a) * 255) / 255;)"},
    {Op::kSplat, R"(r = a.xxx;)"},
}};

std::string_view StatementOf(InterpreterOpcode opcode) {
  const auto found =
      std::ranges::find(kStatements, opcode, &OpcodeStatement::opcode);
  if (found != kStatements.end())
    return found->statement;
  return std::ranges::find(kStatements, Op::kNormalize,
                           &OpcodeStatement::opcode)
      ->statement;
}

bool KeepsZ(InterpreterOpcode opcode) {
  return opcode == Op::kLength || opcode == Op::kDistance || opcode == Op::kDot;
}

std::string InputStatement(std::span<const TextureSlot> slots,
                           std::uint32_t index) {
  if (index < slots.size() && slots[index])
    return std::format("r = ReadTexture({}, uv);", *slots[index]);
  return "r = refValues[idx].xyz;";
}

std::string StackLocal(std::optional<std::size_t> depth) {
  return depth ? std::format("s{}", *depth) : std::string{"float3(0, 0, 0)"};
}
}

std::span<const OpcodeStatement> OpcodeStatements() noexcept {
  return kStatements;
}

std::string InterpreterSwitch() {
  std::string text = "switch (op) {\n";
  for (const auto &[opcode, statement] : kStatements)
    if (opcode != Op::kNormalize)
      text += std::format("case {}: {} break;\n", static_cast<int>(opcode),
                          statement);
  text += std::format("default: {} break;\n}}\n", StatementOf(Op::kNormalize));
  return text;
}

std::vector<TextureSlot>
TextureSlots(std::span<const InterpreterInput> inputs) {
  std::vector<TextureSlot> slots;
  for (const auto &input : inputs) {
    const auto *texture = Get<InterpreterTextureInput>(input);
    slots.push_back(texture ? TextureSlot{texture->slot} : std::nullopt);
  }
  return slots;
}

std::string ProgramFunction(std::string_view name,
                            std::span<const InterpreterInstruction> code,
                            std::size_t first,
                            std::span<const TextureSlot> slots) {
  std::string body;
  std::size_t depth = 0;
  std::size_t locals = 0;
  for (std::size_t k = 0; k < code.size(); ++k) {
    const auto &instruction = code[k];
    const auto pops = InterpreterPops(instruction.opcode);
    const auto pop = [&]() -> std::optional<std::size_t> {
      if (depth == 0)
        return std::nullopt;
      return --depth;
    };
    const auto a = pops >= 1 ? pop() : std::nullopt;
    const auto b = pops >= 2 ? pop() : std::nullopt;
    const auto d = pops >= 3 ? pop() : std::nullopt;
    const auto statement = instruction.opcode == Op::kInput
                               ? InputStatement(slots, instruction.index)
                               : std::string{StatementOf(instruction.opcode)};
    const bool zeroZ =
        instruction.components == 2 && !KeepsZ(instruction.opcode);
    const bool pushes = depth < kInterpreterStack;
    body += std::format(
        "\t{{ float4 c = code[{}]; int idx = {}; int components = {}; "
        "float3 a = {}, b = {}, d = {}; float3 r = 0; {}{}{} }}\n",
        first + k, instruction.index, instruction.components, StackLocal(a),
        StackLocal(b), StackLocal(d), statement, zeroZ ? " r.z = 0;" : "",
        pushes ? std::format(" s{} = r;", depth) : std::string{});
    if (pushes) {
      ++depth;
      locals = std::max(locals, depth);
    }
  }
  std::string text = std::format("float3 {}(float2 uv)\n{{\n", name);
  for (std::size_t s = 0; s < locals; ++s)
    text += std::format("\tfloat3 s{} = 0;\n", s);
  text += body;
  text += std::format("\treturn {};\n}}\n\n",
                      depth > 0 ? std::format("s{}", depth - 1)
                                : std::string{"float3(0, 0, 0)"});
  return text;
}

std::string GenerateProgramShader(const InterpreterProgram &program) {
  std::string text = ProgramFunction("GeneratedProgram", program.Instructions(),
                                     0, TextureSlots(program.Inputs()));
  text += std::format("float4 {}(VSOut i) : SV_Target\n{{\n\tfloat3 result = "
                      "GeneratedProgram(i.uv);\n\treturn {};\n}}\n",
                      kGeneratedProgramEntry,
                      program.ResultType() != ValueType::kScalar
                          ? "float4(result, 1)"
                          : "float4(result.xxx, 1)");
  return text;
}
}
