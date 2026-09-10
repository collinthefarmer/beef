#pragma once

#include "mesh/Islands.h"
#include "mesh/MaterialClusters.h"
#include "recipe/Recipe.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct RawTerm {
  [[nodiscard]] bool operator==(const RawTerm &) const = default;
};
struct ReferenceTerm {
  std::string name;
  [[nodiscard]] bool operator==(const ReferenceTerm &) const = default;
};
struct ThresholdTerm {
  MaterialChannel channel = MaterialChannel::kMetallic;
  float low = 0.0f;
  float high = 1.0f;
  float softness = 0.05f;
  std::uint8_t posterize = 0;
  bool invert = false;
  [[nodiscard]] bool operator==(const ThresholdTerm &) const = default;
};
struct PresetTerm {
  std::string preset;
  [[nodiscard]] bool operator==(const PresetTerm &) const = default;
};
struct PartitionTerm {
  std::uint32_t slot = 32;
  [[nodiscard]] bool operator==(const PartitionTerm &) const = default;
};
struct BoneTerm {
  std::vector<std::string> bones;
  [[nodiscard]] bool operator==(const BoneTerm &) const = default;
};
struct IslandTerm {
  IslandSource source = IslandSource::kComponent;
  std::uint16_t id = 0;
  [[nodiscard]] bool operator==(const IslandTerm &) const = default;
};
struct ClusterTerm {
  ClusterSettings settings;
  std::uint8_t id = 0;
  [[nodiscard]] bool operator==(const ClusterTerm &) const = default;
};

using TermKind = std::variant<RawTerm, ReferenceTerm, ThresholdTerm, PresetTerm,
                              PartitionTerm, BoneTerm, IslandTerm, ClusterTerm>;
inline constexpr std::size_t kTermKindCount = 8;
static_assert(std::variant_size_v<TermKind> == kTermKindCount);

[[nodiscard]] std::string_view TermKindName(const TermKind &a_kind) noexcept;

enum class TermOp {
  kSet,
  kAnd,
  kOr,
  kNot,
};
inline constexpr std::size_t kTermOpCount = 4;
[[nodiscard]] std::string_view TermOpName(TermOp a_op) noexcept;
[[nodiscard]] std::optional<TermOp>
ParseTermOp(std::string_view a_name) noexcept;

struct Term {
  TermOp op = TermOp::kSet;
  std::string text;
  std::string label;
  TermKind kind;
  [[nodiscard]] bool operator==(const Term &) const = default;
};

inline constexpr std::size_t kMaxTerms = 64;
inline constexpr std::string_view kExpressionLabel = "expression";

[[nodiscard]] std::string
BuildMask(std::span<const Term> a_terms,
          std::optional<std::size_t> a_solo = std::nullopt,
          const std::set<std::size_t> &a_muted = {});

struct MaskStack {
  std::vector<Term> terms;
  std::optional<std::size_t> selected;
  std::optional<std::size_t> solo;
  std::set<std::size_t> muted;
  std::string editing;
  bool dirty = false;
};
}
