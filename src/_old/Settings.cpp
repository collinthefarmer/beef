#include "Settings.h"

#include "Identity.h"

#include <fstream>
#include <sstream>

namespace BetterEnchantmentEffects
{
	namespace
	{
		Settings g_settings{};
	}

	std::filesystem::path SettingsPath()
	{
		return Identity::IniPath();
	}

	Settings LoadSettingsFromDisk()
	{
		const auto    path = SettingsPath();
		std::ifstream file{ path };
		if (!file) {
			logger::warn("settings: {} not found, using defaults", path.string());
			return Settings{};
		}
		std::stringstream buffer;
		buffer << file.rdbuf();
		auto s = Settings::Parse(buffer.str());
		logger::info("settings loaded from {}:", path.string());
		std::istringstream lines{ s.Serialize() };
		for (std::string text; std::getline(lines, text);) {
			if (!text.empty() && text.front() != ';') {
				logger::info("  {}", text);
			}
		}
		return s;
	}

	bool SaveSettingsToDisk(const Settings& a_settings)
	{
		std::ofstream out{ SettingsPath(), std::ios::trunc };
		if (!out) {
			logger::error("settings: cannot write {}", SettingsPath().string());
			return false;
		}
		out << a_settings.Serialize();
		logger::info("settings: saved {}", SettingsPath().string());
		return static_cast<bool>(out);
	}

	std::string FormKeyOf(const RE::TESForm& a_form)
	{
		const auto* file = a_form.GetFile(0);
		std::string stem = file && file->GetFilename().data() ? std::string{ file->GetFilename() } : "unknown";
		if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) {
			stem.erase(dot);
		}
		const auto id = a_form.GetFormID();
		const auto local = (file && file->IsLight()) ? (id & 0xFFF) : (id & 0xFFFFFF);
		auto       key = std::format("{}~{:06x}", stem, local);
		for (auto& c : key) {
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		return key;
	}

	const Settings& GetSettings() noexcept
	{
		return g_settings;
	}

	Settings& GetMutableSettings() noexcept
	{
		return g_settings;
	}

	void SetSettings(Settings a_settings) noexcept
	{
		g_settings = std::move(a_settings);
	}
}
