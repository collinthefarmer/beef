// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "regression/Run.h"

#include <array>
#include <cstdint>
#include <string>

namespace BetterEnchantmentEffects {
struct RunWorld {
  std::array<RE::FormID, Regression::kRoleCount> spawned{};
  RE::FormID marker = 0;
  std::int32_t request = 0;
  std::string save;
  std::uint32_t loads = 0;
  std::uint64_t edit = 0;
  std::uint64_t gesture = 0;
  std::uint64_t file = 0;
};

[[nodiscard]] Regression::Observation Observe(const RunWorld &a_world);
void Execute(const Regression::Command &a_command, RunWorld &a_world);
void ReleaseWorld(RunWorld &a_world);
void ForgetWorldAfterLoad(RunWorld &a_world);
void QuitGame();
}
