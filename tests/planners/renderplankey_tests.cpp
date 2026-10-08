// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/RenderPlanCache.h"
#include "planners/RenderPlanKey.h"
#include "test_support.h"

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
Recipe MaterialRecipe() {
  Recipe recipe;
  recipe.sources = {{"surface", MaterialSource{MaterialChannel::kDiffuseRgb}}};
  recipe.masks = {{"mask", "dot(@surface,[0.2,0.3,0.5])"}};
  return recipe;
}

Recipe ItemRecipe() {
  Recipe recipe;
  recipe.sources = {{"item", PieceSource{PieceField::kItemHash}}};
  recipe.masks = {{"partCount", "1 + floor(@item * 7)"}};
  return recipe;
}

Recipe DistanceRecipe() {
  Recipe recipe;
  recipe.sources = {{"near", DistanceSource{"NPC Head [Head]"}}};
  return recipe;
}

ValueBindings ItemBindings(std::string a_item) {
  ValueBindings bindings;
  bindings.external = [a_item = std::move(a_item)](const ExternalSource &source)
      -> std::expected<std::string, std::string> {
    return Is<ItemHashInput>(source) ? a_item : std::string{"static"};
  };
  bindings.state = [](NodeId node) { return std::to_string(node); };
  return bindings;
}

StackKeyRequest Request(const RecipeGraph &a_graph, std::string a_item) {
  return StackKeyRequest{&a_graph,
                         0,
                         PlacementId{0},
                         GeometryId{0},
                         1,
                         SideRef{0},
                         "material",
                         std::string{"mesh:1"},
                         ItemBindings(std::move(a_item))};
}

std::optional<RenderPlanKey> KeyOf(const StackKeyRequest &a_request) {
  return RenderPlanKeyOf(std::span{&a_request, 1}, {});
}

void EqualRequestsGiveEqualKeys() {
  const RecipeGraph graph = RecipeGraph::Compile(MaterialRecipe());
  const std::optional<RenderPlanKey> first = KeyOf(Request(graph, "item:1"));
  const std::optional<RenderPlanKey> second = KeyOf(Request(graph, "item:1"));
  Check(first && second && *first == *second &&
            RenderPlanKeyHash{}(*first) == RenderPlanKeyHash{}(*second),
        "equal requests give equal keys and equal hashes");
}

void EachFieldSeparatesKeys() {
  const RecipeGraph graph = RecipeGraph::Compile(MaterialRecipe());
  const StackKeyRequest base = Request(graph, "item:1");
  const std::optional<RenderPlanKey> baseKey = KeyOf(base);
  StackKeyRequest placement = base;
  placement.placement = PlacementId{1};
  StackKeyRequest geometry = base;
  geometry.geometry = GeometryId{1};
  StackKeyRequest size = base;
  size.side = SideRef{1};
  StackKeyRequest material = base;
  material.material = "other material";
  StackKeyRequest mesh = base;
  mesh.meshHash = "mesh:2";
  for (const StackKeyRequest &changed :
       std::array{placement, geometry, size, material, mesh}) {
    const std::optional<RenderPlanKey> key = KeyOf(changed);
    Check(baseKey && key && *key != *baseKey,
          "a changed placement, geometry, size, material or mesh changes "
          "the key");
  }
}

void ItemHashSeparatesKeysOnlyWhenRead() {
  const RecipeGraph item = RecipeGraph::Compile(ItemRecipe());
  const std::optional<RenderPlanKey> first = KeyOf(Request(item, "item:1"));
  const std::optional<RenderPlanKey> second = KeyOf(Request(item, "item:2"));
  Check(first && second && *first != *second,
        "a graph that reads the item hash keys each item apart");
  const RecipeGraph plain = RecipeGraph::Compile(MaterialRecipe());
  const std::optional<RenderPlanKey> plainFirst =
      KeyOf(Request(plain, "item:1"));
  const std::optional<RenderPlanKey> plainSecond =
      KeyOf(Request(plain, "item:2"));
  Check(plainFirst && plainSecond && *plainFirst == *plainSecond,
        "a graph that does not read the item hash shares across items");
}

void UncacheableRequestsGiveNoKey() {
  const RecipeGraph distance = RecipeGraph::Compile(DistanceRecipe());
  Check(ReadsNodePositions(distance), "the distance recipe reads a node");
  Check(!KeyOf(Request(distance, "item:1")),
        "a graph that reads a node position gives no key");
  const RecipeGraph graph = RecipeGraph::Compile(MaterialRecipe());
  StackKeyRequest unhashed = Request(graph, "item:1");
  unhashed.meshHash.reset();
  Check(!KeyOf(unhashed), "a mesh without a hash gives no key");
  StackKeyRequest missing = Request(graph, "item:1");
  missing.graph = nullptr;
  Check(!KeyOf(missing), "a request without a graph gives no key");
  const RecipeGraph item = RecipeGraph::Compile(ItemRecipe());
  StackKeyRequest failing = Request(item, "item:1");
  failing.bindings.external =
      [](const ExternalSource &) -> std::expected<std::string, std::string> {
    return std::unexpected{std::string{"unbound"}};
  };
  Check(!KeyOf(failing), "an input whose identity fails gives no key");
}

void ChainsEnterTheKey() {
  const RecipeGraph graph = RecipeGraph::Compile(MaterialRecipe());
  const StackKeyRequest request = Request(graph, "item:1");
  const std::array chain{SlotChainRequest{GeometryId{0},
                                          Surface::kMaterial,
                                          Slot::kEmissive,
                                          {{PlacementId{0}, 0}}}};
  const std::array linked{SlotChainRequest{GeometryId{0},
                                           Surface::kMaterial,
                                           Slot::kEmissive,
                                           {{PlacementId{1}, 0}}}};
  const std::optional<RenderPlanKey> first =
      RenderPlanKeyOf(std::span{&request, 1}, chain);
  const std::optional<RenderPlanKey> second =
      RenderPlanKeyOf(std::span{&request, 1}, linked);
  Check(first && second && *first != *second,
        "a different chain link changes the key");
}

void GraphsNamedByTheKeyAreKept() {
  const auto named = std::make_shared<const RecipeGraph>(
      RecipeGraph::Compile(MaterialRecipe()));
  const auto other =
      std::make_shared<const RecipeGraph>(RecipeGraph::Compile(ItemRecipe()));
  const std::optional<RenderPlanKey> key = KeyOf(Request(*named, "item:1"));
  const std::array live{named, other, named};
  const std::vector<std::shared_ptr<const RecipeGraph>> kept =
      key ? GraphsNamedBy(*key, live)
          : std::vector<std::shared_ptr<const RecipeGraph>>{};
  Check(kept.size() == 1 && kept.front() == named,
        "only the graphs the key names are kept, once each");
}

void CacheEvictsTheLeastRecentlyUsed() {
  RenderPlanCache cache;
  const RecipeGraph graph = RecipeGraph::Compile(MaterialRecipe());
  std::vector<RenderPlanKey> keys;
  for (std::size_t i = 0; i < 65; ++i) {
    StackKeyRequest request = Request(graph, "item:1");
    request.placement = PlacementId{i};
    if (const std::optional<RenderPlanKey> key = KeyOf(request)) {
      keys.push_back(*key);
    }
  }
  if (keys.size() != 65) {
    Check(false, "the cache test builds 65 keys");
    return;
  }
  for (std::size_t i = 0; i < 64; ++i) {
    cache.Store(keys[i], RenderPlan{}, {});
  }
  Check(cache.Find(keys[0]) != nullptr, "a stored plan is found");
  cache.Store(keys[64], RenderPlan{}, {});
  test::Equal(cache.Size(), std::size_t{64}, "the cache holds 64 plans");
  Check(cache.Find(keys[0]) != nullptr && cache.Find(keys[1]) == nullptr,
        "the least recently used plan is evicted");
}

void CacheEntryKeepsItsGraphsAlive() {
  RenderPlanCache cache;
  auto graph = std::make_shared<const RecipeGraph>(
      RecipeGraph::Compile(MaterialRecipe()));
  const std::weak_ptr<const RecipeGraph> watched = graph;
  const std::optional<RenderPlanKey> key = KeyOf(Request(*graph, "item:1"));
  if (!key) {
    Check(false, "the graph test builds a key");
    return;
  }
  cache.Store(*key, RenderPlan{}, {graph});
  graph.reset();
  Check(!watched.expired(), "a cache entry keeps its graphs alive");
  cache.Clear();
  Check(watched.expired(), "clearing the cache releases its graphs");
}
}

int main() {
  EqualRequestsGiveEqualKeys();
  EachFieldSeparatesKeys();
  ItemHashSeparatesKeysOnlyWhenRead();
  UncacheableRequestsGiveNoKey();
  ChainsEnterTheKey();
  GraphsNamedByTheKeyAreKept();
  CacheEvictsTheLeastRecentlyUsed();
  CacheEntryKeepsItsGraphsAlive();
  return test::Finish("render plan key");
}
