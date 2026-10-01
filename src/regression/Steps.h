// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "regression/Words.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Regression {
struct WaitFrames {
  std::uint32_t frames = 0;
};
struct Solo {
  std::string_view recipe;
};
struct RestoreSolo {};
struct Spawn {
  Role role = Role::kWearer;
};
struct Despawn {
  Role role = Role::kWearer;
};
struct Disable {
  Role role = Role::kWearer;
};
struct Enable {
  Role role = Role::kWearer;
};
struct Equip {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct Unequip {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct Remove {
  Role role = Role::kPlayer;
  Item item = Item::kFixture;
};
struct Apply {
  Role role = Role::kPlayer;
};
struct Retire {
  Role role = Role::kPlayer;
};
struct AwaitRendered {
  Role role = Role::kPlayer;
};
struct AwaitRetired {
  Role role = Role::kPlayer;
};
struct AwaitBaseline {
  Role role = Role::kPlayer;
};
struct ExpectEffect {
  Role role = Role::kPlayer;
};
struct HoldUntouched {
  Role role = Role::kControl;
  std::uint32_t frames = 0;
};
struct LeaveStart {};
struct ReturnToStart {};
struct SetCamera {
  Camera camera = Camera::kThirdPerson;
};
struct LoadDuring {
  QueuedWork work = QueuedWork::kNothing;
  std::string_view recipe;
};
struct ExpectAborted {};
struct ExpectCancelled {
  TrackedWork work = TrackedWork::kEdit;
};
struct ExpectSettled {
  TrackedWork work = TrackedWork::kEdit;
};
struct DeleteScratch {};
struct DuplicateToScratch {
  std::string_view from;
};
struct SetScratchOpacity {
  float value = 1.0f;
};
struct SaveScratch {};
struct ExpectScratch {
  float opacity = 1.0f;
};
struct SpawnCrowd {
  std::uint32_t count = 0;
};
struct AwaitCrowdRendered {};
struct DespawnCrowd {};
struct HoldWindow {
  std::string_view window;
  std::uint32_t seconds = 0;
};
struct BeginSession {
  Session session = Session::kPaint;
  std::string_view recipe;
};
struct AwaitSessionActive {
  Session session = Session::kPaint;
};
struct BeginWindow {
  std::string_view window;
};
struct EndWindow {
  std::string_view window;
};
struct AwaitIdle {};
using Step = std::variant<
    WaitFrames, Solo, RestoreSolo, Spawn, Despawn, Disable, Enable, Equip,
    Unequip, Remove, Apply, Retire, AwaitRendered, AwaitRetired, AwaitBaseline,
    ExpectEffect, HoldUntouched, LeaveStart, ReturnToStart, SetCamera,
    LoadDuring, ExpectAborted, ExpectCancelled, ExpectSettled, DeleteScratch,
    DuplicateToScratch, SetScratchOpacity, SaveScratch, ExpectScratch,
    SpawnCrowd, AwaitCrowdRendered, DespawnCrowd, HoldWindow, BeginWindow,
    EndWindow, BeginSession, AwaitSessionActive, AwaitIdle>;

struct Case {
  std::string_view name;
  std::span<const Step> body;
  std::span<const Step> cleanup;
};
using CaseList = std::vector<std::reference_wrapper<const Case>>;

inline constexpr std::string_view kScratchRecipe = "regression-scratch";
inline constexpr std::uint32_t kMaxCrowd = 64;

[[nodiscard]] std::string StepLabel(const Step &a_step);
[[nodiscard]] std::span<const Case> Catalog();
[[nodiscard]] const Case *FindCase(std::string_view a_name);
}
