// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "regression/Words.h"

#include <cstdint>
#include <string_view>
#include <variant>

namespace BetterEnchantmentEffects::Regression {
struct SoloRecipe {
  std::string_view recipe;
};
struct EndSolo {};
struct SpawnActor {
  Role role = Role::kWearer;
};
struct DespawnActor {
  Role role = Role::kWearer;
};
struct DisableActor {
  Role role = Role::kWearer;
};
struct EnableActor {
  Role role = Role::kWearer;
};
struct AddAndEquip {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct EquipCarried {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct UnequipArmor {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct RemoveArmor {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct SubmitApply {
  Role role = Role::kPlayer;
};
struct SubmitRetire {
  Role role = Role::kPlayer;
};
struct AbortRequest {};
struct TravelFromStart {};
struct TravelToStart {};
struct SwitchCamera {
  Camera camera = Camera::kThirdPerson;
};
struct StartDuplicate {
  std::string_view from;
  std::string_view to;
};
struct StartOpacityEdit {
  std::string_view recipe;
  float value = 1.0f;
};
struct StartSave {
  std::string_view recipe;
};
struct StartDelete {
  std::string_view recipe;
};
struct SpawnCrowdActors {
  std::uint32_t count = 0;
};
struct DespawnCrowdActors {};
struct OpenSession {
  Session session = Session::kPaint;
  std::string_view recipe;
};
struct LoadSaveWith {
  QueuedWork work = QueuedWork::kNothing;
  std::string_view recipe;
};
struct Quit {};
using Command =
    std::variant<SoloRecipe, EndSolo, SpawnActor, DespawnActor, DisableActor,
                 EnableActor, AddAndEquip, EquipCarried, UnequipArmor,
                 RemoveArmor, SubmitApply, SubmitRetire, AbortRequest,
                 TravelFromStart, TravelToStart, SwitchCamera, StartDuplicate,
                 StartOpacityEdit, StartSave, StartDelete, SpawnCrowdActors,
                 DespawnCrowdActors, OpenSession, LoadSaveWith, Quit>;
}
