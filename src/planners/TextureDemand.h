// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/TextureSize.h"
#include "planners/ActorPlan.h"
#include "planners/InterpreterProgram.h"
#include "planners/ValueIdentity.h"

namespace BetterEnchantmentEffects {
enum class TextureFormat { kRgba8, kRgba32Float };
enum class MipPolicy { kGenerate, kNone };
struct TextureRequirements {
  TextureSize size{TextureSize::kMin};
  TextureFormat format = TextureFormat::kRgba8;
  MipPolicy mipPolicy = MipPolicy::kGenerate;
  [[nodiscard]] bool operator==(const TextureRequirements &) const = default;
};
struct TextureKey {
  ValueIdentity identity;
  TextureRequirements requirements;
  [[nodiscard]] bool operator==(const TextureKey &) const = default;
  [[nodiscard]] std::strong_ordering operator<=>(const TextureKey &other) const;
};
enum class TextureUseInput { kSource, kMask, kFunctionArgument };
struct TextureUse {
  PlacementId placement{};
  std::size_t output = 0;
  std::size_t layer = 0;
  TextureUseInput input = TextureUseInput::kSource;
  [[nodiscard]] bool operator==(const TextureUse &) const = default;
};
struct TextureValue {
  const RecipeGraph *graph = nullptr;
  OutputRef output;
  std::size_t instance = 0;
  [[nodiscard]] bool operator==(const TextureValue &) const = default;
};
using TextureDemandId = std::size_t;
struct TextureDemand {
  TextureKey key;
  TextureValue value;
  std::vector<TextureDemandId> dependencies;
  std::vector<TextureUse> dependents;
  std::optional<InterpreterProgram> program;
  GeometryId geometry{};
};
[[nodiscard]] std::expected<TextureDemandId, std::string>
CollectTextureDemand(std::vector<TextureDemand> &demands, TextureValue value,
                     TextureRequirements requirements, const TextureUse &use,
                     const ValueBindings &bindings, GeometryId geometry = {});
}
