#pragma once

#include "PCH.h"
#include "Settings.h"

namespace BetterEnchantmentEffects {
[[nodiscard]] std::filesystem::path SettingsPath();
[[nodiscard]] Settings LoadSettingsFromDisk();
bool SaveSettingsToDisk(const Settings &a_settings);

[[nodiscard]] std::string FormKeyOf(const RE::TESForm &a_form);

[[nodiscard]] Settings GetSettings();
void SetSettings(Settings a_settings);
}
