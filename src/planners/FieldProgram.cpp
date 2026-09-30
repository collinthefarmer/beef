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
ProgramLimits ClampedLimits(const ProgramLimits &limits) {
  return {std::min(limits.instructions, kProgramInstructions),
          std::min(limits.inputs, kProgramInputs),
          std::min(limits.textures, kProgramTextures),
          std::min(limits.lookups, kProgramLookups),
          std::min(limits.stack, kProgramStack)};
}
std::expected<void, std::string>
ValidateExpressionLimits(const BoundExpression &expression,
                         const ProgramLimits &limits) {
  const std::span<const Program::Node> code = expression.program.Code();
  if (code.empty() || code.size() > limits.instructions)
    return std::unexpected(std::format("instruction limit exceeded (limit {})",
                                       limits.instructions));
  if (expression.valueBindings.size() > limits.inputs)
    return std::unexpected(
        std::format("input slot limit exceeded (limit {})", limits.inputs));
  if (expression.functionBindings.size() > limits.lookups)
    return std::unexpected(std::format(
        "function lookup limit exceeded (limit {})", limits.lookups));
  return {};
}
std::expected<ValueType, std::string>
OperandTypeOf(std::span<const ValueType> operands) {
  ValueType type = ValueType::kScalar;
  for (const ValueType operand : operands) {
    if (operand == ValueType::kScalar)
      continue;
    if (type != ValueType::kScalar && type != operand)
      return std::unexpected("instruction mixes incompatible vector widths");
    type = operand;
  }
  return type;
}
std::expected<ValueType, std::string>
ResultTypeOf(const Program::Node &node, ValueType operandType,
             std::span<const ValueType> inputTypes, std::size_t lookupCount) {
  using Op = Program::Op;
  switch (node.op) {
  case Op::kRef:
    if (node.index >= inputTypes.size())
      return std::unexpected("input slot is out of range");
    return inputTypes[node.index];
  case Op::kCurve:
    if (node.index >= lookupCount || operandType != ValueType::kScalar)
      return std::unexpected("invalid scalar function lookup");
    return operandType;
  case Op::kMakeVec2:
    return ValueType::kVec2;
  case Op::kMakeVec3:
    return ValueType::kVec3;
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
    return ValueType::kScalar;
  default:
    return operandType;
  }
}
ProgramInstruction InstructionFor(const Program::Node &node,
                                  const Encoding &encoding,
                                  ValueType operandType, ValueType resultType) {
  const bool reduction = node.op == Program::Op::kLength ||
                         node.op == Program::Op::kDistance ||
                         node.op == Program::Op::kDot;
  return {encoding.opcode, node.number, node.index,
          Components(reduction ? operandType : resultType)};
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
  const RecipeNode *node = graph.NodeAt(result.node);
  const auto fail = [&](const std::string &message)
      -> std::expected<FieldProgram, std::string> {
    return std::unexpected(
        std::format("{}: {}", node ? node->displayName : "program", message));
  };
  const BoundExpression *expression = graph.ExpressionAt(result.node);
  const std::optional<GraphValueType> outputType = graph.OutputType(result);
  const ValueType *numeric = outputType ? Get<ValueType>(*outputType) : nullptr;
  if (!node || graph.IsDisabled(result.node) || result.output != 0 ||
      !expression || !numeric)
    return fail("requires an enabled numeric expression output");
  const ProgramLimits clamped = ClampedLimits(limits);
  if (const auto fits = ValidateExpressionLimits(*expression, clamped); !fits)
    return fail(fits.error());
  FieldProgram compiled;
  compiled.resultType_ = *numeric;
  const auto inputTypes = compiled.AppendExpressionInputs(
      graph, expression->valueBindings, clamped.textures);
  if (!inputTypes)
    return fail(inputTypes.error());
  if (const auto lookups =
          compiled.AppendFunctionLookups(graph, expression->functionBindings);
      !lookups)
    return fail(lookups.error());
  if (const auto appended = compiled.AppendInstructions(
          expression->program.Code(), *inputTypes, clamped.stack);
      !appended)
    return fail(appended.error());
  return compiled;
}
std::expected<std::vector<ValueType>, std::string>
FieldProgram::AppendExpressionInputs(const RecipeGraph &graph,
                                     std::span<const OutputRef> bindings,
                                     std::size_t textureLimit) {
  std::vector<ValueType> inputTypes;
  for (const OutputRef input : bindings) {
    const std::optional<GraphValueType> type = graph.OutputType(input);
    const ValueType *valueType = type ? Get<ValueType>(*type) : nullptr;
    if (!valueType || graph.IsDisabled(input.node))
      return std::unexpected("input has no executable numeric value");
    inputTypes.push_back(*valueType);
    if (graph.SampleDependent(input)) {
      if (textureCount_ >= textureLimit)
        return std::unexpected(std::format(
            "texture slot limit exceeded (limit {})", textureLimit));
      inputs_.emplace_back(ProgramTextureInput{
          input, static_cast<std::uint32_t>(textureCount_++)});
    } else {
      inputs_.emplace_back(ProgramValueInput{input});
    }
  }
  return inputTypes;
}
std::expected<void, std::string>
FieldProgram::AppendFunctionLookups(const RecipeGraph &graph,
                                    std::span<const BoundFunction> bindings) {
  for (const BoundFunction &binding : bindings) {
    const FunctionDefinition *function = graph.FunctionAt(binding.function);
    if (!function || function->isDisabled ||
        !SamplesAsLookup(*function, binding.sampledParameter))
      return std::unexpected(
          "function cannot be represented by a scalar lookup");
    for (const BoundFunctionArgument &argument : binding.arguments)
      if (graph.SampleDependent(argument.value))
        return std::unexpected("lookup bound argument must be uniform");
    lookups_.push_back(
        {binding.function, binding.sampledParameter, binding.arguments});
  }
  return {};
}
std::expected<void, std::string>
FieldProgram::AppendInstructions(std::span<const Program::Node> code,
                                 std::span<const ValueType> inputTypes,
                                 std::size_t stackLimit) {
  std::vector<ValueType> stack;
  for (const Program::Node &node : code) {
    const std::optional<Encoding> encoding = Encode(node.op);
    if (!encoding)
      return std::unexpected("unsupported operation or unbound context input");
    if (stack.size() < encoding->operands)
      return std::unexpected("instruction stack underflow");
    const std::size_t start = stack.size() - encoding->operands;
    const std::expected<ValueType, std::string> operandType =
        OperandTypeOf(std::span<const ValueType>{stack}.subspan(start));
    if (!operandType)
      return std::unexpected(operandType.error());
    const std::expected<ValueType, std::string> resultType =
        ResultTypeOf(node, *operandType, inputTypes, lookups_.size());
    if (!resultType)
      return std::unexpected(resultType.error());
    stack.resize(start);
    stack.push_back(*resultType);
    stackSize_ = std::max(stackSize_, stack.size());
    if (stackSize_ > stackLimit)
      return std::unexpected(
          std::format("stack depth limit exceeded (limit {})", stackLimit));
    instructions_.push_back(
        InstructionFor(node, *encoding, *operandType, *resultType));
  }
  if (stack.size() != 1 || stack.front() != resultType_)
    return std::unexpected(
        "instruction result type does not match graph output");
  return {};
}
bool SamplesAsLookup(const FunctionDefinition &function,
                     std::size_t sampledParameter) {
  return sampledParameter < function.parameters.size() &&
         function.parameters[sampledParameter].type == ValueType::kScalar &&
         FunctionResultTypeOf(function) ==
             std::optional<GraphValueType>{ValueType::kScalar};
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
  const ProgramLimits clamped = ClampedLimits(limits);
  return instructions_.size() <= clamped.instructions &&
         inputs_.size() <= clamped.inputs &&
         textureCount_ <= clamped.textures &&
         lookups_.size() <= clamped.lookups && stackSize_ <= clamped.stack;
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
    return std::unexpected("inlined program exceeds program limits");
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
  const ProgramLimits clamped = ClampedLimits(limits);
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
    if (program->StackSize() > clamped.stack)
      return std::unexpected("packed program exceeds the program stack");
  }
  if (pack.code.size() > clamped.instructions ||
      pack.inputs.size() > clamped.inputs ||
      pack.textureCount > clamped.textures ||
      pack.lookupCount > clamped.lookups)
    return std::unexpected("packed programs exceed program limits");
  return pack;
}
}
