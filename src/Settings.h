#pragma once

#include "PCH.h"
#include "SettingsCore.h"

namespace WornEnchantmentPBR
{
	[[nodiscard]] std::filesystem::path SettingsPath();
	[[nodiscard]] Settings              LoadSettingsFromDisk();
	bool                                SaveSettingsToDisk(const Settings& a_settings);

	// Load-order independent identity for a form: "<plugin stem>~<local id hex>",
	// e.g. "skyrim~092ded". Used for INI colour overrides and flipbook folders.
	[[nodiscard]] std::string FormKeyOf(const RE::TESForm& a_form);

	[[nodiscard]] const Settings& GetSettings() noexcept;
	[[nodiscard]] Settings&       GetMutableSettings() noexcept;  // menu edits, game thread only
	void                          SetSettings(Settings a_settings) noexcept;
}
