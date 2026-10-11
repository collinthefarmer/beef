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
#include <variant>
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
  std::uint64_t pluginBytes = 0;
  std::uint64_t capBytes = 0;
  std::optional<std::uint64_t> heldBytes;
};

enum class AllowanceLimit : std::uint8_t { kCap, kFreeVideoMemory };

struct LimitedAllowance {
  std::uint64_t bytes = 0;
  AllowanceLimit limit = AllowanceLimit::kFreeVideoMemory;
  [[nodiscard]] bool operator==(const LimitedAllowance &) const = default;
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
  std::uint64_t withIntermediates = 0;
  std::uint64_t whenReleased = 0;
  [[nodiscard]] bool operator==(const LevelCost &) const = default;
};

using LevelBytes = PerLevel<std::optional<std::uint64_t>>;

struct InstallBounds {
  ResolutionLevel finestLevel = ResolutionLevel::kFull;
  std::optional<std::uint64_t> headroom;
  [[nodiscard]] bool operator==(const InstallBounds &) const = default;
};

enum class IntermediateHolding : std::uint8_t { kHeld, kReleased };

struct StepDownOption {
  ResolutionLevel to = ResolutionLevel::kEighth;
  std::uint64_t freed = 0;
  [[nodiscard]] bool operator==(const StepDownOption &) const = default;
};

struct BudgetCosts {
  std::uint64_t releasable = 0;
  std::optional<StepDownOption> stepDown;
  std::uint64_t planned = 0;
  std::uint64_t surplus = 0;
};

struct BudgetActor {
  std::uint32_t actor = 0;
  ActorVisibility visibility;
  std::optional<float> distance;
  ResolutionLevel level = ResolutionLevel::kFull;
  bool resizing = false;
  BudgetCosts costs;
};

struct PreemptionRequest {
  ActorVisibility visibility;
  std::uint64_t shortfall = 0;
};

struct StepDownReport {
  std::uint32_t actor = 0;
  std::string name;
  ResolutionLevel from = ResolutionLevel::kFull;
  StepDownOption option;
  std::optional<std::uint32_t> preemptedBy;
};

struct BudgetRelease {
  std::uint32_t actor = 0;
  std::uint64_t bytes = 0;
  [[nodiscard]] bool operator==(const BudgetRelease &) const = default;
};

struct Enough {
  [[nodiscard]] bool operator==(const Enough &) const = default;
};

struct AwaitResize {
  [[nodiscard]] bool operator==(const AwaitResize &) const = default;
};

struct StepDown {
  std::uint32_t actor = 0;
  StepDownOption option;
  [[nodiscard]] bool operator==(const StepDown &) const = default;
};

struct Exhausted {
  [[nodiscard]] bool operator==(const Exhausted &) const = default;
};

using PressureStep = std::variant<Enough, AwaitResize, StepDown, Exhausted>;

struct BudgetResponse {
  std::vector<BudgetRelease> release;
  PressureStep next;
};

[[nodiscard]] std::uint64_t Sum(std::uint64_t a_lhs,
                                std::uint64_t a_rhs) noexcept;
[[nodiscard]] std::uint64_t Less(std::uint64_t a_lhs,
                                 std::uint64_t a_rhs) noexcept;
[[nodiscard]] std::uint64_t MarginOf(std::uint64_t a_budget) noexcept;
[[nodiscard]] std::uint64_t BandOf(std::uint64_t a_allowance) noexcept;
[[nodiscard]] std::uint64_t
FreeVideoMemory(const VideoMemory &a_memory) noexcept;
[[nodiscard]] std::optional<LimitedAllowance>
LimitedAllowanceOf(const BudgetSample &a_sample) noexcept;
[[nodiscard]] std::optional<std::uint64_t>
AllowanceOf(const BudgetSample &a_sample) noexcept;
[[nodiscard]] bool IsOver(Pressure a_pressure) noexcept;
[[nodiscard]] BudgetState NextBudget(const BudgetState &a_current,
                                     std::optional<std::uint64_t> a_allowance,
                                     std::uint64_t a_planned) noexcept;
[[nodiscard]] std::uint64_t PressureGoal(std::uint64_t a_allowance) noexcept;
[[nodiscard]] Admission AdmitGrowth(const BudgetState &a_state,
                                    std::uint64_t a_added) noexcept;
[[nodiscard]] std::uint64_t ShortfallOf(const BudgetState &a_state,
                                        std::uint64_t a_added) noexcept;
[[nodiscard]] BudgetState Charged(BudgetState a_state,
                                  std::uint64_t a_added) noexcept;
[[nodiscard]] BudgetState Released(BudgetState a_state,
                                   std::uint64_t a_freed) noexcept;
[[nodiscard]] std::optional<std::uint64_t>
HeadroomOf(const BudgetState &a_state, std::uint64_t a_replaced) noexcept;
[[nodiscard]] ResolutionLevel
FittingLevel(ResolutionLevel a_finest, const LevelBytes &a_bytes,
             std::optional<std::uint64_t> a_headroom) noexcept;
[[nodiscard]] std::uint64_t
PlannedBytes(const LevelCost &a_cost, IntermediateHolding a_holding) noexcept;
[[nodiscard]] std::uint64_t IntermediateBytes(const LevelCost &a_cost) noexcept;
[[nodiscard]] std::optional<StepDownOption>
StepDownOf(ResolutionLevel a_level, const LevelCost &a_current,
           const std::optional<LevelCost> &a_coarser,
           IntermediateHolding a_holding) noexcept;
[[nodiscard]] std::optional<std::size_t>
PreemptionCandidate(const PreemptionRequest &a_request,
                    std::span<const BudgetActor> a_actors) noexcept;
[[nodiscard]] BudgetResponse
RespondToPressure(std::uint64_t a_planned, std::uint64_t a_goal,
                  std::span<const BudgetActor> a_actors, bool a_resizeBusy);
[[nodiscard]] std::string_view AdmissionName(Admission a_admission) noexcept;
[[nodiscard]] std::string PressureStartLine(const BudgetSample &a_sample,
                                            const BudgetState &a_state);
[[nodiscard]] std::string PressureEndLine(const BudgetState &a_state);
[[nodiscard]] std::string StepDownLine(const StepDownReport &a_report);
[[nodiscard]] std::string
ExhaustedLine(const BudgetState &a_state,
              std::span<const ActorHolding> a_largest);
}
