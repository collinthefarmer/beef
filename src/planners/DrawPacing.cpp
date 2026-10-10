// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/DrawPacing.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
bool MayDrawStack(DrawPace a_pace) noexcept {
  return MayDrawStack(a_pace, kUnpublishedDrawBudgetUs);
}

bool MayDrawStack(DrawPace a_pace, std::uint64_t a_budgetUs) noexcept {
  return a_pace.stacks == 0 || a_pace.spentUs < a_budgetUs;
}

DrawPace Drew(DrawPace a_pace, std::uint64_t a_us) noexcept {
  a_pace.spentUs += a_us;
  ++a_pace.stacks;
  return a_pace;
}

std::vector<std::uint32_t>
UnpublishedDrawOrder(std::span<const UnpublishedDrawCandidate> a_candidates) {
  std::vector<UnpublishedDrawCandidate> sorted(a_candidates.begin(),
                                               a_candidates.end());
  std::ranges::stable_sort(sorted, [](const UnpublishedDrawCandidate &a_left,
                                      const UnpublishedDrawCandidate &a_right) {
    if (a_left.role != a_right.role) {
      return a_left.role == ActorRole::kPlayer;
    }
    return LargerOnScreen(a_left.screenFraction, a_right.screenFraction);
  });
  std::vector<std::uint32_t> order;
  order.reserve(sorted.size());
  for (const UnpublishedDrawCandidate &candidate : sorted) {
    order.push_back(candidate.actor);
  }
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
