// GPL-3.0-only with the additional permission in COPYING.md.
#include "Settings.h"

#include "Core.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <format>
#include <optional>
#include <sstream>

namespace BetterEnchantmentEffects {
namespace {
using W = SettingDesc::Widget;

constexpr std::string_view kTextureScaleNames[]{"Quarter", "Half", "Full"};

constexpr std::array kTable{
    SettingDesc{"General", "PlayerOnly", "Player only",
                "Apply to the player only, not loaded NPCs.",
                &Settings::playerOnly, 0, 1, W::kCheckbox, true},
    SettingDesc{"General", "EnableShaders", "Enabled", "Master switch.",
                &Settings::enableShaders, 0, 1, W::kCheckbox, true},
    SettingDesc{"General", "ThirdPerson", "Third person",
                "Drive the normal actor model.", &Settings::thirdPerson, 0, 1,
                W::kCheckbox, true},
    SettingDesc{"General", "FirstPerson", "First person",
                "Drive the player's first-person arms.", &Settings::firstPerson,
                0, 1, W::kCheckbox, true},
    SettingDesc{"General", "UniqueMaterial", "Unique material per clone",
                "Give each worn clone its own copy of the pooled PBR material "
                "before writing to it.",
                &Settings::uniqueMaterial, 0, 1, W::kCheckbox, true},
    SettingDesc{"General", "VerboseLogging", "Verbose logging",
                "One line per geometry on apply, plus restore and ownership "
                "diagnostics.",
                &Settings::verboseLogging, 0, 1, W::kCheckbox, false},
    SettingDesc{
        "General", "DiagnosticLogging", "Diagnostic trace",
        "Write bounded transition and resource events to a unique JSONL log.",
        &Settings::diagnosticLogging, 0, 1, W::kCheckbox, false},
    SettingDesc{"General", "GpuTiming", "GPU timing",
                "Time the plugin's GPU work per render tick and write the "
                "totals to the diagnostic trace.",
                &Settings::gpuTiming, 0, 1, W::kCheckbox, false},
    SettingDesc{"General", "PublishEffects", "Publish effects",
                "Show the rendered effects on materials and shells. When off, "
                "effects still render but the original materials stay.",
                &Settings::publishEffects, 0, 1, W::kCheckbox, false},
    SettingDesc{"General", "FusionCheck", "Fusion check",
                "Also render each fused stack layer by layer, and each "
                "generated program with the interpreter, and record the "
                "largest differences in the diagnostic trace. Slow.",
                &Settings::fusionCheck, 0, 1, W::kCheckbox, false},
    SettingDesc{"General", "GeneratedShaders", "Generated shaders",
                "Draw recipe programs and stacks with shaders compiled for "
                "each one instead of the shared interpreter.",
                &Settings::generatedShaders, 0, 1, W::kCheckbox, false},
    SettingDesc{"General", "AnimationFPS", "Animation FPS",
                "Animation update rate.", &Settings::animationFPS,
                kMinAnimationFPS, kMaxAnimationFPS, W::kIntSlider, false},
    SettingDesc{"General", "AnimationSpeed", "Animation speed",
                "Time multiplier for the EFSH animation.",
                &Settings::animationSpeed, kMinAnimationSpeed,
                kMaxAnimationSpeed, W::kSlider, false},
    SettingDesc{"General", "TextureScale", "Texture scale",
                "Runtime texture resolution relative to the armor's own maps.",
                &Settings::textureScale, 0, 0, W::kCombo, true,
                kTextureScaleNames},
    SettingDesc{"General", "EvictDistance", "Evict distance",
                "Game units past which a non-player actor's effects are "
                "dropped and restored on approach; 0 disables eviction.",
                &Settings::evictDistance, 0.0f, kMaxEvictDistance, W::kSlider,
                false},
};

std::optional<bool> ParseBool(std::string_view a_value) {
  const auto v = Lower(a_value);
  if (v == "1" || v == "true" || v == "yes" || v == "on") {
    return true;
  }
  if (v == "0" || v == "false" || v == "no" || v == "off") {
    return false;
  }
  return std::nullopt;
}

std::optional<float> ParseFloat(std::string_view a_value) {
  std::string buf{a_value};
  char *end = nullptr;
  const float v = std::strtof(buf.c_str(), &end);
  if (end == buf.c_str() || !Trim({end}).empty() || !std::isfinite(v)) {
    return std::nullopt;
  }
  return v;
}

void Assign(Settings &a_settings, const SettingDesc &a_desc,
            std::string_view a_value) {
  std::visit(
      [&](auto a_member) {
        using T = std::remove_cvref_t<decltype(a_settings.*a_member)>;
        if constexpr (std::is_same_v<T, bool>) {
          if (const auto b = ParseBool(a_value)) {
            a_settings.*a_member = *b;
          }
        } else if constexpr (std::is_same_v<T, float>) {
          if (const auto f = ParseFloat(a_value)) {
            a_settings.*a_member = std::clamp(*f, a_desc.min, a_desc.max);
          }
        } else if constexpr (std::is_same_v<T, TextureScale>) {
          if (const auto scale = TextureScaleFromString(a_value)) {
            a_settings.*a_member = *scale;
          }
        } else {
          if (const auto f = ParseFloat(a_value)) {
            a_settings.*a_member = static_cast<std::uint32_t>(
                std::clamp(*f, a_desc.min, a_desc.max));
          }
        }
      },
      a_desc.member);
}

std::string Format(const Settings &a_settings, const SettingDesc &a_desc) {
  return std::visit(
      [&](auto a_member) -> std::string {
        using T = std::remove_cvref_t<decltype(a_settings.*a_member)>;
        if constexpr (std::is_same_v<T, bool>) {
          return a_settings.*a_member ? "true" : "false";
        } else if constexpr (std::is_same_v<T, float>) {
          return std::format("{:.4g}", a_settings.*a_member);
        } else if constexpr (std::is_same_v<T, TextureScale>) {
          return std::string{TextureScaleName(a_settings.*a_member)};
        } else {
          return std::to_string(a_settings.*a_member);
        }
      },
      a_desc.member);
}
}

std::string_view TextureScaleName(TextureScale a_scale) noexcept {
  const auto index = static_cast<std::size_t>(a_scale);
  return index < std::size(kTextureScaleNames)
             ? kTextureScaleNames[index]
             : kTextureScaleNames[static_cast<std::size_t>(
                   TextureScale::kFull)];
}

std::optional<TextureScale> TextureScaleFromString(std::string_view a_text) {
  const auto text = Lower(Trim(a_text));
  for (std::size_t i = 0; i < std::size(kTextureScaleNames); ++i) {
    if (text == Lower(kTextureScaleNames[i])) {
      return FromIndex<TextureScale>(i);
    }
  }
  return std::nullopt;
}

std::span<const std::string_view> TextureScaleNames() noexcept {
  return kTextureScaleNames;
}

std::span<const SettingDesc> SettingTable() { return kTable; }

Settings Settings::Parse(std::string_view a_text) {
  Settings s{};
  std::istringstream stream{std::string{a_text}};
  std::string line;
  std::string section = "general";
  while (std::getline(stream, line)) {
    auto text = Trim(line);
    if (text.empty() || text.front() == ';' || text.front() == '#') {
      continue;
    }
    if (text.front() == '[') {
      const auto close = text.find(']');
      section = Lower(Trim(text.substr(1, close == std::string_view::npos
                                              ? std::string_view::npos
                                              : close - 1)));
      continue;
    }
    const auto eq = text.find('=');
    if (eq == std::string_view::npos) {
      continue;
    }
    const auto key = Lower(Trim(text.substr(0, eq)));
    auto value = Trim(text.substr(eq + 1));
    if (const auto comment = value.find(';');
        comment != std::string_view::npos) {
      value = Trim(value.substr(0, comment));
    }

    for (const auto &desc : kTable) {
      if (section == Lower(desc.section) && key == Lower(desc.key)) {
        Assign(s, desc, value);
        break;
      }
    }
  }
  return s;
}

std::string Settings::Serialize() const {
  std::string out;
  std::string section;
  for (const auto &desc : kTable) {
    if (section != desc.section) {
      if (!section.empty()) {
        out += "\n";
      }
      section = desc.section;
      out += std::format("[{}]\n", section);
    }
    if (desc.help && *desc.help) {
      out += std::format("; {}\n", desc.help);
    }
    out += std::format("{}={}\n", desc.key, Format(*this, desc));
  }
  return out;
}

Settings NormalizeSettings(Settings a_settings) {
  const Settings defaults;
  for (const SettingDesc &desc : SettingTable()) {
    Match(desc.member, [&](auto a_member) {
      using T = std::remove_cvref_t<decltype(a_settings.*a_member)>;
      if constexpr (std::is_same_v<T, float>) {
        const float value = a_settings.*a_member;
        a_settings.*a_member = std::isfinite(value)
                                   ? std::clamp(value, desc.min, desc.max)
                                   : defaults.*a_member;
      } else if constexpr (std::is_same_v<T, std::uint32_t>) {
        a_settings.*a_member = std::clamp(a_settings.*a_member,
                                          static_cast<std::uint32_t>(desc.min),
                                          static_cast<std::uint32_t>(desc.max));
      } else if constexpr (std::is_same_v<T, TextureScale>) {
        const TextureScale value = a_settings.*a_member;
        a_settings.*a_member =
            static_cast<std::size_t>(value) < desc.items.size()
                ? value
                : defaults.*a_member;
      }
    });
  }
  return a_settings;
}

bool SettingsDiffer(const Settings &a_lhs, const Settings &a_rhs) {
  for (const auto &desc : kTable) {
    if (Format(a_lhs, desc) != Format(a_rhs, desc)) {
      return true;
    }
  }
  return false;
}
}
