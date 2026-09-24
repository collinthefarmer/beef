// GPL-3.0-only with the additional permission in COPYING.md.
#include "SettingsFile.h"

#include "Core.h"
#include "Identity.h"
#include "SettingsPublication.h"
#include "diagnostics/Trace.h"
#include "engine/TextFile.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace BetterEnchantmentEffects {
namespace {
SettingsPublication g_settings;

std::optional<std::string_view> IniValue(std::string_view a_text,
                                         std::string_view a_key) {
  std::size_t pos = 0;
  while (pos <= a_text.size()) {
    const auto newline = a_text.find('\n', pos);
    const auto line = Trim(a_text.substr(pos, newline == std::string_view::npos
                                                  ? std::string_view::npos
                                                  : newline - pos));
    pos = newline == std::string_view::npos ? a_text.size() + 1 : newline + 1;
    if (line.empty() || line.front() == ';' || line.front() == '#' ||
        line.front() == '[') {
      continue;
    }
    const auto eq = line.find('=');
    if (eq == std::string_view::npos) {
      continue;
    }
    if (EqualsIgnoreCase(Trim(line.substr(0, eq)), a_key)) {
      auto value = Trim(line.substr(eq + 1));
      if (const auto comment = value.find(';');
          comment != std::string_view::npos) {
        value = Trim(value.substr(0, comment));
      }
      return value;
    }
  }
  return std::nullopt;
}
}

std::filesystem::path SettingsPath() { return Identity::IniPath(); }

Settings LoadSettingsFromDisk() {
  const auto path = SettingsPath();
  std::ifstream file{path};
  if (!file) {
    logger::warn("settings: {} not found, using defaults", path.string());
    return Settings{};
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  const auto text = buffer.str();
  if (const auto raw = IniValue(text, "TextureScale");
      raw && !TextureScaleFromString(*raw)) {
    logger::warn("settings: unrecognised TextureScale '{}', using {}", *raw,
                 TextureScaleName(TextureScale::kFull));
  }
  auto s = Settings::Parse(text);
  logger::info("settings loaded from {}:", path.string());
  std::istringstream lines{s.Serialize()};
  for (std::string text; std::getline(lines, text);) {
    if (!text.empty() && text.front() != ';') {
      logger::info("  {}", text);
    }
  }
  return s;
}

bool SaveSettingsToDisk(const Settings &a_settings) {
  const auto path = SettingsPath();
  if (!WriteText(path, a_settings.Serialize())) {
    logger::error("settings: cannot write {}", path.string());
    return false;
  }
  logger::info("settings: saved {}", path.string());
  return true;
}

std::string FormKeyOf(const RE::TESForm &a_form) {
  const auto *file = a_form.GetFile(0);
  std::string stem = file && file->GetFilename().data()
                         ? std::string{file->GetFilename()}
                         : "unknown";
  if (const auto dot = stem.find_last_of('.'); dot != std::string::npos) {
    stem.erase(dot);
  }
  const auto id = a_form.GetFormID();
  const auto local = (file && file->IsLight()) ? (id & 0xFFF) : (id & 0xFFFFFF);
  auto key = std::format("{}~{:06x}", stem, local);
  for (auto &c : key) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return key;
}

Settings GetSettings() { return g_settings.Read(); }

void SetSettings(Settings a_settings) {
  a_settings = NormalizeSettings(a_settings);
  Trace::Get().Enable(true);
  Trace::Emit(
      Trace::Event::kSettings,
      {{"fingerprint_fnv1a64", Trace::Fingerprint(a_settings.Serialize())},
       {"effective", a_settings.Serialize()}});
  Trace::Get().Enable(a_settings.diagnosticLogging);
  g_settings.Publish(a_settings);
}
}
