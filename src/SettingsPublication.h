#pragma once

#include "Settings.h"

#include <mutex>

namespace BetterEnchantmentEffects {
class SettingsPublication {
public:
  [[nodiscard]] Settings Read() const {
    const std::lock_guard lock{mutex_};
    return settings_;
  }

  void Publish(Settings a_settings) {
    const Settings checked = NormalizeSettings(a_settings);
    const std::lock_guard lock{mutex_};
    settings_ = checked;
  }

private:
  mutable std::mutex mutex_;
  Settings settings_;
};
}
