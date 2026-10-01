// GPL-3.0-only with the additional permission in COPYING.md.
#include "regression/Steps.h"

#include "Core.h"

#include <format>
#include <string>
#include <variant>

namespace BetterEnchantmentEffects::Regression {
namespace {
std::string WithRole(std::string_view a_action, Role a_role) {
  return std::format("{} {}", a_action, NameOf(kRoles, a_role));
}

std::string WithArmor(std::string_view a_action, Role a_role, Item a_item) {
  return std::format("{} {} {}", a_action, NameOf(kRoles, a_role),
                     NameOf(kItems, a_item));
}
std::string Label(const WaitFrames &a_settle) {
  return std::format("settle {}", a_settle.frames);
}

std::string Label(const Solo &a_solo) {
  return std::format("solo {}", a_solo.recipe);
}

std::string Label(const RestoreSolo &) { return std::string{"restore-view"}; }

std::string Label(const Spawn &a_s) { return WithRole("spawn", a_s.role); }

std::string Label(const Despawn &a_s) { return WithRole("despawn", a_s.role); }

std::string Label(const Disable &a_s) { return WithRole("disable", a_s.role); }

std::string Label(const Enable &a_s) { return WithRole("enable", a_s.role); }

std::string Label(const Equip &a_s) {
  return WithArmor("equip", a_s.role, a_s.item);
}

std::string Label(const Unequip &a_s) {
  return WithArmor("unequip", a_s.role, a_s.item);
}

std::string Label(const Remove &a_s) {
  return WithArmor("remove", a_s.role, a_s.item);
}

std::string Label(const Apply &a_s) { return WithRole("apply", a_s.role); }

std::string Label(const Retire &a_s) { return WithRole("retire", a_s.role); }

std::string Label(const AwaitRendered &a_s) {
  return WithRole("await-rendered", a_s.role);
}

std::string Label(const AwaitRetired &a_s) {
  return WithRole("await-retired", a_s.role);
}

std::string Label(const AwaitBaseline &a_s) {
  return WithRole("await-baseline", a_s.role);
}

std::string Label(const ExpectEffect &a_s) {
  return WithRole("expect-effect", a_s.role);
}

std::string Label(const HoldUntouched &a_s) {
  return std::format("hold-untouched {} {}", NameOf(kRoles, a_s.role),
                     a_s.frames);
}

std::string Label(const LeaveStart &) { return std::string{"leave-cell"}; }

std::string Label(const ReturnToStart &) {
  return std::string{"return-to-start"};
}

std::string Label(const SetCamera &a_s) {
  return std::format("set-camera {}", NameOf(kCameras, a_s.camera));
}

std::string Label(const LoadDuring &a_s) {
  return std::format("load-during {}", WordOf(a_s.work));
}

std::string Label(const ExpectAborted &) {
  return std::string{"expect-aborted"};
}

std::string Label(const ExpectCancelled &a_s) {
  return std::format("expect-cancelled {}", WordOf(a_s.work));
}

std::string Label(const ExpectSettled &a_s) {
  return std::format("expect-settled {}", WordOf(a_s.work));
}

std::string Label(const DeleteScratch &) {
  return std::string{"delete-scratch"};
}

std::string Label(const DuplicateToScratch &a_s) {
  return std::format("duplicate-to-scratch {}", a_s.from);
}

std::string Label(const SetScratchOpacity &a_s) {
  return std::format("set-scratch-opacity {}", a_s.value);
}

std::string Label(const SaveScratch &) { return std::string{"save-scratch"}; }

std::string Label(const ExpectScratch &a_s) {
  return std::format("expect-scratch opacity {}", a_s.opacity);
}

std::string Label(const SpawnCrowd &a_s) {
  return std::format("spawn-crowd {}", a_s.count);
}

std::string Label(const AwaitCrowdRendered &) {
  return std::string{"await-crowd-rendered"};
}

std::string Label(const DespawnCrowd &) { return std::string{"despawn-crowd"}; }

std::string Label(const HoldWindow &a_s) {
  return std::format("hold {} {}s", a_s.window, a_s.seconds);
}

std::string Label(const BeginSession &a_s) {
  return std::format("begin-session {}", WordOf(a_s.session));
}

std::string Label(const BeginWindow &a_s) {
  return std::format("begin-window {}", a_s.window);
}

std::string Label(const EndWindow &a_s) {
  return std::format("end-window {}", a_s.window);
}

std::string Label(const AwaitSessionActive &a_s) {
  return std::format("await-session-active {}", WordOf(a_s.session));
}

std::string Label(const AwaitIdle &) { return std::string{"await-idle"}; }
}

std::string StepLabel(const Step &a_step) {
  return std::visit([](const auto &a_kind) { return Label(a_kind); }, a_step);
}
}
