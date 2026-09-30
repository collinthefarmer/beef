// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/RecipeGraph.h"

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kProgramInstructions = 256;
inline constexpr std::size_t kProgramInputs = 16;
inline constexpr std::size_t kProgramTextures = 8;
inline constexpr std::size_t kProgramLookups = 4;
inline constexpr std::size_t kProgramStack = 32;

enum class ProgramOpcode : std::uint8_t {
  kNumber = 0,
  kMakeVec2 = 1,
  kMakeVec3 = 2,
  kInput = 3,
  kLookup = 4,
  kNeg = 8,
  kNot = 9,
  kAdd = 10,
  kSub = 11,
  kMul = 12,
  kDiv = 13,
  kLt = 14,
  kGt = 15,
  kLe = 16,
  kGe = 17,
  kEq = 18,
  kNe = 19,
  kAnd = 20,
  kOr = 21,
  kIf = 22,
  kAbs = 23,
  kMin = 24,
  kMax = 25,
  kClamp = 26,
  kSaturate = 27,
  kFloor = 28,
  kCeil = 29,
  kFrac = 30,
  kSqrt = 31,
  kPow = 32,
  kSin = 33,
  kCos = 34,
  kStep = 35,
  kSmoothstep = 36,
  kLerp = 37,
  kLength = 38,
  kDistance = 39,
  kDot = 40,
  kCross = 41,
  kNormalize = 42,
  kQuantize = 43,
  kSplat = 44
};
struct ProgramInstruction {
  ProgramOpcode opcode;
  float number = 0;
  std::uint32_t index = 0;
  std::uint32_t components = 1;
  [[nodiscard]] bool operator==(const ProgramInstruction &) const = default;
  [[nodiscard]] auto operator<=>(const ProgramInstruction &) const = default;
};
struct ProgramValueInput {
  OutputRef output;
  [[nodiscard]] bool operator==(const ProgramValueInput &) const = default;
};
struct ProgramTextureInput {
  OutputRef output;
  std::uint32_t slot = 0;
  [[nodiscard]] bool operator==(const ProgramTextureInput &) const = default;
};
using ProgramInput = std::variant<ProgramValueInput, ProgramTextureInput>;
struct FunctionLookup {
  FunctionId function = 0;
  std::size_t sampledParameter = 0;
  std::vector<BoundFunctionArgument> arguments;
  [[nodiscard]] bool operator==(const FunctionLookup &) const = default;
};
struct ProgramLimits {
  std::size_t instructions = kProgramInstructions;
  std::size_t inputs = kProgramInputs;
  std::size_t textures = kProgramTextures;
  std::size_t lookups = kProgramLookups;
  std::size_t stack = kProgramStack;
};
struct InputRenumbering {
  std::vector<std::uint32_t> consumer;
  std::vector<std::uint32_t> producer;
};
[[nodiscard]] std::size_t OpcodePops(ProgramOpcode opcode) noexcept;
[[nodiscard]] std::size_t
StackDepthOf(std::span<const ProgramInstruction> code) noexcept;
[[nodiscard]] bool SamplesAsLookup(const FunctionDefinition &function,
                                   std::size_t sampledParameter);
class FieldProgram {
public:
  [[nodiscard]] static FieldProgram Sample(ValueType type, bool texture = true);
  [[nodiscard]] static FieldProgram Map();
  [[nodiscard]] static FieldProgram AbsoluteDifference();
  [[nodiscard]] static std::expected<FieldProgram, std::string>
  Compose(std::span<const bool> textures);
  [[nodiscard]] static std::expected<FieldProgram, std::string>
  Compile(const RecipeGraph &a_graph, OutputRef a_result,
          const ProgramLimits &a_limits = {});
  [[nodiscard]] static std::expected<FieldProgram, std::string>
  Inline(const FieldProgram &a_consumer, std::size_t a_input,
         const FieldProgram &a_producer, const ProgramLimits &a_limits = {});
  [[nodiscard]] std::span<const ProgramInstruction>
  Instructions() const noexcept;
  [[nodiscard]] std::span<const ProgramInput> Inputs() const noexcept;
  [[nodiscard]] std::span<const FunctionLookup>
  FunctionLookups() const noexcept;
  [[nodiscard]] std::size_t TextureCount() const noexcept;
  [[nodiscard]] ValueType ResultType() const noexcept;
  [[nodiscard]] std::size_t StackSize() const noexcept;
  [[nodiscard]] bool operator==(const FieldProgram &) const = default;

private:
  FieldProgram() = default;
  std::uint32_t AppendInput(const ProgramInput &input);
  InputRenumbering MergeInputs(const FieldProgram &consumer,
                               std::size_t inlinedInput,
                               const FieldProgram &producer);
  [[nodiscard]] bool FitsLimits(const ProgramLimits &limits) const;
  [[nodiscard]] std::expected<std::vector<ValueType>, std::string>
  AppendExpressionInputs(const RecipeGraph &graph,
                         std::span<const OutputRef> bindings,
                         std::size_t textureLimit);
  [[nodiscard]] std::expected<void, std::string>
  AppendFunctionLookups(const RecipeGraph &graph,
                        std::span<const BoundFunction> bindings);
  [[nodiscard]] std::expected<void, std::string>
  AppendInstructions(std::span<const Program::Node> code,
                     std::span<const ValueType> inputTypes,
                     std::size_t stackLimit);
  std::vector<ProgramInstruction> instructions_;
  std::vector<ProgramInput> inputs_;
  std::vector<FunctionLookup> lookups_;
  std::size_t textureCount_ = 0;
  std::size_t stackSize_ = 0;
  ValueType resultType_ = ValueType::kScalar;
};
struct ProgramSegment {
  std::uint32_t first = 0;
  std::uint32_t count = 0;
  [[nodiscard]] bool operator==(const ProgramSegment &) const = default;
  [[nodiscard]] auto operator<=>(const ProgramSegment &) const = default;
};
struct ProgramPack {
  std::vector<ProgramInstruction> code;
  std::vector<ProgramInput> inputs;
  std::size_t textureCount = 0;
  std::size_t lookupCount = 0;
  std::vector<ProgramSegment> segments;
};
struct ProgramCode {
  std::span<const ProgramInstruction> instructions;
  std::span<const ProgramInput> inputs;
  ValueType result = ValueType::kScalar;
};
[[nodiscard]] ProgramCode CodeOf(const FieldProgram &program);
[[nodiscard]] std::optional<ProgramCode> SegmentCode(const ProgramPack &pack,
                                                     std::uint32_t segment);
[[nodiscard]] std::optional<ProgramSegment>
SegmentAt(std::span<const ProgramSegment> segments, std::size_t codeSize,
          std::uint32_t segment);
[[nodiscard]] std::expected<ProgramPack, std::string>
PackPrograms(std::span<const FieldProgram *const> programs,
             const ProgramLimits &limits = {});
}
