// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/InstallPacing.h"

#include "Core.h"

#include <algorithm>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
enum class InstallRank { kOverdue, kPlayer, kInView, kOther };

std::uint32_t WaitOf(const WaitingInstall &a_entry,
                     std::uint32_t a_nowMS) noexcept {
  return a_nowMS - a_entry.sinceMS;
}

InstallRank RankOf(const WaitingInstall &a_entry,
                   std::uint32_t a_nowMS) noexcept {
  if (WaitOf(a_entry, a_nowMS) >= kInstallWaitLimitMS) {
    return InstallRank::kOverdue;
  }
  if (a_entry.role == ActorRole::kPlayer) {
    return InstallRank::kPlayer;
  }
  return a_entry.inView ? InstallRank::kInView : InstallRank::kOther;
}

bool Precedes(const WaitingInstall &a_left, const WaitingInstall &a_right,
              std::uint32_t a_nowMS) noexcept {
  const InstallRank left = RankOf(a_left, a_nowMS);
  const InstallRank right = RankOf(a_right, a_nowMS);
  if (left != right) {
    return left < right;
  }
  if (left == InstallRank::kInView &&
      a_left.screenFraction != a_right.screenFraction) {
    return LargerOnScreen(a_left.screenFraction, a_right.screenFraction);
  }
  return WaitOf(a_left, a_nowMS) > WaitOf(a_right, a_nowMS);
}
}

bool CanInstallThisTick(FrameSyncs a_frame) noexcept {
  return a_frame.installs == 0 || a_frame.spentUs < kInstallBudgetUs;
}

FrameSyncs Synced(FrameSyncs a_frame, std::uint64_t a_us,
                  bool a_installed) noexcept {
  a_frame.spentUs += a_us;
  if (a_installed) {
    ++a_frame.installs;
  }
  return a_frame;
}

std::optional<std::size_t>
NextInstall(std::span<const WaitingInstall> a_waiting, FrameSyncs a_frame,
            std::uint32_t a_nowMS) noexcept {
  if (!CanInstallThisTick(a_frame) || a_waiting.empty()) {
    return std::nullopt;
  }
  const auto first = std::ranges::min_element(
      a_waiting,
      [a_nowMS](const WaitingInstall &a_left, const WaitingInstall &a_right) {
        return Precedes(a_left, a_right, a_nowMS);
      });
  return IndexOf(a_waiting, first);
}

std::vector<std::uint32_t>
InstallOrder(std::span<const WaitingInstall> a_waiting, std::uint32_t a_nowMS) {
  std::vector<WaitingInstall> sorted(a_waiting.begin(), a_waiting.end());
  std::ranges::stable_sort(sorted, [a_nowMS](const WaitingInstall &a_left,
                                             const WaitingInstall &a_right) {
    return Precedes(a_left, a_right, a_nowMS);
  });
  return Map(sorted,
             [](const WaitingInstall &a_entry) { return a_entry.actor; });
}

std::vector<WaitingInstall> Deferred(std::vector<WaitingInstall> a_waiting,
                                     std::uint32_t a_actor,
                                     std::uint32_t a_nowMS) {
  if (std::ranges::find(a_waiting, a_actor, &WaitingInstall::actor) ==
      a_waiting.end()) {
    a_waiting.push_back({.actor = a_actor, .sinceMS = a_nowMS});
  }
  return a_waiting;
}

std::vector<WaitingInstall> Removed(std::vector<WaitingInstall> a_waiting,
                                    std::uint32_t a_actor) {
  std::erase_if(a_waiting, [a_actor](const WaitingInstall &a_entry) {
    return a_entry.actor == a_actor;
  });
  return a_waiting;
}

std::optional<std::uint32_t> WaitedMS(std::span<const WaitingInstall> a_waiting,
                                      std::uint32_t a_actor,
                                      std::uint32_t a_nowMS) noexcept {
  const auto found =
      std::ranges::find(a_waiting, a_actor, &WaitingInstall::actor);
  if (found == a_waiting.end()) {
    return std::nullopt;
  }
  return WaitOf(*found, a_nowMS);
}
}
