// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "mesh/TextureSize.h"

#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects {
struct RecipeTextureKey {
  std::string recipe;
  std::string name;
  std::uint32_t pixels;
  std::size_t context;

  RecipeTextureKey(std::string_view a_recipe, std::string_view a_name,
                   TextureSize a_size, std::size_t a_context = 0)
      : recipe(a_recipe), name(a_name), pixels(a_size.Pixels()),
        context(a_context) {}

  [[nodiscard]] std::strong_ordering
  operator<=>(const RecipeTextureKey &) const = default;
  [[nodiscard]] bool operator==(const RecipeTextureKey &) const = default;
};

template <class T>
using RecipeTextureCache = std::map<RecipeTextureKey, std::shared_ptr<T>>;

template <class T>
[[nodiscard]] std::shared_ptr<T>
FindRecipeTexture(const RecipeTextureCache<T> &a_cache,
                  std::string_view a_recipe, std::string_view a_name,
                  TextureSize a_size, std::size_t a_context = 0) {
  const auto found =
      a_cache.find(RecipeTextureKey{a_recipe, a_name, a_size, a_context});
  return found != a_cache.end() ? found->second : nullptr;
}

template <class T>
[[nodiscard]] std::shared_ptr<T>
LargestRecipeTexture(const RecipeTextureCache<T> &a_cache,
                     std::string_view a_recipe, std::string_view a_name,
                     std::size_t a_context = 0) {
  std::shared_ptr<T> best;
  std::uint32_t bestSize = 0;
  for (const auto &[key, value] : a_cache) {
    if (key.context == a_context && key.recipe == a_recipe &&
        key.name == a_name && value && (!best || key.pixels > bestSize)) {
      best = value;
      bestSize = key.pixels;
    }
  }
  return best;
}
}
