// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "planners/ValueIdentity.h"
#include "recipe/Recipe.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace BetterEnchantmentEffects {
struct MatchedShaderFacts {
  std::string form;
  std::string fill;
  std::string membrane;
  [[nodiscard]] bool operator==(const MatchedShaderFacts &) const = default;
};

struct IdentityFacts {
  std::string instance;
  std::string mesh;
  std::string material;
  std::uintptr_t geometry = 0;
  std::uintptr_t root = 0;
  float itemHash = 0.0f;
  std::array<float, kEnchantmentFieldCount> enchantment{};
  MatchedShaderFacts shader;
  std::string geometryName;
  std::uint32_t geometryIndex = 0;
  std::uint32_t geometryCount = 1;
  [[nodiscard]] bool operator==(const IdentityFacts &) const = default;
};

[[nodiscard]] const std::string &
TexturePathOf(const MatchedShaderFacts &a_shader,
              EffectShaderTexture a_texture) noexcept;

[[nodiscard]] std::string InstanceIdentity(std::uintptr_t a_graph,
                                           std::size_t a_context,
                                           std::uint32_t a_actor);
[[nodiscard]] ValueBindings
IdentityBindings(std::shared_ptr<const IdentityFacts> a_facts);
}
