// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "PCH.h"
#include "regression/Run.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct CrowdMember {
  RE::FormID actor = 0;
  RE::FormID armor = 0;
};

struct ArmorTag {
  RE::FormID armor = 0;
  bool addedKeyword = false;
  RE::FormID enchantmentBefore = 0;
};

struct RunWorld {
  std::array<RE::FormID, Regression::kRoleCount> spawned{};
  RE::FormID marker = 0;
  std::optional<std::uint64_t> request;
  std::string save;
  std::uint32_t loads = 0;
  std::uint64_t edit = 0;
  std::uint64_t gesture = 0;
  std::uint64_t file = 0;
  std::vector<CrowdMember> crowd;
  std::vector<ArmorTag> tags;
};

[[nodiscard]] Regression::Observation Observe(const RunWorld &a_world);
void Execute(const Regression::Command &a_command, RunWorld &a_world);
void ReleaseWorld(RunWorld &a_world);
void ForgetWorldAfterLoad(RunWorld &a_world);
void QuitGame();
}
