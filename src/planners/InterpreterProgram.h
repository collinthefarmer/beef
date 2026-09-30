// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/RecipeGraph.h"

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kInterpreterInstructions = 256;
inline constexpr std::size_t kInterpreterInputs = 16;
inline constexpr std::size_t kInterpreterTextures = 8;
inline constexpr std::size_t kInterpreterLookups = 4;
inline constexpr std::size_t kInterpreterStack = 32;

enum class InterpreterOpcode : std::uint8_t {
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
struct InterpreterInstruction {
  InterpreterOpcode opcode;
  float number = 0;
  std::uint32_t index = 0;
  std::uint32_t components = 1;
  [[nodiscard]] bool operator==(const InterpreterInstruction &) const = default;
};
struct InterpreterValueInput {
  OutputRef output;
  [[nodiscard]] bool operator==(const InterpreterValueInput &) const = default;
};
struct InterpreterTextureInput {
  OutputRef output;
  std::uint32_t slot = 0;
  [[nodiscard]] bool
  operator==(const InterpreterTextureInput &) const = default;
};
using InterpreterInput =
    std::variant<InterpreterValueInput, InterpreterTextureInput>;
struct FunctionLookup {
  FunctionId function = 0;
  std::size_t sampledParameter = 0;
  std::vector<BoundFunctionArgument> arguments;
  [[nodiscard]] bool operator==(const FunctionLookup &) const = default;
};
struct InterpreterLimits {
  std::size_t instructions = kInterpreterInstructions;
  std::size_t inputs = kInterpreterInputs;
  std::size_t textures = kInterpreterTextures;
  std::size_t lookups = kInterpreterLookups;
  std::size_t stack = kInterpreterStack;
};
[[nodiscard]] std::size_t InterpreterPops(InterpreterOpcode opcode) noexcept;
class InterpreterProgram {
public:
  [[nodiscard]] static InterpreterProgram Sample(ValueType type,
                                                 bool texture = true);
  [[nodiscard]] static InterpreterProgram Map();
  [[nodiscard]] static InterpreterProgram AbsoluteDifference();
  [[nodiscard]] static std::expected<InterpreterProgram, std::string>
  Compose(std::span<const bool> textures);
  [[nodiscard]] static std::expected<InterpreterProgram, std::string>
  Compile(const RecipeGraph &a_graph, OutputRef a_result,
          const InterpreterLimits &a_limits = {});
  [[nodiscard]] static std::expected<InterpreterProgram, std::string>
  Inline(const InterpreterProgram &a_consumer, std::size_t a_input,
         const InterpreterProgram &a_producer,
         const InterpreterLimits &a_limits = {});
  [[nodiscard]] std::span<const InterpreterInstruction>
  Instructions() const noexcept;
  [[nodiscard]] std::span<const InterpreterInput> Inputs() const noexcept;
  [[nodiscard]] std::span<const FunctionLookup>
  FunctionLookups() const noexcept;
  [[nodiscard]] std::size_t TextureCount() const noexcept;
  [[nodiscard]] ValueType ResultType() const noexcept;
  [[nodiscard]] std::size_t StackSize() const noexcept;
  [[nodiscard]] bool operator==(const InterpreterProgram &) const = default;

private:
  InterpreterProgram() = default;
  std::vector<InterpreterInstruction> instructions_;
  std::vector<InterpreterInput> inputs_;
  std::vector<FunctionLookup> lookups_;
  std::size_t textureCount_ = 0;
  std::size_t stackSize_ = 0;
  ValueType resultType_ = ValueType::kScalar;
};
}
