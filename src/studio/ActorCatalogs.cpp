// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/ActorCatalogs.h"

#include "Core.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <unordered_set>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
void MergeBone(std::vector<BoneCoverage> &a_skin, const BoneCoverage &a_bone) {
  const auto known =
      std::ranges::find(a_skin, a_bone.name, &BoneCoverage::name);
  if (known == a_skin.end()) {
    a_skin.push_back(a_bone);
    return;
  }
  known->coverage = std::max(known->coverage, a_bone.coverage);
}

[[nodiscard]] bool IsMoreCovered(const BoneCoverage &a_first,
                                 const BoneCoverage &a_second) {
  if (a_first.coverage != a_second.coverage) {
    return a_first.coverage > a_second.coverage;
  }
  return Lower(a_first.name) < Lower(a_second.name);
}
}

std::vector<BoneCoverage> SkinBonesOf(std::span<const PieceRow> a_pieces,
                                      FormID a_actor) {
  std::vector<BoneCoverage> skin;
  for (const PieceRow &piece : a_pieces) {
    if (piece.ref.actorID != a_actor) {
      continue;
    }
    for (const RecipeRow &recipe : piece.recipes) {
      for (const GeometryRow &geometry : recipe.geometries) {
        for (const BoneCoverage &bone : geometry.bones) {
          MergeBone(skin, bone);
        }
      }
    }
  }
  std::ranges::sort(skin, IsMoreCovered);
  return skin;
}

namespace {
[[nodiscard]] GameObjectCandidate NodeCandidate(const std::string &a_name,
                                                std::string a_qualifier) {
  return GameObjectCandidate{a_name, a_name, std::move(a_qualifier)};
}

[[nodiscard]] std::string SkinQualifier(float a_coverage) {
  const long percent = std::lround(std::clamp(a_coverage, 0.0f, 1.0f) * 100.0f);
  return std::format("skin {}%", percent);
}

[[nodiscard]] bool IsBeforeAlphabetically(const std::string &a_first,
                                          const std::string &a_second) {
  return Lower(a_first) < Lower(a_second);
}
}

GameObjectCatalog NodeCatalogOf(std::span<const BoneCoverage> a_skin,
                                std::span<const std::string> a_skeleton) {
  GameObjectCatalog catalog;
  catalog.kind = GameObjectKind::kNode;
  std::unordered_set<std::string> listed;
  for (const BoneCoverage &bone : a_skin) {
    if (bone.name.empty() || !listed.insert(bone.name).second) {
      continue;
    }
    catalog.candidates.push_back(
        NodeCandidate(bone.name, SkinQualifier(bone.coverage)));
  }
  const std::size_t skinCount = catalog.candidates.size();

  std::vector<std::string> rest;
  for (const std::string &node : a_skeleton) {
    if (!node.empty() && listed.insert(node).second) {
      rest.push_back(node);
    }
  }
  std::ranges::sort(rest, IsBeforeAlphabetically);
  for (const std::string &node : rest) {
    catalog.candidates.push_back(NodeCandidate(node, "skeleton"));
  }
  catalog.sourceNote =
      std::format("{} skin bone(s), {} other node(s)", skinCount,
                  catalog.candidates.size() - skinCount);
  return catalog;
}

std::string TexturePathKey(std::string_view a_path) {
  std::string key = Lower(a_path);
  std::ranges::replace(key, '/', '\\');
  while (key.starts_with('\\')) {
    key.erase(0, 1);
  }
  if (key.starts_with("data\\")) {
    key.erase(0, 5);
  }
  return key;
}

namespace {
struct TextureCatalogBuilder {
  GameObjectCatalog catalog{.kind = GameObjectKind::kTexture};
  std::unordered_set<std::string> listed;

  void Add(std::string_view a_path, std::string a_qualifier) {
    std::string key = TexturePathKey(a_path);
    if (!key.ends_with(".dds") || !listed.insert(key).second) {
      return;
    }
    catalog.candidates.push_back(
        GameObjectCandidate{key, key, std::move(a_qualifier)});
  }
};
}

GameObjectCatalog TextureCatalogOf(const WornTextures &a_worn) {
  TextureCatalogBuilder builder;
  for (const MaterialTexture &material : a_worn.materials) {
    builder.Add(material.path, std::format("{} {}", material.geometry,
                                           SlotName(material.slot)));
  }
  for (const EffectShaderTextures &shader : a_worn.shaders) {
    builder.Add(shader.fill, "effect shader fill");
    builder.Add(shader.membrane, "effect shader membrane");
  }
  builder.catalog.sourceNote =
      std::format("{} texture(s) in use", builder.catalog.candidates.size());
  return std::move(builder.catalog);
}

std::vector<std::string> GeometryNamesOf(const RecipeRow &a_recipe) {
  std::vector<std::string> names;
  for (const GeometryRow &geometry : a_recipe.geometries) {
    if (!geometry.name.empty() &&
        !std::ranges::contains(names, geometry.name)) {
      names.push_back(geometry.name);
    }
  }
  return names;
}
}
