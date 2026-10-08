// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/DrawPacing.h"

#include <algorithm>
#include <numeric>

namespace BetterEnchantmentEffects {
bool MayDrawStack(DrawPace a_pace) noexcept {
  return a_pace.stacks == 0 || a_pace.spentUs < kHiddenDrawBudgetUs;
}

DrawPace Drew(DrawPace a_pace, std::uint64_t a_us) noexcept {
  a_pace.spentUs += a_us;
  ++a_pace.stacks;
  return a_pace;
}

std::vector<std::size_t>
HiddenDrawOrder(std::span<const HiddenDrawCandidate> a_candidates) {
  std::vector<std::size_t> order(a_candidates.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::ranges::stable_sort(order, [&](std::size_t a_left, std::size_t a_right) {
    const HiddenDrawCandidate &left = a_candidates[a_left];
    const HiddenDrawCandidate &right = a_candidates[a_right];
    if (left.role != right.role) {
      return left.role == ActorRole::kPlayer;
    }
    return left.screenFraction > right.screenFraction;
  });
  return order;
}

std::optional<FirstDrawEnd>
FirstDrawDone(const FirstDrawProgress &a_progress) noexcept {
  if (a_progress.drawn >= a_progress.stacks) {
    return FirstDrawEnd::kDrawn;
  }
  if (a_progress.drawTicks >= kFirstDrawAttachAfterTicks) {
    return FirstDrawEnd::kTickLimit;
  }
  return std::nullopt;
}

std::string_view FirstDrawEndName(FirstDrawEnd a_end) noexcept {
  switch (a_end) {
  case FirstDrawEnd::kDrawn:
    return "drawn";
  case FirstDrawEnd::kTickLimit:
    return "tick_limit";
  }
  return "unknown";
}
}
