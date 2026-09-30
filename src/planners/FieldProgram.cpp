// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/FieldProgram.h"

#include <algorithm>
#include <array>
#include <format>

namespace BetterEnchantmentEffects {
namespace {
struct Encoding {
  ProgramOpcode opcode;
  std::size_t operands;
};
std::optional<Encoding> Encode(Program::Op op) {
  switch (op) {
  case Program::Op::kNumber:
    return Encoding{ProgramOpcode::kNumber, 0};
  case Program::Op::kMakeVec2:
    return Encoding{ProgramOpcode::kMakeVec2, 2};
  case Program::Op::kMakeVec3:
    return Encoding{ProgramOpcode::kMakeVec3, 3};
  case Program::Op::kRef:
    return Encoding{ProgramOpcode::kInput, 0};
  case Program::Op::kCurve:
    return Encoding{ProgramOpcode::kLookup, 1};
  case Program::Op::kX:
    return std::nullopt;
  case Program::Op::kMean:
    return std::nullopt;
  case Program::Op::kTime:
    return std::nullopt;
  case Program::Op::kNeg:
    return Encoding{ProgramOpcode::kNeg, 1};
  case Program::Op::kNot:
    return Encoding{ProgramOpcode::kNot, 1};
  case Program::Op::kAdd:
    return Encoding{ProgramOpcode::kAdd, 2};
  case Program::Op::kSub:
    return Encoding{ProgramOpcode::kSub, 2};
  case Program::Op::kMul:
    return Encoding{ProgramOpcode::kMul, 2};
  case Program::Op::kDiv:
    return Encoding{ProgramOpcode::kDiv, 2};
  case Program::Op::kLt:
    return Encoding{ProgramOpcode::kLt, 2};
  case Program::Op::kGt:
    return Encoding{ProgramOpcode::kGt, 2};
  case Program::Op::kLe:
    return Encoding{ProgramOpcode::kLe, 2};
  case Program::Op::kGe:
    return Encoding{ProgramOpcode::kGe, 2};
  case Program::Op::kEq:
    return Encoding{ProgramOpcode::kEq, 2};
  case Program::Op::kNe:
    return Encoding{ProgramOpcode::kNe, 2};
  case Program::Op::kAnd:
    return Encoding{ProgramOpcode::kAnd, 2};
  case Program::Op::kOr:
    return Encoding{ProgramOpcode::kOr, 2};
  case Program::Op::kIf:
    return Encoding{ProgramOpcode::kIf, 3};
  case Program::Op::kAbs:
    return Encoding{ProgramOpcode::kAbs, 1};
  case Program::Op::kMin:
    return Encoding{ProgramOpcode::kMin, 2};
  case Program::Op::kMax:
    return Encoding{ProgramOpcode::kMax, 2};
  case Program::Op::kClamp:
    return Encoding{ProgramOpcode::kClamp, 3};
  case Program::Op::kSaturate:
    return Encoding{ProgramOpcode::kSaturate, 1};
  case Program::Op::kFloor:
    return Encoding{ProgramOpcode::kFloor, 1};
  case Program::Op::kCeil:
    return Encoding{ProgramOpcode::kCeil, 1};
  case Program::Op::kFrac:
    return Encoding{ProgramOpcode::kFrac, 1};
  case Program::Op::kSqrt:
    return Encoding{ProgramOpcode::kSqrt, 1};
  case Program::Op::kPow:
    return Encoding{ProgramOpcode::kPow, 2};
  case Program::Op::kSin:
    return Encoding{ProgramOpcode::kSin, 1};
  case Program::Op::kCos:
    return Encoding{ProgramOpcode::kCos, 1};
  case Program::Op::kStep:
    return Encoding{ProgramOpcode::kStep, 2};
  case Program::Op::kSmoothstep:
    return Encoding{ProgramOpcode::kSmoothstep, 3};
  case Program::Op::kLerp:
    return Encoding{ProgramOpcode::kLerp, 3};
  case Program::Op::kLength:
    return Encoding{ProgramOpcode::kLength, 1};
  case Program::Op::kDistance:
    return Encoding{ProgramOpcode::kDistance, 2};
  case Program::Op::kDot:
    return Encoding{ProgramOpcode::kDot, 2};
  case Program::Op::kCross:
    return Encoding{ProgramOpcode::kCross, 2};
  case Program::Op::kNormalize:
    return Encoding{ProgramOpcode::kNormalize, 1};
  }
  return std::nullopt;
}
std::uint32_t Components(ValueType type) {
  return type == ValueType::kScalar ? 1 : type == ValueType::kVec2 ? 2 : 3;
}
ProgramInstruction RenumberInput(ProgramInstruction instruction,
                                 std::span<const std::uint32_t> inputIndex) {
  if (instruction.opcode == ProgramOpcode::kInput &&
      instruction.index < inputIndex.size())
    instruction.index = inputIndex[instruction.index];
  return instruction;
}
std::vector<ProgramInstruction>
InlinedProducerCode(const FieldProgram &producer,
                    std::span<const std::uint32_t> inputIndex,
                    std::uint32_t lookupOffset, std::uint32_t components) {
  std::vector<ProgramInstruction> code;
  code.reserve(producer.Instructions().size() + 2);
  for (const ProgramInstruction &instruction : producer.Instructions()) {
    ProgramInstruction renumbered = RenumberInput(instruction, inputIndex);
    if (renumbered.opcode == ProgramOpcode::kLookup)
      renumbered.index += lookupOffset;
    code.push_back(renumbered);
  }
  if (producer.ResultType() == ValueType::kScalar)
    code.push_back({ProgramOpcode::kSplat, 0, 0, components});
  code.push_back({ProgramOpcode::kQuantize, 0, 0, components});
  return code;
}
}
std::expected<FieldProgram, std::string>
FieldProgram::Compile(const RecipeGraph &graph, OutputRef result,
                      const ProgramLimits &limits) {
  const auto *node = graph.NodeAt(result.node);
  const auto fail =
      [&](std::string message) -> std::expected<FieldProgram, std::string> {
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
      code.size() > std::min(limits.instructions, kProgramInstructions))
    return fail(
        std::format("instruction limit exceeded (limit {})",
                    std::min(limits.instructions, kProgramInstructions)));
  if (expression->valueBindings.size() >
      std::min(limits.inputs, kProgramInputs))
    return fail(std::format("input slot limit exceeded (limit {})",
                            std::min(limits.inputs, kProgramInputs)));
  if (expression->functionBindings.size() >
      std::min(limits.lookups, kProgramLookups))
    return fail(std::format("function lookup limit exceeded (limit {})",
                            std::min(limits.lookups, kProgramLookups)));
  FieldProgram compiled;
  compiled.resultType_ = *numeric;
  std::vector<ValueType> inputTypes;
  for (const auto input : expression->valueBindings) {
    const auto type = graph.OutputType(input);
    const auto *valueType = type ? Get<ValueType>(*type) : nullptr;
    if (!valueType || graph.IsDisabled(input.node))
      return fail("input has no executable numeric value");
    inputTypes.push_back(*valueType);
    if (graph.SampleDependent(input)) {
      if (compiled.textureCount_ >= std::min(limits.textures, kProgramTextures))
        return fail(std::format("texture slot limit exceeded (limit {})",
                                std::min(limits.textures, kProgramTextures)));
      compiled.inputs_.emplace_back(ProgramTextureInput{
          input, static_cast<std::uint32_t>(compiled.textureCount_++)});
    } else {
      compiled.inputs_.emplace_back(ProgramValueInput{input});
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
    if (compiled.stackSize_ > std::min(limits.stack, kProgramStack))
      return fail(std::format("stack depth limit exceeded (limit {})",
                              std::min(limits.stack, kProgramStack)));
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
std::size_t OpcodePops(ProgramOpcode opcode) noexcept {
  using Op = ProgramOpcode;
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
std::size_t StackDepthOf(std::span<const ProgramInstruction> code) noexcept {
  std::size_t depth = 0;
  std::size_t deepest = 0;
  for (const ProgramInstruction &instruction : code) {
    const std::size_t pops = OpcodePops(instruction.opcode);
    depth = (depth > pops ? depth - pops : 0) + 1;
    deepest = std::max(deepest, depth);
  }
  return deepest;
}
std::uint32_t FieldProgram::AppendInput(const ProgramInput &input) {
  const auto index = static_cast<std::uint32_t>(inputs_.size());
  if (const auto *texture = Get<ProgramTextureInput>(input))
    inputs_.emplace_back(ProgramTextureInput{
        texture->output, static_cast<std::uint32_t>(textureCount_++)});
  else
    inputs_.push_back(input);
  return index;
}
InputRenumbering FieldProgram::MergeInputs(const FieldProgram &consumer,
                                           std::size_t inlinedInput,
                                           const FieldProgram &producer) {
  InputRenumbering renumbering{
      std::vector<std::uint32_t>(consumer.inputs_.size()),
      std::vector<std::uint32_t>(producer.inputs_.size())};
  for (std::size_t i = 0; i < consumer.inputs_.size(); ++i)
    if (i != inlinedInput)
      renumbering.consumer[i] = AppendInput(consumer.inputs_[i]);
  for (std::size_t i = 0; i < producer.inputs_.size(); ++i)
    renumbering.producer[i] = AppendInput(producer.inputs_[i]);
  return renumbering;
}
bool FieldProgram::FitsLimits(const ProgramLimits &limits) const {
  return instructions_.size() <=
             std::min(limits.instructions, kProgramInstructions) &&
         inputs_.size() <= std::min(limits.inputs, kProgramInputs) &&
         textureCount_ <= std::min(limits.textures, kProgramTextures) &&
         lookups_.size() <= std::min(limits.lookups, kProgramLookups) &&
         stackSize_ <= std::min(limits.stack, kProgramStack);
}
std::expected<FieldProgram, std::string>
FieldProgram::Inline(const FieldProgram &a_consumer, std::size_t a_input,
                     const FieldProgram &a_producer,
                     const ProgramLimits &a_limits) {
  if (a_input >= a_consumer.inputs_.size() ||
      !Is<ProgramTextureInput>(a_consumer.inputs_[a_input]))
    return std::unexpected("inlined input is not a texture input");
  FieldProgram inlined;
  inlined.resultType_ = a_consumer.resultType_;
  const InputRenumbering renumbering =
      inlined.MergeInputs(a_consumer, a_input, a_producer);
  inlined.lookups_ = a_consumer.lookups_;
  const auto lookupOffset = static_cast<std::uint32_t>(inlined.lookups_.size());
  inlined.lookups_.insert(inlined.lookups_.end(), a_producer.lookups_.begin(),
                          a_producer.lookups_.end());
  for (const ProgramInstruction &instruction : a_consumer.instructions_) {
    if (instruction.opcode == ProgramOpcode::kInput &&
        instruction.index == a_input) {
      const std::vector<ProgramInstruction> produced =
          InlinedProducerCode(a_producer, renumbering.producer, lookupOffset,
                              instruction.components);
      inlined.instructions_.insert(inlined.instructions_.end(),
                                   produced.begin(), produced.end());
    } else {
      inlined.instructions_.push_back(
          RenumberInput(instruction, renumbering.consumer));
    }
  }
  inlined.stackSize_ = StackDepthOf(inlined.instructions_);
  if (!inlined.FitsLimits(a_limits))
    return std::unexpected("inlined program exceeds interpreter limits");
  return inlined;
}
FieldProgram FieldProgram::Sample(ValueType type, bool texture) {
  FieldProgram result;
  if (texture)
    result.inputs_.emplace_back(ProgramTextureInput{{}, 0});
  else
    result.inputs_.emplace_back(ProgramValueInput{});
  result.textureCount_ = texture ? 1 : 0;
  result.stackSize_ = 1;
  result.resultType_ = type;
  result.instructions_.push_back(
      {ProgramOpcode::kInput, 0, 0, Components(type)});
  return result;
}
FieldProgram FieldProgram::AbsoluteDifference() {
  FieldProgram result;
  result.inputs_ = {ProgramTextureInput{{}, 0}, ProgramTextureInput{{}, 1}};
  result.textureCount_ = 2;
  result.stackSize_ = 2;
  result.resultType_ = ValueType::kVec3;
  result.instructions_ = {{ProgramOpcode::kInput, 0, 0, 3},
                          {ProgramOpcode::kInput, 0, 1, 3},
                          {ProgramOpcode::kSub, 0, 0, 3},
                          {ProgramOpcode::kAbs, 0, 0, 3}};
  return result;
}
FieldProgram FieldProgram::Map() {
  auto result = Sample(ValueType::kScalar);
  result.lookups_.push_back({});
  result.instructions_.push_back({ProgramOpcode::kLookup, 0, 0, 1});
  return result;
}
std::expected<FieldProgram, std::string>
FieldProgram::Compose(std::span<const bool> textures) {
  if (textures.size() < 2 || textures.size() > 3)
    return std::unexpected("invalid vector width");
  FieldProgram result;
  for (std::size_t i = 0; i < textures.size(); ++i) {
    if (textures[i])
      result.inputs_.emplace_back(ProgramTextureInput{
          {}, static_cast<std::uint32_t>(result.textureCount_++)});
    else
      result.inputs_.emplace_back(ProgramValueInput{});
    result.instructions_.push_back(
        {ProgramOpcode::kInput, 0, static_cast<std::uint32_t>(i), 1});
  }
  result.instructions_.push_back(
      {textures.size() == 2 ? ProgramOpcode::kMakeVec2
                            : ProgramOpcode::kMakeVec3,
       0, 0, static_cast<std::uint32_t>(textures.size())});
  result.stackSize_ = textures.size();
  result.resultType_ =
      textures.size() == 2 ? ValueType::kVec2 : ValueType::kVec3;
  return result;
}
std::span<const ProgramInstruction>
FieldProgram::Instructions() const noexcept {
  return instructions_;
}
std::span<const ProgramInput> FieldProgram::Inputs() const noexcept {
  return inputs_;
}
std::span<const FunctionLookup> FieldProgram::FunctionLookups() const noexcept {
  return lookups_;
}
std::size_t FieldProgram::TextureCount() const noexcept {
  return textureCount_;
}
ValueType FieldProgram::ResultType() const noexcept { return resultType_; }
std::size_t FieldProgram::StackSize() const noexcept { return stackSize_; }
ProgramCode CodeOf(const FieldProgram &program) {
  return {program.Instructions(), program.Inputs(), program.ResultType()};
}
std::optional<ProgramCode> SegmentCode(const ProgramPack &pack,
                                       std::uint32_t segment) {
  const auto found = SegmentAt(pack.segments, pack.code.size(), segment);
  if (!found)
    return std::nullopt;
  return ProgramCode{std::span{pack.code}.subspan(found->first, found->count),
                     pack.inputs, ValueType::kVec3};
}
std::optional<ProgramSegment>
SegmentAt(std::span<const ProgramSegment> segments, std::size_t codeSize,
          std::uint32_t segment) {
  if (segment >= segments.size())
    return std::nullopt;
  const auto found = segments[segment];
  if (found.first > codeSize || found.count > codeSize - found.first)
    return std::nullopt;
  return found;
}
std::expected<ProgramPack, std::string>
PackPrograms(std::span<const FieldProgram *const> programs,
             const ProgramLimits &limits) {
  ProgramPack pack;
  for (const auto *program : programs) {
    if (!program)
      return std::unexpected("packed program is missing");
    const auto inputOffset = static_cast<std::uint32_t>(pack.inputs.size());
    const auto lookupOffset = static_cast<std::uint32_t>(pack.lookupCount);
    const auto textureOffset = static_cast<std::uint32_t>(pack.textureCount);
    for (const auto &input : program->Inputs()) {
      if (const auto *texture = Get<ProgramTextureInput>(input))
        pack.inputs.emplace_back(ProgramTextureInput{
            texture->output, texture->slot + textureOffset});
      else
        pack.inputs.push_back(input);
    }
    pack.textureCount += program->TextureCount();
    pack.lookupCount += program->FunctionLookups().size();
    const ProgramSegment segment{
        static_cast<std::uint32_t>(pack.code.size()),
        static_cast<std::uint32_t>(program->Instructions().size())};
    for (auto instruction : program->Instructions()) {
      if (instruction.opcode == ProgramOpcode::kInput)
        instruction.index += inputOffset;
      else if (instruction.opcode == ProgramOpcode::kLookup)
        instruction.index += lookupOffset;
      pack.code.push_back(instruction);
    }
    pack.segments.push_back(segment);
    if (program->StackSize() > std::min(limits.stack, kProgramStack))
      return std::unexpected("packed program exceeds the interpreter stack");
  }
  if (pack.code.size() > std::min(limits.instructions, kProgramInstructions) ||
      pack.inputs.size() > std::min(limits.inputs, kProgramInputs) ||
      pack.textureCount > std::min(limits.textures, kProgramTextures) ||
      pack.lookupCount > std::min(limits.lookups, kProgramLookups))
    return std::unexpected("packed programs exceed interpreter limits");
  return pack;
}
}
