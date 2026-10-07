// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/IdleCamera.h"

namespace BetterEnchantmentEffects {
namespace {
bool InIdleCamera(const RE::PlayerCamera &a_camera) {
  const RE::TESCameraState *state = a_camera.currentState.get();
  return state && state->id == RE::CameraState::kAutoVanity;
}
}

RE::Setting *VanityDelay() {
  RE::INISettingCollection *settings = RE::INISettingCollection::GetSingleton();
  RE::Setting *setting =
      settings ? settings->GetSetting(kVanityDelaySetting) : nullptr;
  return setting && setting->GetType() == RE::Setting::Type::kFloat ? setting
                                                                    : nullptr;
}

std::optional<IdleCameraHold> HoldIdleCamera() {
  RE::Setting *delay = VanityDelay();
  if (!delay) {
    return std::nullopt;
  }
  const IdleCameraHold hold{delay->GetFloat()};
  delay->data.f = kVanityNever;
  if (RE::PlayerCamera *camera = RE::PlayerCamera::GetSingleton();
      camera && InIdleCamera(*camera)) {
    camera->ForceThirdPerson();
  }
  return hold;
}

void ReleaseIdleCamera(const IdleCameraHold &a_hold) {
  if (RE::Setting *delay = VanityDelay()) {
    delay->data.f = a_hold.delay;
  }
}
}
