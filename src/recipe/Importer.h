#pragma once

#include "recipe/Efsh.h"
#include "recipe/Recipe.h"

#include <expected>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
struct EffectShaderRecord {
  FormKey key;
  std::string editorId;
  std::string fillTexture;
  Efsh::EffectParams params;
  float tileU = 1.0f;
  float tileV = 1.0f;

  [[nodiscard]] FormRef Reference() const;
};

[[nodiscard]] std::expected<EffectShaderRecord, std::string>
ParseEffectShaderRecord(std::string_view a_json);

[[nodiscard]] std::string RecipeIdFor(const EffectShaderRecord &a_record);

[[nodiscard]] Recipe ImportEffectShader(const EffectShaderRecord &a_record);
}
