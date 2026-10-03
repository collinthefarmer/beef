// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/Reconcile.h"

#include "Core.h"

#include <algorithm>
#include <utility>

namespace BetterEnchantmentEffects::Actors {
namespace {
bool GeometryKept(const ActorPlan &a_installed, const ActorPlan &a_plan,
                  const GeometryRef &a_ref) {
  const PlannedGeometry *installed = Find(a_installed, a_ref);
  const PlannedGeometry *planned = Find(a_plan, a_ref);
  return installed && planned && *installed == *planned;
}

bool PlacementKept(const ActorPlan &a_installed, const ActorPlan &a_plan,
                   const PlannedPlacement &a_installedPlacement) {
  const PlannedPlacement *planned = Find(a_plan, a_installedPlacement.ref);
  return planned && *planned == a_installedPlacement &&
         GeometryKept(a_installed, a_plan, a_installedPlacement.ref.geometry);
}

template <class Entry, class Ref>
void Erase(std::vector<Entry> &a_entries, const Ref &a_ref) {
  std::erase_if(a_entries,
                [&](const Entry &a_entry) { return a_entry.ref == a_ref; });
}

template <class Entry> void Put(std::vector<Entry> &a_entries, Entry a_entry) {
  Erase(a_entries, a_entry.ref);
  a_entries.push_back(std::move(a_entry));
}
}

std::vector<ActorCommand> Reconcile(const ActorPlan &a_installed,
                                    const ActorPlan &a_plan) {
  std::vector<ActorCommand> commands;
  bool placementsChanged = false;
  for (const PlannedPlacement &placement : a_installed.placements) {
    if (!PlacementKept(a_installed, a_plan, placement)) {
      commands.emplace_back(RetirePlacement{placement.ref});
      placementsChanged = true;
    }
  }
  for (const PlannedGeometry &geometry : a_installed.geometries) {
    if (!GeometryKept(a_installed, a_plan, geometry.ref)) {
      commands.emplace_back(RetireGeometry{geometry.ref});
    }
  }
  for (const PlannedInstance &instance : a_installed.instances) {
    if (!Find(a_plan, instance.ref)) {
      commands.emplace_back(RetireInstance{instance.ref});
    }
  }
  for (const PlannedInstance &instance : a_plan.instances) {
    if (!Find(a_installed, instance.ref)) {
      commands.emplace_back(InstallInstance{instance});
    }
  }
  for (const PlannedGeometry &geometry : a_plan.geometries) {
    if (!GeometryKept(a_installed, a_plan, geometry.ref)) {
      commands.emplace_back(InstallGeometry{geometry});
    }
  }
  for (const PlannedPlacement &placement : a_plan.placements) {
    const PlannedPlacement *installed = Find(a_installed, placement.ref);
    if (!installed || !PlacementKept(a_installed, a_plan, *installed)) {
      commands.emplace_back(InstallPlacement{placement});
      placementsChanged = true;
    }
  }
  if (placementsChanged && !a_plan.placements.empty()) {
    commands.emplace_back(RebuildRender{});
  }
  return commands;
}

ActorPlan AfterCommand(ActorPlan a_installed, const ActorCommand &a_command) {
  std::visit(Overloaded{
                 [&](const InstallInstance &a_c) {
                   Put(a_installed.instances, a_c.instance);
                 },
                 [&](const RetireInstance &a_c) {
                   Erase(a_installed.instances, a_c.ref);
                 },
                 [&](const InstallGeometry &a_c) {
                   Put(a_installed.geometries, a_c.geometry);
                 },
                 [&](const RetireGeometry &a_c) {
                   Erase(a_installed.geometries, a_c.ref);
                 },
                 [&](const InstallPlacement &a_c) {
                   Put(a_installed.placements, a_c.placement);
                 },
                 [&](const RetirePlacement &a_c) {
                   Erase(a_installed.placements, a_c.ref);
                 },
                 [](const RebuildRender &) {},
             },
             a_command);
  return a_installed;
}

ActorPlan AfterCommands(ActorPlan a_installed,
                        std::span<const ActorCommand> a_commands) {
  for (const ActorCommand &command : a_commands) {
    a_installed = AfterCommand(std::move(a_installed), command);
  }
  return a_installed;
}
}
