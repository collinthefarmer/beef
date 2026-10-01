// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace BetterEnchantmentEffects::Regression {
enum class Outcome : std::uint8_t { kPass, kFail, kBlocked, kAborted };
inline constexpr std::size_t kOutcomeCount = 4;

enum class Role : std::uint8_t { kPlayer, kWearer, kControl };
inline constexpr std::size_t kRoleCount = 3;

enum class Item : std::uint8_t { kFixture, kPlainCuirass };
inline constexpr std::size_t kItemCount = 2;

enum class Camera : std::uint8_t { kFirstPerson, kThirdPerson };
inline constexpr std::size_t kCameraCount = 2;

enum class QueuedWork : std::uint8_t { kNothing, kApply, kEdit };
inline constexpr std::size_t kQueuedWorkCount = 3;
enum class Session : std::uint8_t { kGesture, kPaint };
inline constexpr std::size_t kSessionCount = 2;
enum class TrackedWork : std::uint8_t { kEdit, kGesture };
inline constexpr std::size_t kTrackedWorkCount = 2;

enum class WorkOutcome : std::uint8_t {
  kNone,
  kPending,
  kApplied,
  kCancelledByLoad,
  kFailed
};
inline constexpr std::size_t kWorkOutcomeCount = 5;

inline constexpr Named<Outcome> kOutcomes[]{{Outcome::kPass, "PASS"},
                                            {Outcome::kFail, "FAIL"},
                                            {Outcome::kBlocked, "BLOCKED"},
                                            {Outcome::kAborted, "ABORTED"}};
static_assert(Complete(kOutcomes, kOutcomeCount));

inline constexpr Named<Role> kRoles[]{{Role::kPlayer, "player"},
                                      {Role::kWearer, "wearer"},
                                      {Role::kControl, "control"}};
static_assert(Complete(kRoles, kRoleCount));

inline constexpr Named<Item> kItems[]{{Item::kFixture, "fixture"},
                                      {Item::kPlainCuirass, "plain-cuirass"}};
static_assert(Complete(kItems, kItemCount));

inline constexpr Named<Camera> kCameras[]{
    {Camera::kFirstPerson, "first-person"},
    {Camera::kThirdPerson, "third-person"}};
static_assert(Complete(kCameras, kCameraCount));

inline constexpr Named<QueuedWork> kQueuedWorks[]{
    {QueuedWork::kNothing, "nothing"},
    {QueuedWork::kApply, "apply"},
    {QueuedWork::kEdit, "edit"}};
static_assert(Complete(kQueuedWorks, kQueuedWorkCount));

inline constexpr Named<Session> kSessions[]{{Session::kGesture, "gesture"},
                                            {Session::kPaint, "paint"}};
static_assert(Complete(kSessions, kSessionCount));

inline constexpr Named<TrackedWork> kTrackedWorks[]{
    {TrackedWork::kEdit, "edit"}, {TrackedWork::kGesture, "gesture"}};
static_assert(Complete(kTrackedWorks, kTrackedWorkCount));

inline constexpr Named<WorkOutcome> kWorkOutcomePhrases[]{
    {WorkOutcome::kNone, "never recorded"},
    {WorkOutcome::kPending, "still pending"},
    {WorkOutcome::kApplied, "applied before the load"},
    {WorkOutcome::kCancelledByLoad, "cancelled by the load"},
    {WorkOutcome::kFailed, "ended for another reason"}};
static_assert(Complete(kWorkOutcomePhrases, kWorkOutcomeCount));

[[nodiscard]] constexpr std::string_view OutcomeName(Outcome a_outcome) {
  return NameOf(kOutcomes, a_outcome);
}

[[nodiscard]] constexpr std::string_view WordOf(QueuedWork a_work) {
  return NameOf(kQueuedWorks, a_work);
}

[[nodiscard]] constexpr std::string_view WordOf(Session a_session) {
  return NameOf(kSessions, a_session);
}

[[nodiscard]] constexpr std::string_view WordOf(TrackedWork a_work) {
  return NameOf(kTrackedWorks, a_work);
}

[[nodiscard]] constexpr std::size_t IndexOf(Role a_role) {
  return std::min(static_cast<std::size_t>(a_role), kRoleCount - 1);
}

[[nodiscard]] constexpr std::size_t IndexOf(Item a_item) {
  return std::min(static_cast<std::size_t>(a_item), kItemCount - 1);
}
}
