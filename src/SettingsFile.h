#pragma once

#include "PCH.h"
#include "Settings.h"

namespace BetterEnchantmentEffects {
[[nodiscard]] std::filesystem::path SettingsPath();
[[nodiscard]] Settings LoadSettingsFromDisk();
bool SaveSettingsToDisk(const Settings &a_settings);

[[nodiscard]] std::string FormKeyOf(const RE::TESForm &a_form);

[[nodiscard]] const Settings &GetSettings() noexcept;
[[nodiscard]] Settings &GetMutableSettings() noexcept;
void SetSettings(Settings a_settings) noexcept;
}
