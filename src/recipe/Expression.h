#pragma once

#include "Core.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::size_t kMaxExpressionLength = 4096;
inline constexpr std::size_t kMaxExpressionDepth = 32;
inline constexpr std::size_t kMaxExpressionOps = 256;

using RefTyper =
    std::function<std::optional<ValueType>(std::string_view a_name)>;

struct NumericLiteral {
  std::size_t offset = 0;
  std::size_t length = 0;
  float value = 0.0f;
};

struct NumericLiteralSelection {
  std::string expression;
  std::size_t index = 0;
};

struct ExpressionRename {
  std::string from;
  std::string to;
};

class Program {
public:
  [[nodiscard]] static std::expected<Program, std::string>
  Parse(std::string_view a_text);

  [[nodiscard]] std::span<const std::string> References() const noexcept {
    return refs_;
  }
  [[nodiscard]] std::span<const std::string> Curves() const noexcept {
    return curves_;
  }
  [[nodiscard]] std::span<const NumericLiteral>
  NumericLiterals() const noexcept {
    return literals_;
  }
  [[nodiscard]] bool UsesTime() const noexcept { return usesTime_; }
  [[nodiscard]] bool UsesX() const noexcept { return usesX_; }
  [[nodiscard]] bool UsesMean() const noexcept { return usesMean_; }
  [[nodiscard]] std::size_t OpCount() const noexcept { return code_.size(); }
  [[nodiscard]] bool Constant() const noexcept {
    return refs_.empty() && curves_.empty() && !usesTime_ && !usesX_ &&
           !usesMean_;
  }

  [[nodiscard]] std::expected<ValueType, std::string>
  Check(const RefTyper &a_types, ValueType a_xType = ValueType::kScalar) const;

  struct Inputs {
    std::span<const Value> refs;
    std::span<const Program *const> curves;
    float time = 0.0f;
    Value x = 0.0f;
    float mean = 0.5f;
  };

  [[nodiscard]] Value Evaluate(const Inputs &a_inputs) const noexcept;

  enum class Op : std::uint8_t {
    kNumber,
    kMakeVec2,
    kMakeVec3,
    kRef,
    kCurve,
    kX,
    kMean,
    kTime,
    kNeg,
    kNot,
    kAdd,
    kSub,
    kMul,
    kDiv,
    kLt,
    kGt,
    kLe,
    kGe,
    kEq,
    kNe,
    kAnd,
    kOr,
    kIf,
    kAbs,
    kMin,
    kMax,
    kClamp,
    kSaturate,
    kFloor,
    kCeil,
    kFrac,
    kSqrt,
    kPow,
    kSin,
    kCos,
    kStep,
    kSmoothstep,
    kLerp,
  };
  struct Node {
    Op op = Op::kNumber;
    float number = 0.0f;
    std::uint32_t index = 0;
  };
  [[nodiscard]] std::span<const Node> Code() const noexcept { return code_; }

private:
  std::vector<Node> code_;
  std::vector<std::string> refs_;
  std::vector<std::string> curves_;
  std::vector<NumericLiteral> literals_;
  bool usesTime_ = false;
  bool usesX_ = false;
  bool usesMean_ = false;
  friend class ExpressionParser;
};

[[nodiscard]] std::expected<std::string, std::string>
ReplaceNumericLiteral(std::string_view a_current,
                      const NumericLiteralSelection &a_selection,
                      std::string_view a_replacement);

[[nodiscard]] std::expected<Program, std::string>
ParseCurve(std::string_view a_text);

[[nodiscard]] std::vector<std::string_view> FunctionNames();

[[nodiscard]] float ApplyCurve(const Program &a_curve, float a_x,
                               float a_mean = 0.5f) noexcept;

[[nodiscard]] std::string
RenameInExpression(std::string_view a_text,
                   std::span<const ExpressionRename> a_renames, bool a_curve);

[[nodiscard]] std::string RenameInExpression(std::string_view a_text,
                                             std::string_view a_from,
                                             std::string_view a_to,
                                             bool a_curve);

[[nodiscard]] std::string ExpressionSummary(std::string_view a_text,
                                            std::size_t a_max = 48);
}
