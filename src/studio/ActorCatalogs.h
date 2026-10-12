// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/MeshFacts.h"
#include "recipe/Recipe.h"
#include "studio/GameObjects.h"
#include "studio/Snapshot.h"

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
struct MaterialTexture {
  std::string geometry;
  Slot slot = Slot::kDiffuse;
  std::string path;
};

struct EffectShaderTextures {
  std::string fill;
  std::string membrane;
};

struct WornTextures {
  std::vector<MaterialTexture> materials;
  std::vector<EffectShaderTextures> shaders;
};

[[nodiscard]] std::vector<BoneCoverage>
SkinBonesOf(std::span<const PieceRow> a_pieces, FormID a_actor);
[[nodiscard]] GameObjectCatalog
NodeCatalogOf(std::span<const BoneCoverage> a_skin,
              std::span<const std::string> a_skeleton);

[[nodiscard]] std::string TexturePathKey(std::string_view a_path);
[[nodiscard]] GameObjectCatalog TextureCatalogOf(const WornTextures &a_worn);

[[nodiscard]] std::vector<std::string>
GeometryNamesOf(const RecipeRow &a_recipe);
}
