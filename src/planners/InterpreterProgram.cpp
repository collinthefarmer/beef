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
std::size_t InterpreterPops(InterpreterOpcode opcode) noexcept {
  using Op = InterpreterOpcode;
  switch (opcode) {
  case Op::kNumber:
  case Op::kInput:
    return 0;
  case Op::kLookup:
  case Op::kNeg:
  case Op::kNot:
  case Op::kAbs:
  case Op::kSaturate:
  case Op::kFloor:
  case Op::kCeil:
  case Op::kFrac:
  case Op::kSqrt:
  case Op::kSin:
  case Op::kCos:
  case Op::kLength:
  case Op::kNormalize:
  case Op::kQuantize:
  case Op::kSplat:
    return 1;
  case Op::kMakeVec2:
  case Op::kAdd:
  case Op::kSub:
  case Op::kMul:
  case Op::kDiv:
  case Op::kLt:
  case Op::kGt:
  case Op::kLe:
  case Op::kGe:
  case Op::kEq:
  case Op::kNe:
  case Op::kAnd:
  case Op::kOr:
  case Op::kMin:
  case Op::kMax:
  case Op::kPow:
  case Op::kStep:
  case Op::kDistance:
  case Op::kDot:
  case Op::kCross:
    return 2;
  case Op::kMakeVec3:
  case Op::kIf:
  case Op::kClamp:
  case Op::kSmoothstep:
  case Op::kLerp:
    return 3;
  }
  return 3;
}
std::expected<InterpreterProgram, std::string> InterpreterProgram::Inline(
    const InterpreterProgram &a_consumer, std::size_t a_input,
    const InterpreterProgram &a_producer, const InterpreterLimits &a_limits) {
  if (a_input >= a_consumer.inputs_.size() ||
      !Is<InterpreterTextureInput>(a_consumer.inputs_[a_input]))
    return std::unexpected("inlined input is not a texture input");
  InterpreterProgram fused;
  fused.resultType_ = a_consumer.resultType_;
  std::vector<std::uint32_t> consumerIndex(a_consumer.inputs_.size());
  std::vector<std::uint32_t> producerIndex(a_producer.inputs_.size());
  const auto append = [&](const InterpreterInput &input) {
    const auto index = static_cast<std::uint32_t>(fused.inputs_.size());
    if (const auto *texture = Get<InterpreterTextureInput>(input))
      fused.inputs_.push_back(InterpreterTextureInput{
          texture->output, static_cast<std::uint32_t>(fused.textureCount_++)});
    else
      fused.inputs_.push_back(input);
    return index;
  };
  for (std::size_t i = 0; i < a_consumer.inputs_.size(); ++i)
    if (i != a_input)
      consumerIndex[i] = append(a_consumer.inputs_[i]);
  for (std::size_t i = 0; i < a_producer.inputs_.size(); ++i)
    producerIndex[i] = append(a_producer.inputs_[i]);
  fused.lookups_ = a_consumer.lookups_;
  const auto lookupOffset = static_cast<std::uint32_t>(fused.lookups_.size());
  fused.lookups_.insert(fused.lookups_.end(), a_producer.lookups_.begin(),
                        a_producer.lookups_.end());
  const bool scalarProducer = a_producer.resultType_ == ValueType::kScalar;
  for (const auto &instruction : a_consumer.instructions_) {
    if (instruction.opcode == InterpreterOpcode::kInput &&
        instruction.index == a_input) {
      for (auto produced : a_producer.instructions_) {
        if (produced.opcode == InterpreterOpcode::kInput)
          produced.index = produced.index < producerIndex.size()
                               ? producerIndex[produced.index]
                               : produced.index;
        else if (produced.opcode == InterpreterOpcode::kLookup)
          produced.index += lookupOffset;
        fused.instructions_.push_back(produced);
      }
      if (scalarProducer)
        fused.instructions_.push_back(
            {InterpreterOpcode::kSplat, 0, 0, instruction.components});
      fused.instructions_.push_back(
          {InterpreterOpcode::kQuantize, 0, 0, instruction.components});
      continue;
    }
    auto kept = instruction;
    if (kept.opcode == InterpreterOpcode::kInput &&
        kept.index < consumerIndex.size())
      kept.index = consumerIndex[kept.index];
    fused.instructions_.push_back(kept);
  }
  std::size_t depth = 0;
  for (const auto &instruction : fused.instructions_) {
    const auto pops = InterpreterPops(instruction.opcode);
    depth = (depth > pops ? depth - pops : 0) + 1;
    fused.stackSize_ = std::max(fused.stackSize_, depth);
  }
  if (fused.instructions_.size() >
          std::min(a_limits.instructions, kInterpreterInstructions) ||
      fused.inputs_.size() > std::min(a_limits.inputs, kInterpreterInputs) ||
      fused.textureCount_ > std::min(a_limits.textures, kInterpreterTextures) ||
      fused.lookups_.size() > std::min(a_limits.lookups, kInterpreterLookups) ||
      fused.stackSize_ > std::min(a_limits.stack, kInterpreterStack))
    return std::unexpected("inlined program exceeds interpreter limits");
  return fused;
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
InterpreterProgram InterpreterProgram::AbsoluteDifference() {
  InterpreterProgram result;
  result.inputs_ = {InterpreterTextureInput{{}, 0},
                    InterpreterTextureInput{{}, 1}};
  result.textureCount_ = 2;
  result.stackSize_ = 2;
  result.resultType_ = ValueType::kVec3;
  result.instructions_ = {{InterpreterOpcode::kInput, 0, 0, 3},
                          {InterpreterOpcode::kInput, 0, 1, 3},
                          {InterpreterOpcode::kSub, 0, 0, 3},
                          {InterpreterOpcode::kAbs, 0, 0, 3}};
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
std::expected<InterpreterPack, std::string>
PackInterpreters(std::span<const InterpreterProgram *const> programs,
                 const InterpreterLimits &limits) {
  InterpreterPack pack;
  for (const auto *program : programs) {
    if (!program)
      return std::unexpected("packed program is missing");
    const auto inputOffset = static_cast<std::uint32_t>(pack.inputs.size());
    const auto lookupOffset = static_cast<std::uint32_t>(pack.lookupCount);
    const auto textureOffset = static_cast<std::uint32_t>(pack.textureCount);
    for (const auto &input : program->Inputs()) {
      if (const auto *texture = Get<InterpreterTextureInput>(input))
        pack.inputs.push_back(InterpreterTextureInput{
            texture->output, texture->slot + textureOffset});
      else
        pack.inputs.push_back(input);
    }
    pack.textureCount += program->TextureCount();
    pack.lookupCount += program->FunctionLookups().size();
    const InterpreterSegment segment{
        static_cast<std::uint32_t>(pack.code.size()),
        static_cast<std::uint32_t>(program->Instructions().size())};
    for (auto instruction : program->Instructions()) {
      if (instruction.opcode == InterpreterOpcode::kInput)
        instruction.index += inputOffset;
      else if (instruction.opcode == InterpreterOpcode::kLookup)
        instruction.index += lookupOffset;
      pack.code.push_back(instruction);
    }
    pack.segments.push_back(segment);
    if (program->StackSize() > std::min(limits.stack, kInterpreterStack))
      return std::unexpected("packed program exceeds the interpreter stack");
  }
  if (pack.code.size() >
          std::min(limits.instructions, kInterpreterInstructions) ||
      pack.inputs.size() > std::min(limits.inputs, kInterpreterInputs) ||
      pack.textureCount > std::min(limits.textures, kInterpreterTextures) ||
      pack.lookupCount > std::min(limits.lookups, kInterpreterLookups))
    return std::unexpected("packed programs exceed interpreter limits");
  return pack;
}
}
