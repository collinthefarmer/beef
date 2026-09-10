#include "studio/Mask.h"

#include "Core.h"
#include "recipe/Expression.h"

#include <format>

namespace BetterEnchantmentEffects::Studio {
std::string_view TermKindName(const TermKind &a_kind) noexcept {
  return Match(
      a_kind, [](const RawTerm &) { return "raw"; },
      [](const ReferenceTerm &) { return "reference"; },
      [](const ThresholdTerm &) { return "threshold"; },
      [](const PresetTerm &) { return "preset"; },
      [](const PartitionTerm &) { return "partition"; },
      [](const BoneTerm &) { return "bones"; },
      [](const IslandTerm &) { return "component"; },
      [](const ClusterTerm &) { return "cluster"; });
}

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
    if (next.size() > kMaxExpressionLength) {
      break;
    }
    built = std::move(next);
  }
  if (WrapsWhole(built)) {
    built = built.substr(1, built.size() - 2);
  }
  return built;
}
}
