// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace RE {
struct TESForm {
  std::uint32_t id = 0;
};
struct BGSKeyword : TESForm {};
struct TESEffectShader : TESForm {};
struct EffectSetting : TESForm {
  TESEffectShader *shader = nullptr;
};
struct Effect {
  EffectSetting *baseEffect = nullptr;
  float cost = 0.0f;
  float magnitude = 0.0f;
  float GetMagnitude() const { return magnitude; }
};
struct MagicItem : TESForm {
  std::vector<Effect *> effects;
  Effect *GetCostliestEffectItem() const {
    Effect *selected = nullptr;
    for (auto *effect : effects) {
      if (effect && effect->baseEffect &&
          (!selected || effect->cost > selected->cost))
        selected = effect;
    }
    return selected;
  }
};
struct TESObjectARMO : TESForm {
  std::vector<BGSKeyword *> keywords;
  std::uint32_t GetNumKeywords() const {
    return static_cast<std::uint32_t>(keywords.size());
  }
  std::optional<BGSKeyword *> GetKeywordAt(std::uint32_t a_index) const {
    if (a_index >= keywords.size())
      return std::nullopt;
    return keywords[a_index];
  }
};
}
