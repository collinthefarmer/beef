// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "recipe/Efsh.h"
#include "recipe/Recipe.h"

#include <array>
#include <expected>
#include <span>
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

inline constexpr std::string_view kFillTextureToken = "$fillTexture";
inline constexpr std::array<std::string_view, 2> kImportTemplateIds{"fill",
                                                                    "bare"};

[[nodiscard]] std::expected<EffectShaderRecord, std::string>
ParseEffectShaderRecord(std::string_view a_json);

[[nodiscard]] std::string RecipeIdFor(const EffectShaderRecord &a_record);

[[nodiscard]] std::string_view
ImportTemplateId(const EffectShaderRecord &a_record) noexcept;

[[nodiscard]] std::span<const std::string_view> ImportSignalNames() noexcept;

[[nodiscard]] Recipe ImportEffectShader(const EffectShaderRecord &a_record,
                                        const Recipe &a_template);
}
