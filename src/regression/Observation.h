// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "regression/Words.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>

namespace BetterEnchantmentEffects::Regression {
struct NoRequest {
  [[nodiscard]] bool operator==(const NoRequest &) const = default;
};
struct RequestPending {
  [[nodiscard]] bool operator==(const RequestPending &) const = default;
};
using RequestState = std::variant<NoRequest, RequestPending, Outcome>;

struct ItemFacts {
  bool equipped = false;
  bool carried = false;
};

struct ActorFacts {
  bool present = false;
  bool bodyArmorWorn = false;
  std::array<ItemFacts, kItemCount> armor{};
  bool live = false;
  std::uint64_t renderedAttempt = 0;
  std::uint32_t residue = 0;
  std::string application;
};

struct Activity {
  std::uint32_t pendingApplications = 0;
  bool paintActive = false;
  bool gestureActive = false;
  bool fileOperationPending = false;
  WorkOutcome editOutcome = WorkOutcome::kNone;
  WorkOutcome gestureOutcome = WorkOutcome::kNone;
  WorkOutcome fileOutcome = WorkOutcome::kNone;
  std::string detail;
};

struct CrowdFacts {
  std::uint32_t present = 0;
  std::uint32_t rendered = 0;
};

struct RecipeFacts {
  bool loaded = false;
  bool dirty = false;
  std::optional<float> firstOpacity;
};

struct Observation {
  std::array<ActorFacts, kRoleCount> actors{};
  Activity activity;
  RecipeFacts scratch;
  CrowdFacts crowd;
  std::uint64_t nowMs = 0;
  std::uint32_t loads = 0;
  std::array<bool, kItemCount> itemsLoaded{};
  bool npcEffects = false;
  Camera camera = Camera::kThirdPerson;
  bool awayFromStart = false;
  RequestState request = NoRequest{};
};
}
