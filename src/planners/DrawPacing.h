// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ActorRole.h"
#include "planners/ResolutionLevel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::uint64_t kUnpublishedDrawBudgetUs = 4000;
inline constexpr std::uint64_t kFirstDrawAttachAfterTicks = 60;

struct DrawPace {
  std::uint64_t spentUs = 0;
  std::size_t stacks = 0;
};

struct UnpublishedDrawCandidate {
  std::uint32_t actor = 0;
  ActorRole role = ActorRole::kOther;
  std::optional<float> screenFraction;
};

struct FirstDrawProgress {
  std::uint64_t drawTicks = 0;
  std::size_t stacks = 0;
  std::size_t drawn = 0;
};

enum class FirstDrawEnd { kDrawn, kTickLimit };

[[nodiscard]] bool MayDrawStack(DrawPace a_pace) noexcept;
[[nodiscard]] bool MayDrawStack(DrawPace a_pace,
                                std::uint64_t a_budgetUs) noexcept;
[[nodiscard]] DrawPace Drew(DrawPace a_pace, std::uint64_t a_us) noexcept;
[[nodiscard]] std::vector<std::uint32_t>
UnpublishedDrawOrder(std::span<const UnpublishedDrawCandidate> a_candidates);
[[nodiscard]] std::optional<FirstDrawEnd>
FirstDrawDone(const FirstDrawProgress &a_progress) noexcept;
[[nodiscard]] std::string_view FirstDrawEndName(FirstDrawEnd a_end) noexcept;
}
