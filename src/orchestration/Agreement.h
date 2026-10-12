// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Facts.h"
#include "orchestration/RenderPhase.h"
#include "orchestration/World.h"
#include "planners/RenderBudget.h"
#include "planners/ResolutionLevel.h"

#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace BetterEnchantmentEffects::Orchestration {
struct UnpublishedCount {
  std::size_t total = 0;
  std::size_t drawn = 0;
  [[nodiscard]] bool operator==(const UnpublishedCount &) const = default;
};

struct HeldRender {
  ResolutionLevel level = ResolutionLevel::kFull;
  IntermediateHolding holding = IntermediateHolding::kHeld;
  [[nodiscard]] bool operator==(const HeldRender &) const = default;
};

struct HeldResize {
  ResolutionLevel to = ResolutionLevel::kFull;
  UnpublishedCount draw;
  [[nodiscard]] bool operator==(const HeldResize &) const = default;
};

struct HeldRestage {
  std::optional<PlanJobId> awaited;
  std::optional<UnpublishedCount> draw;
  [[nodiscard]] bool operator==(const HeldRestage &) const = default;
};

struct HeldResources {
  std::optional<HeldRender> render;
  std::optional<UnpublishedCount> firstDraw;
  std::optional<HeldResize> resize;
  std::optional<PlanJobId> awaited;
  std::optional<HeldRestage> restage;
  [[nodiscard]] bool operator==(const HeldResources &) const = default;
};

struct Disagreement {
  ActorId actor{};
  std::string what;
  [[nodiscard]] bool operator==(const Disagreement &) const = default;
};

[[nodiscard]] std::vector<Disagreement>
Disagreements(const World &a_world,
              const std::map<ActorId, HeldResources> &a_held);
[[nodiscard]] World SettledWorld(const World &a_world,
                                 const TickFacts &a_facts);
}
