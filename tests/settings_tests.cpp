// Round-trip and clamp checks for the settings table.

#include "SettingsCore.h"

#include <cstdio>
#include <string>

using namespace WornEnchantmentPBR;

namespace
{
	int failures = 0;

	void Check(bool ok, const char* what)
	{
		if (!ok) {
			++failures;
			std::printf("FAIL: %s\n", what);
		}
	}
}

int main()
{
	// Every table row has a unique (section, key) and page/label text.
	{
		const auto table = SettingTable();
		for (std::size_t i = 0; i < table.size(); ++i) {
			Check(table[i].page && *table[i].page && table[i].label && *table[i].label, "row has page and label");
			for (std::size_t j = i + 1; j < table.size(); ++j) {
				Check(std::string{ table[i].section } != table[j].section || std::string{ table[i].key } != table[j].key, "keys are unique per section");
			}
		}
	}

	// Defaults survive a serialise/parse round trip.
	{
		const Settings a{};
		const auto     text = a.Serialize();
		const auto     b = Settings::Parse(text);
		Check(!SettingsDiffer(a, b), "defaults round-trip");
	}

	// Edited values and overrides survive a round trip.
	{
		Settings a{};
		a.emissiveStrength = 3.25f;
		a.animationFPS = 45;
		a.sheen = false;
		a.glossMapSize = 512;
		a.colorOverrides["skyrim~092dee"] = Timing::Rgb{ 0.25f, 0.5f, 1.0f };
		const auto b = Settings::Parse(a.Serialize());
		Check(!SettingsDiffer(a, b), "edited values round-trip");
		Check(b.colorOverrides.count("skyrim~092dee") == 1, "override survives");
		Check(SettingsDiffer(a, Settings{}), "edited differs from defaults");
	}

	// Parser: sections, case, comments, clamps, 0..255 colours, unknown keys.
	{
		const auto s = Settings::Parse(
			"[general]\n"
			"animationfps = 200 ; too high\n"
			"AnimationSpeed=0\n"
			"Unknown=1\n"
			"[Layers]\n"
			"SheenScale=9\n"
			"# comment\n"
			"[Colors]\n"
			"TintWithEdge=off\n"
			"skyrim~092ded = 255, 128, 0\n");
		Check(s.animationFPS == 60, "fps clamps to 60");
		Check(s.animationSpeed == Timing::kMinAnimationSpeed, "speed clamps to minimum");
		Check(s.sheenScale == 4.0f, "sheen scale clamps to 4");
		Check(!s.tintWithEdge, "'off' parses as false");
		const auto it = s.colorOverrides.find("skyrim~092ded");
		Check(it != s.colorOverrides.end() && it->second.r == 1.0f && it->second.b == 0.0f, "0..255 colour parses");
	}

	if (failures == 0) {
		std::printf("all settings checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
