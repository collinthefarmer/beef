#include "studio/FieldCheck.h"

#include "recipe/Expression.h"
#include "recipe/Recipe.h"
#include "studio/Fields.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] bool Listed(std::span<const std::string> a_names,
                          std::string_view a_name) {
  return std::ranges::contains(a_names, a_name);
}

[[nodiscard]] std::optional<ValueType> SignalType(const Names &a_names,
                                                  std::string_view a_name) {
  for (const auto &[name, type] : a_names.signals) {
    if (name == a_name) {
      return type;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<ValueType> TexelType(const Names &a_names,
                                                 std::string_view a_name) {
  for (const auto &[name, type] : a_names.sources) {
    if (name == a_name) {
      return type;
    }
  }
  if (Listed(a_names.masks, a_name)) {
    return ValueType::kScalar;
  }
  return SignalType(a_names, a_name);
}

[[nodiscard]] std::optional<std::string>
CheckWholeReference(const FormField &a_field, std::string_view a_name) {
  if (Listed(a_field.names, a_name)) {
    return std::nullopt;
  }
  return std::format("'@{}' is not one of the rows this field takes", a_name);
}

[[nodiscard]] std::optional<std::string> CheckComponent(const Names &a_names,
                                                        const Param &a_part) {
  const Ref *ref = Get<Ref>(a_part);
  if (ref == nullptr) {
    return std::nullopt;
  }
  const std::optional<ValueType> type = SignalType(a_names, ref->name);
  if (!type) {
    return std::format("'@{}' is not a signal", ref->name);
  }
  if (*type != ValueType::kScalar) {
    return std::format("'@{}' is not a scalar signal", ref->name);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> CheckScalar(const FormField &a_field,
                                                     std::string_view a_text) {
  const std::optional<Param> param = ParseParam(a_text);
  if (!param) {
    return "a number, or @signal";
  }
  if (const Ref *ref = Get<Ref>(*param)) {
    return CheckWholeReference(a_field, ref->name);
  }
  const float *number = Get<float>(*param);
  if (a_field.integral && number && std::trunc(*number) != *number) {
    return "a whole number";
  }
  if (a_field.range && number &&
      (*number < a_field.range->first || *number > a_field.range->second)) {
    return std::format("{} to {}", ParamText(a_field.range->first),
                       ParamText(a_field.range->second));
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string>
CheckLayerSource(const FormField &a_field, std::string_view a_text) {
  if (a_text.starts_with('@')) {
    return CheckWholeReference(a_field, a_text.substr(1));
  }
  return LiteralColor(a_text)
             ? std::nullopt
             : std::optional<std::string>{"@source, @mask, or r, g, b"};
}

[[nodiscard]] std::optional<std::string>
CheckExpression(std::string_view a_text, const Names &a_names, bool a_texel,
                bool a_curve) {
  const std::expected<Program, std::string> program = Program::Parse(a_text);
  if (!program) {
    return program.error();
  }
  for (const std::string &name : program->References()) {
    const bool known = a_texel ? TexelType(a_names, name).has_value()
                               : SignalType(a_names, name).has_value();
    if (!known) {
      return a_texel
                 ? std::format("'@{}' is not a source, mask or signal", name)
                 : std::format("'@{}' is not a signal", name);
    }
  }
  for (const std::string &name : program->Curves()) {
    if (!Listed(a_names.curves, name)) {
      return std::format("'@{}(' is not a declared curve", name);
    }
  }
  if (program->UsesX() && !a_curve) {
    return "'x' is only defined inside a curve";
  }
  const std::expected<ValueType, std::string> type =
      program->Check([&](std::string_view a_name) {
        return a_texel ? TexelType(a_names, a_name)
                       : SignalType(a_names, a_name);
      });
  if (!type) {
    return type.error();
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> CheckVec3(const FormField &a_field,
                                                   std::string_view a_text,
                                                   const Names &a_names) {
  const std::optional<Vec3Param> param = ParseVec3Param(a_text);
  if (!param) {
    return "three numbers, one number, or @signal";
  }
  if (const Ref *ref = Get<Ref>(*param)) {
    return CheckWholeReference(a_field, ref->name);
  }
  for (const Param &part : *Get<std::array<Param, 3>>(*param)) {
    if (std::optional<std::string> problem = CheckComponent(a_names, part)) {
      return problem;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> CheckVec2(const FormField &a_field,
                                                   std::string_view a_text,
                                                   const Names &a_names) {
  const std::optional<Vec2Param> param = ParseVec2Param(a_text);
  if (!param) {
    return "two numbers, one number, or @signal";
  }
  if (const Ref *ref = Get<Ref>(*param)) {
    return CheckWholeReference(a_field, ref->name);
  }
  for (const Param &part : *Get<std::array<Param, 2>>(*param)) {
    if (std::optional<std::string> problem = CheckComponent(a_names, part)) {
      return problem;
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> CheckCurve(const FormField &a_field,
                                                    std::string_view a_text,
                                                    const Names &a_names) {
  const CurveRef ref{std::string{a_text}};
  if (const std::optional<std::string> name = ref.Named()) {
    return Listed(a_field.names, *name)
               ? std::nullopt
               : std::optional<std::string>{
                     std::format("'@{}' is not a declared curve", *name)};
  }
  return CheckExpression(a_text, a_names, false, true);
}

[[nodiscard]] std::optional<std::string>
CheckReference(const FormField &a_field, std::string_view a_text) {
  return a_text.starts_with('@') && a_text.size() > 1
             ? CheckWholeReference(a_field, a_text.substr(1))
             : std::optional<std::string>{"@name of a row"};
}

[[nodiscard]] std::optional<std::string>
CheckChannels(std::string_view a_text) {
  return ChannelSet::Parse(a_text)
             ? std::nullopt
             : std::optional<std::string>{"any of r g b a"};
}

[[nodiscard]] std::optional<std::string> CheckChoice(const FormField &a_field,
                                                     std::string_view a_text) {
  return Listed(a_field.names, a_text)
             ? std::nullopt
             : std::optional<std::string>{"one of the listed values"};
}

[[nodiscard]] std::optional<std::string> CheckName(const FormField &a_field,
                                                   std::string_view a_text) {
  if (!IsName(a_text)) {
    return "letters, digits and underscores, not starting with a digit";
  }
  if (a_text != a_field.text && Listed(a_field.names, a_text)) {
    return std::format("another row is named '{}'", a_text);
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Diagnostic>
DiagnosticOf(std::string_view a_where, std::optional<std::string> a_message) {
  if (!a_message) {
    return std::nullopt;
  }
  return MakeDiagnostic(Severity::kError, std::string{a_where},
                        std::move(*a_message));
}
}

std::optional<Diagnostic> CheckSignalValue(std::string_view a_text,
                                           const Names &a_names) {
  if (a_text.empty()) {
    return DiagnosticOf("signal value", "cannot be empty");
  }
  if (const std::optional<Param> param = ParseParam(a_text);
      param && Get<float>(*param)) {
    return std::nullopt;
  }
  if (const std::optional<Vec3Param> colour = ParseVec3Param(a_text)) {
    if (const std::array<Param, 3> *parts = Get<std::array<Param, 3>>(*colour);
        parts && std::ranges::all_of(*parts, [](const Param &a_part) {
          return Get<float>(a_part) != nullptr;
        })) {
      return std::nullopt;
    }
  }
  return DiagnosticOf("signal value",
                      CheckExpression(a_text, a_names, false, false));
}

std::optional<Diagnostic> CheckCurveText(std::string_view a_text,
                                         const Names &a_names) {
  if (a_text.empty()) {
    return DiagnosticOf("curve", "cannot be empty");
  }
  return DiagnosticOf("curve", CheckExpression(a_text, a_names, false, true));
}

std::optional<Diagnostic> CheckMaskText(std::string_view a_text,
                                        const Names &a_names) {
  if (a_text.empty()) {
    return DiagnosticOf("mask", "cannot be empty");
  }
  return DiagnosticOf("mask", CheckExpression(a_text, a_names, true, false));
}

std::optional<Diagnostic> CheckField(const FormField &a_field,
                                     std::string_view a_text,
                                     const Names &a_names) {
  if (a_text.empty()) {
    return a_field.allowEmpty
               ? std::nullopt
               : DiagnosticOf(a_field.name, std::string{"cannot be empty"});
  }
  const FieldKindSpec *row = RowOf(kFieldKinds, a_field.kind);
  if (row == nullptr) {
    return std::nullopt;
  }
  switch (row->check) {
  case FieldCheckKind::kScalar:
    return DiagnosticOf(a_field.name, CheckScalar(a_field, a_text));
  case FieldCheckKind::kColorOrVector:
    return DiagnosticOf(a_field.name, CheckVec3(a_field, a_text, a_names));
  case FieldCheckKind::kVec2:
    return DiagnosticOf(a_field.name, CheckVec2(a_field, a_text, a_names));
  case FieldCheckKind::kReference:
    return DiagnosticOf(a_field.name, CheckReference(a_field, a_text));
  case FieldCheckKind::kExpression:
    return DiagnosticOf(a_field.name,
                        CheckExpression(a_text, a_names, false, false));
  case FieldCheckKind::kMask:
    return DiagnosticOf(a_field.name,
                        CheckExpression(a_text, a_names, true, false));
  case FieldCheckKind::kCurve:
    return DiagnosticOf(a_field.name, CheckCurve(a_field, a_text, a_names));
  case FieldCheckKind::kChannels:
    return DiagnosticOf(a_field.name, CheckChannels(a_text));
  case FieldCheckKind::kChoice:
    return DiagnosticOf(a_field.name, CheckChoice(a_field, a_text));
  case FieldCheckKind::kName:
    return DiagnosticOf(a_field.name, CheckName(a_field, a_text));
  case FieldCheckKind::kSignalValue:
    return CheckSignalValue(a_text, a_names);
  case FieldCheckKind::kLayerSource:
    return DiagnosticOf(a_field.name, CheckLayerSource(a_field, a_text));
  case FieldCheckKind::kNone:
    return std::nullopt;
  }
  return std::nullopt;
}
}
