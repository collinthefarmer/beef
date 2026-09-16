#include "studio/EditChecks.h"

#include "recipe/Expression.h"

#include <array>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
using Refusal = std::optional<Diagnostic>;

Diagnostic Refuse(std::string a_where, std::string a_message) {
  return MakeDiagnostic(Severity::kError, std::move(a_where),
                        std::move(a_message));
}
}

std::optional<Diagnostic> CheckText(const std::string &a_where,
                                    const std::string &a_text) {
  if (a_text.empty()) {
    return Refuse(a_where, "the expression is empty");
  }
  if (a_text.size() > kMaxExpressionLength) {
    return Refuse(a_where, std::format("longer than {} characters",
                                       kMaxExpressionLength));
  }
  if (const auto program = Program::Parse(a_text); !program) {
    return Refuse(a_where, program.error());
  }
  return std::nullopt;
}

std::optional<Diagnostic> CheckCurveText(const std::string &a_where,
                                         const CurveRef &a_curve) {
  if (a_curve.Named()) {
    return std::nullopt;
  }
  if (const auto program = ParseCurve(a_curve.text); !program) {
    return Refuse(a_where, program.error());
  }
  return std::nullopt;
}

std::optional<Diagnostic> CheckScalarRef(const CheckContext &a_ctx,
                                         std::string_view a_field,
                                         const Param &a_param) {
  const auto *ref = Get<Ref>(a_param);
  if (!ref) {
    return std::nullopt;
  }
  const auto type = SignalTypeOf(a_ctx.rows, ref->name);
  if (!type) {
    return Refuse(
        std::string{a_ctx.where},
        std::format("'{}' reads unknown signal '@{}'", a_field, ref->name));
  }
  if (*type != ValueType::kScalar) {
    return Refuse(std::string{a_ctx.where},
                  std::format("'{}' must be a scalar; '@{}' is a {}", a_field,
                              ref->name, Name(*type)));
  }
  return std::nullopt;
}

namespace {
Refusal CheckVectorRefSignal(const CheckContext &a_ctx,
                             std::string_view a_field, const Ref &a_ref) {
  const auto type = SignalTypeOf(a_ctx.rows, a_ref.name);
  if (!type) {
    return Refuse(
        std::string{a_ctx.where},
        std::format("'{}' reads unknown signal '@{}'", a_field, a_ref.name));
  }
  if (*type != ValueType::kVec3) {
    return Refuse(std::string{a_ctx.where},
                  std::format("'{}' must be a vec3; '@{}' is a {}", a_field,
                              a_ref.name, Name(*type)));
  }
  return std::nullopt;
}

Refusal CheckVectorRefParts(const CheckContext &a_ctx, std::string_view a_field,
                            const std::array<Param, 3> &a_parts, bool a_color) {
  for (const auto &part : a_parts) {
    if (auto problem = CheckScalarRef(a_ctx, a_field, part)) {
      return problem;
    }
    const auto *number = Get<float>(part);
    if (a_color && number && (*number < 0.0f || *number > 1.0f)) {
      return Refuse(std::string{a_ctx.where},
                    std::format("'{}' components are 0..1", a_field));
    }
  }
  return std::nullopt;
}
}

std::optional<Diagnostic> CheckVectorRef(const CheckContext &a_ctx,
                                         std::string_view a_field,
                                         const Vec3Param &a_param,
                                         bool a_color) {
  return Match(
      a_param,
      [&](const Ref &a_ref) -> Refusal {
        return CheckVectorRefSignal(a_ctx, a_field, a_ref);
      },
      [&](const std::array<Param, 3> &a_parts) -> Refusal {
        return CheckVectorRefParts(a_ctx, a_field, a_parts, a_color);
      });
}
}
