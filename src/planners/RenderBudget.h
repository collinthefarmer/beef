// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/RenderMemory.h"
#include "planners/ResolutionLevel.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::uint64_t kMebibyte = std::uint64_t{1} << 20;
inline constexpr std::uint64_t kMinBudgetMargin = 512 * kMebibyte;
inline constexpr std::uint64_t kBudgetMarginDivisor = 20;
inline constexpr std::uint64_t kMinPressureBand = 256 * kMebibyte;
inline constexpr std::uint64_t kPressureBandDivisor = 10;

struct VideoMemory {
  std::uint64_t budget = 0;
  std::uint64_t usage = 0;
  [[nodiscard]] bool operator==(const VideoMemory &) const = default;
};

struct BudgetSample {
  std::optional<VideoMemory> memory;
  std::uint64_t liveBytes = 0;
  std::uint64_t capBytes = 0;
};

enum class Pressure : std::uint8_t { kNone, kOver, kOverWarned };

struct BudgetState {
  std::optional<std::uint64_t> allowance;
  std::uint64_t planned = 0;
  Pressure pressure = Pressure::kNone;
  [[nodiscard]] bool operator==(const BudgetState &) const = default;
};

enum class Admission : std::uint8_t {
  kAdmitted,
  kUnderPressure,
  kOverAllowance
};

struct LevelCost {
  std::uint64_t held = 0;
  std::uint64_t stacks = 0;
  [[nodiscard]] bool operator==(const LevelCost &) const = default;
};

using LevelBytes =
    std::array<std::optional<std::uint64_t>, kResolutionLevelCount>;

struct InstallBound {
  ResolutionLevel finest = ResolutionLevel::kFull;
  std::optional<std::uint64_t> headroom;
  [[nodiscard]] bool operator==(const InstallBound &) const = default;
};

enum class IntermediateHolding : std::uint8_t { kHeld, kReleased };

struct StepDownOption {
  ResolutionLevel to = ResolutionLevel::kEighth;
  std::uint64_t freed = 0;
  [[nodiscard]] bool operator==(const StepDownOption &) const = default;
};

struct BudgetActor {
  std::uint32_t actor = 0;
  ActorRole role = ActorRole::kOther;
  bool inView = true;
  bool resizing = false;
  ResolutionLevel level = ResolutionLevel::kFull;
  float screenFraction = 0.0f;
  std::uint64_t releasable = 0;
  std::optional<StepDownOption> stepDown;
  std::uint64_t held = 0;
  std::optional<float> distance;
};

struct StepDown {
  std::size_t actor = 0;
  StepDownOption option;
  [[nodiscard]] bool operator==(const StepDown &) const = default;
};

struct BudgetResponse {
  std::vector<std::size_t> release;
  std::optional<StepDown> stepDown;
  bool exhausted = false;
};

[[nodiscard]] std::uint64_t MarginOf(std::uint64_t a_budget) noexcept;
[[nodiscard]] std::uint64_t BandOf(std::uint64_t a_allowance) noexcept;
[[nodiscard]] std::optional<std::uint64_t>
AllowanceOf(const BudgetSample &a_sample) noexcept;
[[nodiscard]] bool IsOver(Pressure a_pressure) noexcept;
[[nodiscard]] BudgetState NextBudget(const BudgetState &a_current,
                                     std::optional<std::uint64_t> a_allowance,
                                     std::uint64_t a_planned) noexcept;
[[nodiscard]] std::uint64_t PressureGoal(std::uint64_t a_allowance) noexcept;
[[nodiscard]] Admission AdmitGrowth(const BudgetState &a_state,
                                    std::uint64_t a_added) noexcept;
[[nodiscard]] BudgetState Admitted(BudgetState a_state,
                                   std::uint64_t a_added) noexcept;
[[nodiscard]] BudgetState Released(BudgetState a_state,
                                   std::uint64_t a_freed) noexcept;
[[nodiscard]] std::optional<std::uint64_t>
HeadroomOf(const BudgetState &a_state, std::uint64_t a_replaced) noexcept;
[[nodiscard]] ResolutionLevel
FittingLevel(ResolutionLevel a_finest, const LevelBytes &a_bytes,
             std::optional<std::uint64_t> a_headroom) noexcept;
[[nodiscard]] std::uint64_t HeldBytes(const LevelCost &a_cost,
                                      IntermediateHolding a_holding) noexcept;
[[nodiscard]] std::optional<StepDownOption>
StepDownOf(ResolutionLevel a_level, const LevelCost &a_current,
           const std::optional<LevelCost> &a_coarser,
           IntermediateHolding a_holding) noexcept;
[[nodiscard]] BudgetResponse
RespondToPressure(std::uint64_t a_planned, std::uint64_t a_goal,
                  std::span<const BudgetActor> a_actors, bool a_resizeBusy);
[[nodiscard]] std::string_view AdmissionName(Admission a_admission) noexcept;
[[nodiscard]] std::string PressureStartLine(const BudgetSample &a_sample,
                                            const BudgetState &a_state);
[[nodiscard]] std::string PressureEndLine(const BudgetState &a_state);
[[nodiscard]] std::string StepDownLine(std::uint32_t a_actor,
                                       std::string_view a_name,
                                       ResolutionLevel a_from,
                                       const StepDownOption &a_option);
[[nodiscard]] std::string
ExhaustedLine(const BudgetState &a_state,
              std::span<const ActorHolding> a_largest);
}
