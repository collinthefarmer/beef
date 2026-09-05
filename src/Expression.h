#pragma once

// The recipe language. One grammar serves signals (evaluated per tick on
// the CPU), masks (per texel) and curves (a function of x). A Program is
// the compiled form: a bounded postfix op list that the CPU evaluator runs
// here and the interpreter shader runs per texel in the compositor.
//
//   expr   := or
//   or     := and ("or" and)*
//   and    := cmp ("and" cmp)*
//   cmp    := add (("<" | ">" | "<=" | ">=" | "==" | "!=") add)?
//   add    := mul (("+" | "-") mul)*
//   mul    := unary (("*" | "/") unary)*
//   unary  := ("-" | "not") unary | atom
//   atom   := number | "[" expr "," expr ("," expr)? "]" | "(" expr ")"
//           | "@" name                      a signal, source or mask
//           | "@" name "(" expr ")"         a declared curve applied to a value
//           | function "(" expr ("," expr)* ")"
//           | "x" | "mean" | "time" | "pi"
//
// Functions: abs min max clamp saturate floor ceil frac sqrt pow sin cos
// step smoothstep lerp if. Comparisons yield 0 or 1. Arithmetic is
// component-wise on vectors and a scalar broadcasts; a vec2 and a vec3
// never mix. Division by zero is 0. There is no "^".

#include "Core.h"

#include <cstddef>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace WornEnchantmentPBR
{
	// Hard limits: an input past them is an error, never a deep stack.
	inline constexpr std::size_t kMaxExpressionLength = 4096;
	inline constexpr std::size_t kMaxExpressionDepth = 32;
	inline constexpr std::size_t kMaxExpressionOps = 256;

	// The types of the names a program reads, for type checking.
	using RefTyper = std::function<std::optional<ValueType>(std::string_view a_name)>;

	class Program
	{
	public:
		[[nodiscard]] static std::expected<Program, std::string> Parse(std::string_view a_text);

		// Names read with "@name", in first-use order; the curves called with "@name(...)".
		[[nodiscard]] std::span<const std::string> References() const noexcept { return refs_; }
		[[nodiscard]] std::span<const std::string> Curves() const noexcept { return curves_; }
		[[nodiscard]] bool                         UsesTime() const noexcept { return usesTime_; }
		[[nodiscard]] bool                         UsesX() const noexcept { return usesX_; }
		[[nodiscard]] bool                         UsesMean() const noexcept { return usesMean_; }
		[[nodiscard]] std::size_t                  OpCount() const noexcept { return code_.size(); }
		// Constant: reads nothing that changes between ticks.
		[[nodiscard]] bool Constant() const noexcept { return refs_.empty() && curves_.empty() && !usesTime_ && !usesX_ && !usesMean_; }

		// The result type given the types of what it reads; an error names
		// the mismatch. Curves are scalar in, scalar out.
		[[nodiscard]] std::expected<ValueType, std::string> Check(const RefTyper& a_types, ValueType a_xType = ValueType::kScalar) const;

		struct Inputs
		{
			std::span<const Value>          refs;    // one per References()
			std::span<const Program* const> curves;  // one per Curves(); null evaluates to its argument
			float                           time = 0.0f;
			Value                           x = 0.0f;
			float                           mean = 0.5f;
		};

		// Never throws; a type mismatch at runtime yields 0.
		[[nodiscard]] Value Evaluate(const Inputs& a_inputs) const noexcept;

		enum class Op : std::uint8_t
		{
			kNumber, kMakeVec2, kMakeVec3, kRef, kCurve, kX, kMean, kTime,
			kNeg, kNot,
			kAdd, kSub, kMul, kDiv,
			kLt, kGt, kLe, kGe, kEq, kNe, kAnd, kOr,
			kIf,
			kAbs, kMin, kMax, kClamp, kSaturate, kFloor, kCeil, kFrac, kSqrt, kPow, kSin, kCos,
			kStep, kSmoothstep, kLerp,
		};
		struct Node
		{
			Op            op = Op::kNumber;
			float         number = 0.0f;
			std::uint32_t index = 0;  // kRef: References() index; kCurve: Curves() index
		};
		[[nodiscard]] std::span<const Node> Code() const noexcept { return code_; }

	private:
		std::vector<Node>        code_;
		std::vector<std::string> refs_;
		std::vector<std::string> curves_;
		bool                     usesTime_ = false;
		bool                     usesX_ = false;
		bool                     usesMean_ = false;
		friend class ExpressionParser;
	};

	// A curve is a Program over x (and mean) that reads no rows.
	[[nodiscard]] std::expected<Program, std::string> ParseCurve(std::string_view a_text);

	// Applies a curve program to one scalar.
	[[nodiscard]] float ApplyCurve(const Program& a_curve, float a_x, float a_mean = 0.5f) noexcept;
}
