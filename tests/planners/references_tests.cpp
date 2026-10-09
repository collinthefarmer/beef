// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/References.h"
#include "test_support.h"

#include <map>
#include <memory>
#include <optional>
#include <type_traits>
#include <unordered_set>

using namespace BetterEnchantmentEffects;
using test::Check;

namespace {
struct Target {
  int size = 0;
};
struct Texture {};
struct Geometry {};
}

template <> struct BetterEnchantmentEffects::IdentityShape<Texture> {
  using type = NamedTexture;
};
template <> struct BetterEnchantmentEffects::IdentityShape<Geometry> {
  using type = MeshInstall;
};

namespace {

static_assert(!std::is_copy_constructible_v<Owned<Target>>);
static_assert(!std::is_copy_constructible_v<Lease<Target>>);
static_assert(!std::is_constructible_v<Lease<Target>, std::shared_ptr<Target>>);
static_assert(std::is_nothrow_move_constructible_v<Lease<Target>>);

void LeasesKeepTheValueUntilTheyEnd() {
  auto made = std::make_shared<Target>(4);
  const std::weak_ptr<Target> watch = made;
  Owned<Target> owner{std::move(made)};
  Lease<Target> lease = owner.Grant();
  Check(lease && lease.Get() == owner.Get(), "a lease names the owner's value");
  owner = Owned<Target>{};
  Check(!watch.expired() && lease.Get()->size == 4,
        "a lease keeps the value while it lasts");
  lease = Lease<Target>{};
  Check(watch.expired(), "the value ends with its last reference");
}

void TargetIdentitiesCompareAddressAndGeneration() {
  const Identity<Target> first{{0x1000, 1}};
  const Identity<Target> reused{{0x1000, 2}};
  Check(first == Identity<Target>{{0x1000, 1}},
        "equal address and generation are one identity");
  Check(first != reused && first < reused,
        "a reused address with a new generation is another identity");
  Check(std::hash<Identity<Target>>{}(first) ==
            std::hash<Identity<Target>>{}(Identity<Target>{{0x1000, 1}}),
        "equal identities hash equally");
}

void TextureIdentitiesIgnoreCaseAndNeedAName() {
  const std::optional<NamedTexture> lower =
      NamedTextureOf("textures/a.dds", 512, 512);
  const std::optional<NamedTexture> upper =
      NamedTextureOf("Textures/A.DDS", 512, 512);
  Check(lower && upper && *lower == *upper,
        "a texture name is compared without case");
  Check(lower != NamedTextureOf("textures/a.dds", 256, 256),
        "the extent is part of a texture identity");
  Check(!NamedTextureOf("", 512, 512), "a texture without a name has none");
  if (!lower || !upper) {
    return;
  }
  const std::unordered_set<Identity<Texture>> seen{Identity<Texture>{*lower}};
  Check(seen.contains(Identity<Texture>{*upper}),
        "texture identities work as hash keys");
}

void GeometryAndGraphIdentitiesOrder() {
  std::map<Identity<Geometry>, int> geometries;
  geometries[Identity<Geometry>{{"mesh", 1}}] = 1;
  geometries[Identity<Geometry>{{"mesh", 2}}] = 2;
  Check(geometries.size() == 2,
        "an install token separates geometries of one mesh");
  using Graph = Identity<RecipeGraph>;
  const Graph base{{"recipe", 3, std::nullopt}};
  const Graph varied{{"recipe", 3, "variant"}};
  Check(base != varied && base < varied, "a variant is another graph identity");
  Check(Graph{{"recipe", 4, std::nullopt}} != base,
        "a new revision is another graph identity");
  const std::unordered_set<Graph> graphs{base, varied};
  Check(graphs.size() == 2 && graphs.contains(Graph{{"recipe", 3, "variant"}}),
        "graph identities work as hash keys");
}
}

int main() {
  LeasesKeepTheValueUntilTheyEnd();
  TargetIdentitiesCompareAddressAndGeneration();
  TextureIdentitiesIgnoreCaseAndNeedAName();
  GeometryAndGraphIdentitiesOrder();
  return test::Finish("planners_references");
}
