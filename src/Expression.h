#pragma once

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
	inline constexpr std::size_t kMaxExpressionLength = 4096;
	inline constexpr std::size_t kMaxExpressionDepth = 32;
	inline constexpr std::size_t kMaxExpressionOps = 256;

	using RefTyper = std::function<std::optional<ValueType>(std::string_view a_name)>;

	class Program
	{
	public:
		[[nodiscard]] static std::expected<Program, std::string> Parse(std::string_view a_text);

		[[nodiscard]] std::span<const std::string> References() const noexcept { return refs_; }
		[[nodiscard]] std::span<const std::string> Curves() const noexcept { return curves_; }
		[[nodiscard]] bool                         UsesTime() const noexcept { return usesTime_; }
		[[nodiscard]] bool                         UsesX() const noexcept { return usesX_; }
		[[nodiscard]] bool                         UsesMean() const noexcept { return usesMean_; }
		[[nodiscard]] std::size_t                  OpCount() const noexcept { return code_.size(); }
		[[nodiscard]] bool Constant() const noexcept { return refs_.empty() && curves_.empty() && !usesTime_ && !usesX_ && !usesMean_; }

		[[nodiscard]] std::expected<ValueType, std::string> Check(const RefTyper& a_types, ValueType a_xType = ValueType::kScalar) const;

		struct Inputs
		{
			std::span<const Value>          refs;
			std::span<const Program* const> curves;
			float                           time = 0.0f;
			Value                           x = 0.0f;
			float                           mean = 0.5f;
		};

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
			std::uint32_t index = 0;
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

	[[nodiscard]] std::expected<Program, std::string> ParseCurve(std::string_view a_text);

	[[nodiscard]] float ApplyCurve(const Program& a_curve, float a_x, float a_mean = 0.5f) noexcept;
}
