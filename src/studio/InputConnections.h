#pragma once

#include "studio/Forms.h"
#include "studio/Names.h"

#include <array>
#include <expected>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
enum class InputConnectionKind {
  kMeasure,
  kFraction,
  kExhausted,
  kHitResponse,
  kCount
};
inline constexpr std::array<std::string_view, 4> kInputConnectionNames{
    "Selected measure", "Current / max", "Exhausted", "Hit response"};
static_assert(kInputConnectionNames.size() ==
              static_cast<std::size_t>(InputConnectionKind::kCount));

struct InputConnectionSpec {
  InputConnectionKind kind = InputConnectionKind::kMeasure;
  std::string actorValue;
  Measure measure = Measure::kCurrent;
};

[[nodiscard]] bool CanConnectInput(const FormField &a_field);
[[nodiscard]] std::expected<EditBatch, std::string>
CreateInput(const Names &a_names, const InputConnectionSpec &a_spec);
[[nodiscard]] std::expected<EditBatch, std::string>
ConnectInput(const FormField &a_field, const Names &a_names,
             const InputConnectionSpec &a_spec);
}
