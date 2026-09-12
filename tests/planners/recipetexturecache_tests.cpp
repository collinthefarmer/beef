#include "planners/RecipeTextureCache.h"
#include "test_support.h"

#include <memory>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  const TextureSize small{64};
  const TextureSize large{1024};
  const auto normal = std::make_shared<int>(1);
  const auto scratch = std::make_shared<int>(2);
  const auto largeNormal = std::make_shared<int>(3);
  const auto normalDependency = std::make_shared<int>(4);
  const auto scratchDependency = std::make_shared<int>(5);
  RecipeTextureCache<int> masks;
  masks.emplace(RecipeTextureKey{"normal", "mask", small}, normal);
  masks.emplace(RecipeTextureKey{"scratch", "mask", small}, scratch);
  masks.emplace(RecipeTextureKey{"normal", "mask", large}, largeNormal);
  masks.emplace(RecipeTextureKey{"normal", "dependency", small},
                normalDependency);
  masks.emplace(RecipeTextureKey{"scratch", "dependency", small},
                scratchDependency);
  Check(FindRecipeTexture(masks, "normal", "mask", small) == normal,
        "normal mask lookup retains its recipe scope");
  Check(FindRecipeTexture(masks, "scratch", "mask", small) == scratch,
        "scratch mask with the same local name has a separate entry");
  Check(FindRecipeTexture(masks, "normal", "mask", large) == largeNormal,
        "the same mask retains separate resolutions");
  Check(!FindRecipeTexture(masks, "scratch", "mask", large),
        "an absent scratch resolution never borrows the normal mask");
  Check(LargestRecipeTexture(masks, "normal", "mask") == largeNormal,
        "inspection chooses the largest mask within the requested recipe");
  Check(LargestRecipeTexture(masks, "scratch", "mask") == scratch,
        "a larger normal mask cannot replace scratch inspection");
  Check(!LargestRecipeTexture(masks, "unrelated", "mask"),
        "inspection of an unrelated recipe reports no rendered mask");
  Check(FindRecipeTexture(masks, "normal", "dependency", small) ==
            normalDependency,
        "normal dependency lookup stays within its recipe");
  Check(FindRecipeTexture(masks, "scratch", "dependency", small) ==
            scratchDependency,
        "scratch dependency lookup stays within its recipe");
  Check(!FindRecipeTexture(masks, "scratch", "dependency", large),
        "dependency lookup requires the active preparation resolution");

  RecipeTextureCache<int> ripples;
  ripples.emplace(RecipeTextureKey{"normal", "pulse", small}, normal);
  ripples.emplace(RecipeTextureKey{"scratch", "pulse", large}, scratch);
  Check(FindRecipeTexture(ripples, "normal", "pulse", small) == normal,
        "ripple preparation uses the same recipe scoped key");
  Check(LargestRecipeTexture(ripples, "normal", "pulse") == normal,
        "ripple inspection excludes another recipe's larger target");
  Check(LargestRecipeTexture(ripples, "scratch", "pulse") == scratch,
        "scratch ripple inspection returns its own target");
  Check(!LargestRecipeTexture(ripples, "normal", "missing"),
        "unknown ripple names do not match another local name");

  masks.emplace(RecipeTextureKey{"normal@mask", "name@1024", small}, normal);
  masks.emplace(RecipeTextureKey{"normal", "mask@name@1024", small}, scratch);
  Check(FindRecipeTexture(masks, "normal@mask", "name@1024", small) == normal &&
            FindRecipeTexture(masks, "normal", "mask@name@1024", small) ==
                scratch,
        "typed fields cannot collide through separator characters");
  std::string recipe = "owned-recipe";
  std::string name = "owned-name";
  const RecipeTextureKey owned{recipe, name, small};
  recipe.clear();
  name.clear();
  masks.emplace(owned, normal);
  Check(FindRecipeTexture(masks, "owned-recipe", "owned-name", small) == normal,
        "cache keys own their identity and local name strings");
  masks.emplace(RecipeTextureKey{"scratch", "mask", large}, nullptr);
  Check(LargestRecipeTexture(masks, "scratch", "mask") == scratch,
        "an incomplete larger entry does not hide a usable smaller target");
  masks.clear();
  Check(!FindRecipeTexture(masks, "normal", "mask", small) &&
            !LargestRecipeTexture(masks, "scratch", "mask"),
        "retiring geometry inputs removes every recipe cache entry");
  return test::Finish("planners recipe texture cache");
}
