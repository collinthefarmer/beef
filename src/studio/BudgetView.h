// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/BudgetRadius.h"
#include "planners/RenderBudget.h"
#include "planners/ResolutionLevel.h"
#include "studio/Widgets.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct MemoryReading {
  VideoMemory memory;
  std::optional<LimitedAllowance> allowance;
  [[nodiscard]] bool operator==(const MemoryReading &) const = default;
};

struct BudgetView {
  std::optional<MemoryReading> reading;
  std::uint64_t planned = 0;
  std::uint64_t measured = 0;
  Pressure pressure = Pressure::kNone;
  std::optional<float> radius;
  PerLevel<std::size_t> actors;
  [[nodiscard]] bool operator==(const BudgetView &) const = default;
};

inline constexpr Preset kBudgetCapPresets[]{{"Automatic", 0},
                                            {"512 MiB", 512},
                                            {"1024 MiB", 1024},
                                            {"2048 MiB", 2048},
                                            {"4096 MiB", 4096}};
inline constexpr PresetSliderSpec kBudgetCapControl{.presets =
                                                        kBudgetCapPresets,
                                                    .min = 128,
                                                    .max = 8192,
                                                    .step = 32,
                                                    .unit = "MiB"};

[[nodiscard]] BudgetView
BudgetViewOf(const BudgetSample &a_sample, const BudgetState &a_state,
             const std::optional<BudgetRadius> &a_radius,
             const PerLevel<std::size_t> &a_actors);
[[nodiscard]] std::string_view PressureText(Pressure a_pressure) noexcept;
[[nodiscard]] std::vector<ReadoutLine> BudgetReadout(const BudgetView &a_view);
}
