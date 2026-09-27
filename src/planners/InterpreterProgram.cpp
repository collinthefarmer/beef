// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/InterpreterProgram.h"

#include <algorithm>
#include <array>
#include <format>

namespace BetterEnchantmentEffects {
namespace {
struct Encoding {
  InterpreterOpcode opcode;
  std::size_t operands;
};
std::optional<Encoding> Encode(Program::Op op) {
  switch (op) {
  case Program::Op::kNumber:
    return Encoding{InterpreterOpcode::kNumber, 0};
  case Program::Op::kMakeVec2:
    return Encoding{InterpreterOpcode::kMakeVec2, 2};
  case Program::Op::kMakeVec3:
    return Encoding{InterpreterOpcode::kMakeVec3, 3};
  case Program::Op::kRef:
    return Encoding{InterpreterOpcode::kInput, 0};
  case Program::Op::kCurve:
    return Encoding{InterpreterOpcode::kLookup, 1};
  case Program::Op::kX:
    return std::nullopt;
  case Program::Op::kMean:
    return std::nullopt;
  case Program::Op::kTime:
    return std::nullopt;
  case Program::Op::kNeg:
    return Encoding{InterpreterOpcode::kNeg, 1};
  case Program::Op::kNot:
    return Encoding{InterpreterOpcode::kNot, 1};
  case Program::Op::kAdd:
    return Encoding{InterpreterOpcode::kAdd, 2};
  case Program::Op::kSub:
    return Encoding{InterpreterOpcode::kSub, 2};
  case Program::Op::kMul:
    return Encoding{InterpreterOpcode::kMul, 2};
  case Program::Op::kDiv:
    return Encoding{InterpreterOpcode::kDiv, 2};
  case Program::Op::kLt:
    return Encoding{InterpreterOpcode::kLt, 2};
  case Program::Op::kGt:
    return Encoding{InterpreterOpcode::kGt, 2};
  case Program::Op::kLe:
    return Encoding{InterpreterOpcode::kLe, 2};
  case Program::Op::kGe:
    return Encoding{InterpreterOpcode::kGe, 2};
  case Program::Op::kEq:
    return Encoding{InterpreterOpcode::kEq, 2};
  case Program::Op::kNe:
    return Encoding{InterpreterOpcode::kNe, 2};
  case Program::Op::kAnd:
    return Encoding{InterpreterOpcode::kAnd, 2};
  case Program::Op::kOr:
    return Encoding{InterpreterOpcode::kOr, 2};
  case Program::Op::kIf:
    return Encoding{InterpreterOpcode::kIf, 3};
  case Program::Op::kAbs:
    return Encoding{InterpreterOpcode::kAbs, 1};
  case Program::Op::kMin:
    return Encoding{InterpreterOpcode::kMin, 2};
  case Program::Op::kMax:
    return Encoding{InterpreterOpcode::kMax, 2};
  case Program::Op::kClamp:
    return Encoding{InterpreterOpcode::kClamp, 3};
  case Program::Op::kSaturate:
    return Encoding{InterpreterOpcode::kSaturate, 1};
  case Program::Op::kFloor:
    return Encoding{InterpreterOpcode::kFloor, 1};
  case Program::Op::kCeil:
    return Encoding{InterpreterOpcode::kCeil, 1};
  case Program::Op::kFrac:
    return Encoding{InterpreterOpcode::kFrac, 1};
  case Program::Op::kSqrt:
    return Encoding{InterpreterOpcode::kSqrt, 1};
  case Program::Op::kPow:
    return Encoding{InterpreterOpcode::kPow, 2};
  case Program::Op::kSin:
    return Encoding{InterpreterOpcode::kSin, 1};
  case Program::Op::kCos:
    return Encoding{InterpreterOpcode::kCos, 1};
  case Program::Op::kStep:
    return Encoding{InterpreterOpcode::kStep, 2};
  case Program::Op::kSmoothstep:
    return Encoding{InterpreterOpcode::kSmoothstep, 3};
  case Program::Op::kLerp:
    return Encoding{InterpreterOpcode::kLerp, 3};
  case Program::Op::kLength:
    return Encoding{InterpreterOpcode::kLength, 1};
  case Program::Op::kDistance:
    return Encoding{InterpreterOpcode::kDistance, 2};
  case Program::Op::kDot:
    return Encoding{InterpreterOpcode::kDot, 2};
  case Program::Op::kCross:
    return Encoding{InterpreterOpcode::kCross, 2};
  case Program::Op::kNormalize:
    return Encoding{InterpreterOpcode::kNormalize, 1};
  }
  return std::nullopt;
}
std::uint32_t Components(ValueType type) {
  return type == ValueType::kScalar ? 1 : type == ValueType::kVec2 ? 2 : 3;
}
}
std::expected<InterpreterProgram, std::string>
InterpreterProgram::Compile(const RecipeGraph &graph, OutputRef result,
                            const InterpreterLimits &limits) {
  const auto *node = graph.NodeAt(result.node);
  const auto fail = [&](std::string message)
      -> std::expected<InterpreterProgram, std::string> {
    return std::unexpected(std::format(
        "{}: {}", node ? node->displayName : "interpreter", message));
  };
  const auto *expression = graph.ExpressionAt(result.node);
  const auto outputType = graph.OutputType(result);
  const auto *numeric = outputType ? Get<ValueType>(*outputType) : nullptr;
  if (!node || graph.IsDisabled(result.node) || result.output != 0 ||
      !expression || !numeric)
    return fail("requires an enabled numeric expression output");
  const auto code = expression->program.Code();
  if (code.empty() ||
      code.size() > std::min(limits.instructions, kInterpreterInstructions))
    return fail(
        std::format("instruction limit exceeded (limit {})",
                    std::min(limits.instructions, kInterpreterInstructions)));
  if (expression->valueBindings.size() >
      std::min(limits.inputs, kInterpreterInputs))
    return fail(std::format("input slot limit exceeded (limit {})",
                            std::min(limits.inputs, kInterpreterInputs)));
  if (expression->functionBindings.size() >
      std::min(limits.lookups, kInterpreterLookups))
    return fail(std::format("function lookup limit exceeded (limit {})",
                            std::min(limits.lookups, kInterpreterLookups)));
  InterpreterProgram compiled;
  compiled.resultType_ = *numeric;
  std::vector<ValueType> inputTypes;
  for (const auto input : expression->valueBindings) {
    const auto type = graph.OutputType(input);
    const auto *valueType = type ? Get<ValueType>(*type) : nullptr;
    if (!valueType || graph.IsDisabled(input.node))
      return fail("input has no executable numeric value");
    inputTypes.push_back(*valueType);
    if (graph.SampleDependent(input)) {
      if (compiled.textureCount_ >=
          std::min(limits.textures, kInterpreterTextures))
        return fail(
            std::format("texture slot limit exceeded (limit {})",
                        std::min(limits.textures, kInterpreterTextures)));
      compiled.inputs_.push_back(InterpreterTextureInput{
          input, static_cast<std::uint32_t>(compiled.textureCount_++)});
    } else {
      compiled.inputs_.push_back(InterpreterValueInput{input});
    }
  }
  for (const auto &binding : expression->functionBindings) {
    const auto *function = graph.FunctionAt(binding.function);
    if (!function || function->isDisabled ||
        binding.sampledParameter >= function->parameters.size() ||
        function->parameters[binding.sampledParameter].type !=
            ValueType::kScalar ||
        function->result.node >= function->nodes.size() ||
        function->result.output >=
            function->nodes[function->result.node].outputs.size() ||
        function->nodes[function->result.node]
                .outputs[function->result.output]
                .type != GraphValueType{ValueType::kScalar})
      return fail("function cannot be represented by a scalar lookup");
    for (const auto &argument : binding.arguments)
      if (graph.SampleDependent(argument.value))
        return fail("lookup bound argument must be uniform");
    compiled.lookups_.push_back(
        {binding.function, binding.sampledParameter, binding.arguments});
  }
  std::vector<ValueType> stack;
  for (const auto &operation : code) {
    const auto encoding = Encode(operation.op);
    if (!encoding)
      return fail("unsupported operation or unbound context input");
    if (stack.size() < encoding->operands)
      return fail("instruction stack underflow");
    const auto start = stack.size() - encoding->operands;
    ValueType type = ValueType::kScalar;
    for (std::size_t i = start; i < stack.size(); ++i) {
      if (stack[i] == ValueType::kScalar)
        continue;
      if (type != ValueType::kScalar && type != stack[i])
        return fail("instruction mixes incompatible vector widths");
      type = stack[i];
    }
    const auto operandType = type;
    using Op = Program::Op;
    switch (operation.op) {
    case Op::kRef:
      if (operation.index >= inputTypes.size())
        return fail("input slot is out of range");
      type = inputTypes[operation.index];
      break;
    case Op::kCurve:
      if (operation.index >= compiled.lookups_.size() ||
          type != ValueType::kScalar)
        return fail("invalid scalar function lookup");
      break;
    case Op::kMakeVec2:
      type = ValueType::kVec2;
      break;
    case Op::kMakeVec3:
      type = ValueType::kVec3;
      break;
    case Op::kLength:
    case Op::kDistance:
    case Op::kDot:
    case Op::kNot:
    case Op::kLt:
    case Op::kGt:
    case Op::kLe:
    case Op::kGe:
    case Op::kEq:
    case Op::kNe:
    case Op::kAnd:
    case Op::kOr:
      type = ValueType::kScalar;
      break;
    default:
      break;
    }
    stack.resize(start);
    stack.push_back(type);
    compiled.stackSize_ = std::max(compiled.stackSize_, stack.size());
    if (compiled.stackSize_ > std::min(limits.stack, kInterpreterStack))
      return fail(std::format("stack depth limit exceeded (limit {})",
                              std::min(limits.stack, kInterpreterStack)));
    const bool reduction = operation.op == Op::kLength ||
                           operation.op == Op::kDistance ||
                           operation.op == Op::kDot;
    compiled.instructions_.push_back(
        {encoding->opcode, operation.number, operation.index,
         Components(reduction ? operandType : type)});
  }
  if (stack.size() != 1 || stack.front() != compiled.resultType_)
    return fail("instruction result type does not match graph output");
  return compiled;
}
InterpreterProgram InterpreterProgram::Sample(ValueType type, bool texture) {
  InterpreterProgram result;
  if (texture)
    result.inputs_.push_back(InterpreterTextureInput{{}, 0});
  else
    result.inputs_.push_back(InterpreterValueInput{});
  result.textureCount_ = texture ? 1 : 0;
  result.stackSize_ = 1;
  result.resultType_ = type;
  result.instructions_.push_back(
      {InterpreterOpcode::kInput, 0, 0, Components(type)});
  return result;
}
InterpreterProgram InterpreterProgram::Map() {
  auto result = Sample(ValueType::kScalar);
  result.lookups_.push_back({});
  result.instructions_.push_back({InterpreterOpcode::kLookup, 0, 0, 1});
  return result;
}
std::expected<InterpreterProgram, std::string>
InterpreterProgram::Compose(std::span<const bool> textures) {
  if (textures.size() < 2 || textures.size() > 3)
    return std::unexpected("invalid vector width");
  InterpreterProgram result;
  for (std::size_t i = 0; i < textures.size(); ++i) {
    if (textures[i])
      result.inputs_.push_back(InterpreterTextureInput{
          {}, static_cast<std::uint32_t>(result.textureCount_++)});
    else
      result.inputs_.push_back(InterpreterValueInput{});
    result.instructions_.push_back(
        {InterpreterOpcode::kInput, 0, static_cast<std::uint32_t>(i), 1});
  }
  result.instructions_.push_back(
      {textures.size() == 2 ? InterpreterOpcode::kMakeVec2
                            : InterpreterOpcode::kMakeVec3,
       0, 0, static_cast<std::uint32_t>(textures.size())});
  result.stackSize_ = textures.size();
  result.resultType_ =
      textures.size() == 2 ? ValueType::kVec2 : ValueType::kVec3;
  return result;
}
std::span<const InterpreterInstruction>
InterpreterProgram::Instructions() const noexcept {
  return instructions_;
}
std::span<const InterpreterInput> InterpreterProgram::Inputs() const noexcept {
  return inputs_;
}
std::span<const FunctionLookup>
InterpreterProgram::FunctionLookups() const noexcept {
  return lookups_;
}
std::size_t InterpreterProgram::TextureCount() const noexcept {
  return textureCount_;
}
ValueType InterpreterProgram::ResultType() const noexcept {
  return resultType_;
}
std::size_t InterpreterProgram::StackSize() const noexcept {
  return stackSize_;
}
}
