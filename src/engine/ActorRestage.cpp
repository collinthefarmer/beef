// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/ActorRestage.h"

#include "Core.h"
#include "engine/ActorResize.h"
#include "engine/InstallStages.h"

#include <cstddef>
#include <functional>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
LiveGeometry StagedGeometryOf(const LiveGeometry &a_published);
LivePiece StagedPieceOf(const LivePiece &a_published);
std::size_t GeometryCountOf(const LiveActor &a_state) noexcept;
std::vector<std::reference_wrapper<LiveGeometry>>
GeometriesOf(LiveActor &a_state);
std::optional<ShellSettings>
ShellSettingsOf(const LiveActor &a_state, std::optional<std::size_t> a_owner);
bool SameGeometry(const LiveGeometry &a_published,
                  const LiveGeometry &a_staged) noexcept;
}

LiveActor StagedActorOf(const LiveActor &a_published,
                        std::shared_ptr<const RecipeCatalog> a_catalog,
                        ActorMatches a_matches) {
  LiveActor staged;
  staged.actor = a_published.actor;
  staged.catalog = std::move(a_catalog);
  staged.matches = std::move(a_matches);
  staged.pieces = Map(a_published.pieces, StagedPieceOf);
  return staged;
}

std::vector<KeptBinding> KeptBindingsOf(const LiveActor &a_published) {
  std::vector<KeptBinding> kept;
  for (const LivePiece &piece : a_published.pieces) {
    for (const LiveGeometry &geometry : piece.geometries) {
      kept.push_back({geometry.binding, geometry.material != nullptr,
                      geometry.shell != nullptr,
                      ShellSettingsOf(a_published, geometry.shellOwner)});
    }
  }
  return kept;
}

std::vector<KeptBinding> KeptBindingsOf(const SurfacePlan &a_plan,
                                        const LiveActor &a_staged) {
  std::vector<KeptBinding> kept(GeometryCountOf(a_staged));
  for (const GeometryBinding &geometry : a_plan.geometries) {
    const std::size_t index = IndexOf(geometry.geometry);
    if (index >= kept.size()) {
      continue;
    }
    const bool shell = geometry.binding.shell && geometry.shellOwner;
    const std::optional<std::size_t> owner =
        shell ? std::optional<std::size_t>{IndexOf(*geometry.shellOwner)}
              : std::nullopt;
    kept[index] = {geometry.binding, geometry.binding.material, shell,
                   ShellSettingsOf(a_staged, owner)};
  }
  return kept;
}

void KeepShellOwners(LiveActor &a_staged, const SurfacePlan &a_plan) {
  for (const GeometryBinding &geometry : a_plan.geometries) {
    if (!geometry.binding.shell || !geometry.shellOwner) {
      continue;
    }
    if (const std::optional<LocatedGeometry> located =
            LocateGeometry(a_staged, geometry.geometry)) {
      located->bound.shellOwner = IndexOf(*geometry.shellOwner);
    }
  }
}

std::expected<void, std::string> HandOverBindings(LiveActor &a_published,
                                                  LiveActor &a_staged) {
  const std::vector<std::reference_wrapper<LiveGeometry>> published =
      GeometriesOf(a_published);
  const std::vector<std::reference_wrapper<LiveGeometry>> staged =
      GeometriesOf(a_staged);
  if (published.size() != staged.size()) {
    return std::unexpected(std::string{"the geometry count changed"});
  }
  for (std::size_t i = 0; i < published.size(); ++i) {
    if (!SameGeometry(published[i], staged[i])) {
      return std::unexpected(
          std::string{"a geometry changed while the restage drew"});
    }
  }

  ConcealStackSlots(a_published);
  for (std::size_t i = 0; i < published.size(); ++i) {
    LiveGeometry &from = published[i];
    LiveGeometry &to = staged[i];
    to.material = std::move(from.material);
    to.shell = std::move(from.shell);
  }
  return {};
}

namespace {
LivePiece StagedPieceOf(const LivePiece &a_published) {
  LivePiece piece;
  piece.ref = a_published.ref;
  piece.model = a_published.model;
  piece.armor = a_published.armor;
  piece.addon = a_published.addon;
  piece.armorName = a_published.armorName;
  piece.enchantment = a_published.enchantment;
  piece.geometries = Map(a_published.geometries, StagedGeometryOf);
  return piece;
}

LiveGeometry StagedGeometryOf(const LiveGeometry &a_published) {
  LiveGeometry geometry;
  geometry.ref = a_published.ref;
  geometry.geometry = a_published.geometry;
  geometry.property = a_published.property;
  geometry.name = a_published.name;
  geometry.inputs = a_published.inputs;
  geometry.inputs.render.reset();
  geometry.lost = a_published.lost;
  return geometry;
}

std::size_t GeometryCountOf(const LiveActor &a_state) noexcept {
  std::size_t count = 0;
  for (const LivePiece &piece : a_state.pieces) {
    count += piece.geometries.size();
  }
  return count;
}

std::vector<std::reference_wrapper<LiveGeometry>>
GeometriesOf(LiveActor &a_state) {
  std::vector<std::reference_wrapper<LiveGeometry>> geometries;
  for (LivePiece &piece : a_state.pieces) {
    for (LiveGeometry &geometry : piece.geometries) {
      geometries.emplace_back(geometry);
    }
  }
  return geometries;
}

std::optional<ShellSettings>
ShellSettingsOf(const LiveActor &a_state, std::optional<std::size_t> a_owner) {
  if (!a_owner || *a_owner >= a_state.instances.size() ||
      !a_state.instances[*a_owner].recipe) {
    return std::nullopt;
  }
  return a_state.instances[*a_owner].recipe->shell;
}

bool SameGeometry(const LiveGeometry &a_published,
                  const LiveGeometry &a_staged) noexcept {
  return !a_published.lost && !a_staged.lost && a_published.geometry &&
         a_published.geometry == a_staged.geometry &&
         a_published.property == a_staged.property;
}
}
}
