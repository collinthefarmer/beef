// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "engine/LiveActor.h"
#include "planners/BindingPlan.h"
#include "planners/InstallProgram.h"
#include "recipe/Recipe.h"
#include "recipe/RecipeCatalog.h"

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects {
struct KeptBinding {
  BindingPlan plan;
  bool material = false;
  bool shell = false;
  std::optional<ShellSettings> shellSettings;
  [[nodiscard]] bool operator==(const KeptBinding &) const = default;
};

[[nodiscard]] LiveActor
StagedActorOf(const LiveActor &a_published,
              std::shared_ptr<const RecipeCatalog> a_catalog,
              ActorMatches a_matches);
[[nodiscard]] std::vector<KeptBinding>
KeptBindingsOf(const LiveActor &a_published);
[[nodiscard]] std::vector<KeptBinding>
KeptBindingsOf(const SurfacePlan &a_plan, const LiveActor &a_staged);
void KeepShellOwners(LiveActor &a_staged, const SurfacePlan &a_plan);
[[nodiscard]] std::expected<void, std::string>
HandOverBindings(LiveActor &a_published, LiveActor &a_staged);
}
