// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ActorRole.h"
#include "planners/ResolutionLevel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::uint64_t kInstallBudgetUs = 8000;
inline constexpr std::uint32_t kInstallWaitLimitMS = 1000;

struct WaitingInstall {
  std::uint32_t actor = 0;
  ActorRole role = ActorRole::kOther;
  bool inView = false;
  std::optional<float> screenFraction;
  std::uint32_t sinceMS = 0;
};

struct FrameSyncs {
  std::uint64_t spentUs = 0;
  std::size_t installs = 0;
};

[[nodiscard]] bool InstallMayStart(FrameSyncs a_frame) noexcept;
[[nodiscard]] FrameSyncs Synced(FrameSyncs a_frame, std::uint64_t a_us,
                                bool a_installed) noexcept;
[[nodiscard]] std::optional<std::size_t>
NextInstall(std::span<const WaitingInstall> a_waiting, FrameSyncs a_frame,
            std::uint32_t a_nowMS) noexcept;
[[nodiscard]] std::vector<WaitingInstall>
Deferred(std::vector<WaitingInstall> a_waiting, std::uint32_t a_actor,
         std::uint32_t a_nowMS);
[[nodiscard]] std::vector<WaitingInstall>
Removed(std::vector<WaitingInstall> a_waiting, std::uint32_t a_actor);
[[nodiscard]] std::optional<std::uint32_t>
WaitedMS(std::span<const WaitingInstall> a_waiting, std::uint32_t a_actor,
         std::uint32_t a_nowMS) noexcept;
}
