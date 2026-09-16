#include "studio/Mask.h"

#include "Core.h"
#include "recipe/Expression.h"

#include <format>

namespace BetterEnchantmentEffects::Studio {
std::string_view TermOpName(TermOp a_op) noexcept {
  switch (a_op) {
  case TermOp::kSet:
    return "set";
  case TermOp::kAnd:
    return "and";
  case TermOp::kOr:
    return "or";
  case TermOp::kNot:
    return "not";
  }
  return "?";
}

std::optional<TermOp> ParseTermOp(std::string_view a_name) noexcept {
  for (const TermOp op :
       {TermOp::kSet, TermOp::kAnd, TermOp::kOr, TermOp::kNot}) {
    if (TermOpName(op) == a_name) {
      return op;
    }
  }
  return std::nullopt;
}

namespace {
[[nodiscard]] bool WrapsWhole(const std::string &a_text) {
  if (a_text.empty() || a_text.front() != '(' || a_text.back() != ')') {
    return false;
  }
  std::size_t depth = 0;
  for (std::size_t i = 0; i + 1 < a_text.size(); ++i) {
    depth += a_text[i] == '(' ? 1 : 0;
    depth -= a_text[i] == ')' ? 1 : 0;
    if (depth == 0) {
      return false;
    }
  }
  return true;
}
}

std::expected<std::string, Diagnostic>
CheckedBuildMask(std::span<const Term> a_terms,
                 std::optional<std::size_t> a_solo,
                 const std::set<std::size_t> &a_muted) {
  if (a_terms.size() > kMaxTerms) {
    return std::unexpected(MakeDiagnostic(
        Severity::kError, "paint", "a mask may contain at most 64 terms"));
  }
  std::string text = BuildMask(a_terms, a_solo, a_muted);
  if (text.empty()) {
    return text;
  }
  if (const auto program = Program::Parse(text); !program) {
    return std::unexpected(
        MakeDiagnostic(Severity::kError, "paint", program.error()));
  }
  return text;
}

std::string BuildMask(std::span<const Term> a_terms,
                      std::optional<std::size_t> a_solo,
                      const std::set<std::size_t> &a_muted) {
  std::string built;
  for (std::size_t i = 0; i < a_terms.size() && i < kMaxTerms; ++i) {
    const Term &term = a_terms[i];
    const bool shown = a_solo ? *a_solo == i : !a_muted.contains(i);
    if (!shown || term.text.empty()) {
      continue;
    }
    std::string next;
    if (built.empty()) {
      next = std::format("({})", term.text);
    } else {
      switch (term.op) {
      case TermOp::kSet:
      case TermOp::kAnd:
        next = std::format("{} * ({})", built, term.text);
        break;
      case TermOp::kOr:
        next = std::format("max({}, ({}))", built, term.text);
        break;
      case TermOp::kNot:
        next = std::format("{} * (1 - ({}))", built, term.text);
        break;
      }
    }
    built = std::move(next);
  }
  if (WrapsWhole(built)) {
    built = built.substr(1, built.size() - 2);
  }
  return built;
}
}
