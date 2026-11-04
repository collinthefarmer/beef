// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>

namespace BetterEnchantmentEffects {
inline constexpr std::uint32_t kMinAnimationFPS = 15;
inline constexpr std::uint32_t kMaxAnimationFPS = 60;
inline constexpr float kMinAnimationSpeed = 0.05f;
inline constexpr float kMaxAnimationSpeed = 4.0f;
inline constexpr float kMaxEvictDistance = 20000.0f;
inline constexpr std::uint32_t kMaxRenderTargetBudgetMiB = 65536;
inline constexpr std::uint32_t kMinUnpublishedDrawBudgetMs = 1;
inline constexpr std::uint32_t kMaxUnpublishedDrawBudgetMs = 16;
inline constexpr float kMaxStepDownHoldSeconds = 30.0f;
inline constexpr float kMaxHiddenReleaseSeconds = 60.0f;

enum class TextureScale {
  kQuarter,
  kHalf,
  kFull,
};

enum class NpcResolution {
  kFull,
  kHalf,
  kQuarter,
  kEighth,
};

[[nodiscard]] std::string_view TextureScaleName(TextureScale a_scale) noexcept;
[[nodiscard]] std::optional<TextureScale>
TextureScaleFromString(std::string_view a_text);
[[nodiscard]] std::span<const std::string_view> TextureScaleNames() noexcept;

struct Settings {
  bool playerOnly = false;
  bool enableShaders = true;
  bool thirdPerson = true;
  bool firstPerson = true;
  bool uniqueMaterial = true;
  bool verboseLogging = false;
  bool diagnosticLogging = true;
  bool gpuTiming = false;
  bool publishEffects = true;
  bool generatedShaders = true;
  bool renderPlanCache = true;
  bool verifyRenderPlanCache = false;
  bool cullOutOfView = true;
  bool sizeByScreen = true;
  bool showDebugRecipes = false;
  std::uint32_t renderTargetBudgetMiB = 0;
  std::uint32_t animationFPS = 60;
  std::uint32_t unpublishedDrawBudgetMs = 4;
  float animationSpeed = 1.0f;
  float evictDistance = 0.0f;
  float stepDownHoldSeconds = 2.0f;
  float hiddenReleaseSeconds = 5.0f;
  TextureScale textureScale = TextureScale::kFull;
  NpcResolution npcResolution = NpcResolution::kHalf;

  [[nodiscard]] std::uint32_t TickIntervalMS() const noexcept {
    return 1000u / std::clamp(animationFPS, kMinAnimationFPS, kMaxAnimationFPS);
  }
  [[nodiscard]] std::uint64_t StepDownHoldMS() const noexcept {
    return MillisOf(stepDownHoldSeconds, kMaxStepDownHoldSeconds);
  }
  [[nodiscard]] std::uint64_t HiddenReleaseMS() const noexcept {
    return MillisOf(hiddenReleaseSeconds, kMaxHiddenReleaseSeconds);
  }

  [[nodiscard]] static Settings Parse(std::string_view a_text);
  [[nodiscard]] std::string Serialize() const;

private:
  [[nodiscard]] static std::uint64_t MillisOf(float a_seconds,
                                              float a_maxSeconds) noexcept {
    if (!(a_seconds > 0.0f)) {
      return 0;
    }
    return static_cast<std::uint64_t>(std::min(a_seconds, a_maxSeconds) *
                                      1000.0f);
  }
};

struct SettingDesc {
  using Member =
      std::variant<bool Settings::*, float Settings::*,
                   std::uint32_t Settings::*, TextureScale Settings::*,
                   NpcResolution Settings::*>;

  enum class Widget {
    kCheckbox,
    kSlider,
    kIntSlider,
    kCombo,
    kBudgetCap,
  };

  const char *section;
  const char *key;
  const char *label;
  const char *help;
  Member member;
  float min;
  float max;
  Widget widget;
  bool reapply;
  std::span<const std::string_view> items = {};
};

[[nodiscard]] std::span<const SettingDesc> SettingTable();

[[nodiscard]] Settings NormalizeSettings(Settings a_settings);

[[nodiscard]] bool SettingsDiffer(const Settings &a_lhs, const Settings &a_rhs);
[[nodiscard]] bool ReapplyDiffers(const Settings &a_lhs, const Settings &a_rhs);
}
