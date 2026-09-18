#include "recipe/Recipe.h"

#include <algorithm>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects {
Diagnostic MakeDiagnostic(Severity a_severity, std::string a_where,
                          std::string a_message) {
  return Diagnostic{a_severity, std::move(a_where), std::move(a_message)};
}

const Signal *Recipe::FindSignal(std::string_view a_name) const noexcept {
  return FindByName(signals, a_name);
}

const Curve *Recipe::FindCurve(std::string_view a_name) const noexcept {
  return FindByName(curves, a_name);
}

const Source *Recipe::FindSource(std::string_view a_name) const noexcept {
  return FindByName(sources, a_name);
}

const Mask *Recipe::FindMask(std::string_view a_name) const noexcept {
  return FindByName(masks, a_name);
}

std::string SignalWhere(std::string_view a_signal) {
  return std::format("signal {}", a_signal);
}
std::string CurveWhere(std::string_view a_curve) {
  return std::format("curve {}", a_curve);
}
std::string SourceWhere(std::string_view a_source) {
  return std::format("source {}", a_source);
}
std::string MaskWhere(std::string_view a_mask) {
  return std::format("mask {}", a_mask);
}
std::string OutputWhere(std::size_t a_output) {
  return std::format("output {}", a_output);
}
std::string LayerWhere(std::size_t a_output, std::size_t a_layer) {
  return std::format("output {} layer {}", a_output, a_layer);
}
std::string VariantWhere(std::string_view a_variant) {
  return std::format("variant {}", a_variant);
}
std::string KeyWhere(const RecipeKey &a_key) {
  return std::format("key {}", a_key.ToString());
}

namespace {
constexpr std::string_view kRowWherePrefixes[]{
    "signal ", "curve ", "source ", "mask ", "output ", "variant "};
}

bool RowLevel(const Diagnostic &a_diagnostic) noexcept {
  return std::ranges::any_of(kRowWherePrefixes, [&](std::string_view a_prefix) {
    return a_diagnostic.where.starts_with(a_prefix);
  });
}

bool HasErrors(std::span<const Diagnostic> a_diagnostics) noexcept {
  return std::ranges::any_of(a_diagnostics, [](const Diagnostic &d) {
    return d.severity == Severity::kError;
  });
}

bool HasRecipeErrors(std::span<const Diagnostic> a_diagnostics) noexcept {
  return std::ranges::any_of(a_diagnostics, [](const Diagnostic &d) {
    return d.severity == Severity::kError && !RowLevel(d);
  });
}

std::string ProblemText(const std::optional<Diagnostic> &a_problem) {
  return a_problem ? a_problem->message : std::string{};
}

bool LoadResult::HasErrors() const noexcept {
  return !recipe || BetterEnchantmentEffects::HasErrors(diagnostics);
}

bool LoadResult::HasRecipeErrors() const noexcept {
  return !recipe || BetterEnchantmentEffects::HasRecipeErrors(diagnostics);
}
}
