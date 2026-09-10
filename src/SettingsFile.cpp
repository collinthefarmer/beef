#include "SettingsFile.h"

#include "Identity.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace BetterEnchantmentEffects {
namespace {
Settings g_settings{};

std::string_view TrimIni(std::string_view a_text) {
  while (!a_text.empty() &&
         std::isspace(static_cast<unsigned char>(a_text.front()))) {
    a_text.remove_prefix(1);
  }
  while (!a_text.empty() &&
         std::isspace(static_cast<unsigned char>(a_text.back()))) {
    a_text.remove_suffix(1);
  }
  return a_text;
}

bool EqualsIgnoreCase(std::string_view a_lhs, std::string_view a_rhs) {
  if (a_lhs.size() != a_rhs.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a_lhs.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a_lhs[i])) !=
        std::tolower(static_cast<unsigned char>(a_rhs[i]))) {
      return false;
    }
  }
  return true;
}

std::optional<std::string_view> IniValue(std::string_view a_text,
                                         std::string_view a_key) {
  std::size_t pos = 0;
  while (pos <= a_text.size()) {
    const auto newline = a_text.find('\n', pos);
    const auto line = TrimIni(a_text.substr(
        pos, newline == std::string_view::npos ? std::string_view::npos
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
    if (EqualsIgnoreCase(TrimIni(line.substr(0, eq)), a_key)) {
      auto value = TrimIni(line.substr(eq + 1));
      if (const auto comment = value.find(';');
          comment != std::string_view::npos) {
        value = TrimIni(value.substr(0, comment));
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
  std::ofstream out{SettingsPath(), std::ios::trunc};
  if (!out) {
    logger::error("settings: cannot write {}", SettingsPath().string());
    return false;
  }
  out << a_settings.Serialize();
  logger::info("settings: saved {}", SettingsPath().string());
  return static_cast<bool>(out);
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

const Settings &GetSettings() noexcept { return g_settings; }

Settings &GetMutableSettings() noexcept { return g_settings; }

void SetSettings(Settings a_settings) noexcept { g_settings = a_settings; }
}
