// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/EngineForms.h"

#include "engine/Tweaks.h"

namespace BetterEnchantmentEffects {
namespace {
Vec3 ToRgb(const RE::Color &a_color) {
  return Vec3{a_color.red / 255.0f, a_color.green / 255.0f,
              a_color.blue / 255.0f};
}
}

FormKey FormKeyFor(const RE::TESForm &a_form) {
  const auto *file = a_form.GetFile(0);
  FormKey key;
  key.file = file && file->GetFilename().data()
                 ? std::string{file->GetFilename()}
                 : "unknown";
  const auto id = a_form.GetFormID();
  key.localId = (file && file->IsLight()) ? (id & 0xFFF) : (id & 0xFFFFFF);
  return key;
}

RE::TESForm *LookupForm(const FormKey &a_key) {
  auto *handler = RE::TESDataHandler::GetSingleton();
  if (!handler || a_key.file.empty()) {
    return nullptr;
  }
  return handler->LookupForm(a_key.localId, a_key.file);
}

EffectShaderRecord RecordFrom(const RE::TESEffectShader &a_shader) {
  const auto &d = a_shader.data;
  EffectShaderRecord r;
  r.key = FormKeyFor(a_shader);
  r.editorId = EditorIdOf(a_shader);
  r.fillTexture = a_shader.fillTexture.textureName.c_str()
                      ? a_shader.fillTexture.textureName.c_str()
                      : "";
  auto &p = r.params;
  p.colorKeys = {ToRgb(d.fillTextureEffectColorKey1),
                 ToRgb(d.fillTextureEffectColorKey2),
                 ToRgb(d.fillTextureEffectColorKey3)};
  p.colorKeyTimes = {d.fillTextureEffectColorKeyScaleTimeColorKey1Time,
                     d.fillTextureEffectColorKeyScaleTimeColorKey2Time,
                     d.fillTextureEffectColorKeyScaleTimeColorKey3Time};
  p.colorKeyScales = {d.fillTextureEffectColorKeyScaleTimeColorKey1Scale,
                      d.fillTextureEffectColorKeyScaleTimeColorKey2Scale,
                      d.fillTextureEffectColorKeyScaleTimeColorKey3Scale};
  p.colorScale = d.colorScale;
  p.fill.fullAlphaRatio = d.fillTextureEffectFullAlphaRatio;
  p.fill.persistentAlphaRatio = d.fillTextureEffectPersistentAlphaRatio;
  p.fill.pulseAmplitude = d.fillTextureEffectAlphaPulseAmplitude;
  p.fill.pulseFrequency = d.fillTextureEffectAlphaPulseFrequency;
  p.fill.fadeInTime = d.fillTextureEffectAlphaFadeInTime;
  p.animationSpeedU = d.fillTextureEffectTextureAnimationSpeedU;
  p.animationSpeedV = d.fillTextureEffectTextureAnimationSpeedV;
  p.edgeColor = ToRgb(d.edgeEffectColor);
  p.edge.fullAlphaRatio = d.edgeEffectFullAlphaRatio;
  p.edge.persistentAlphaRatio = d.edgeEffectPersistentAlphaRatio;
  p.edge.pulseAmplitude = d.edgeEffectAlphaPulseAmplitude;
  p.edge.pulseFrequency = d.edgeEffectAlphaPulseFrequency;
  p.edge.fadeInTime = d.edgeEffectAlphaFadeInTime;
  r.tileU = d.fillTextureEffectTextureScaleU;
  r.tileV = d.fillTextureEffectTextureScaleV;
  return r;
}

RE::TESEffectShader *ShaderFor(const RE::EffectSetting *a_effect) {
  if (!a_effect) {
    return nullptr;
  }
  if (const auto *visuals = a_effect->data.enchantVisuals;
      visuals && visuals->data.effectShader) {
    return visuals->data.effectShader;
  }
  return a_effect->data.enchantShader;
}

RE::TESEffectShader *ShaderFor(const RE::MagicItem *a_item) {
  if (!a_item) {
    return nullptr;
  }
  if (const auto *costliest = a_item->GetCostliestEffectItem()) {
    if (auto *shader = ShaderFor(costliest->baseEffect)) {
      return shader;
    }
  }
  for (const auto *effect : a_item->effects) {
    if (effect) {
      if (auto *shader = ShaderFor(effect->baseEffect)) {
        return shader;
      }
    }
  }
  return nullptr;
}
}
