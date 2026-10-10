// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "planners/Hold.h"
#include "planners/RenderBudget.h"
#include "planners/ResolutionLevel.h"
#include "planners/ResolutionResize.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects {
struct HeldIntermediates {
  Hold outOfView;
  [[nodiscard]] bool operator==(const HeldIntermediates &) const = default;
};

struct ReleasedIntermediates {
  [[nodiscard]] bool operator==(const ReleasedIntermediates &) const = default;
};

using Holding = std::variant<HeldIntermediates, ReleasedIntermediates>;

struct Live {
  ResolutionLevel level = ResolutionLevel::kFull;
  Holding holding;
  Hold smallOnScreen;
  std::optional<ResolutionLevel> stepUpBlockedFrom;
  std::optional<ResolutionLevel> failedLevel;
};

template <class Job> struct AwaitingPlan {
  Job job;
};

template <class Draw> struct FirstDrawing {
  ResolutionLevel level = ResolutionLevel::kFull;
  Draw draw;
  std::optional<ResolutionLevel> next;
};

struct ResizeQueued {};

template <class Work> struct ResizeRunning {
  std::uint64_t beganFrame = 0;
  Work work;
};

template <class Work> struct Resizing {
  ResolutionLevel from = ResolutionLevel::kFull;
  ResolutionLevel to = ResolutionLevel::kFull;
  IntermediateHolding holding = IntermediateHolding::kHeld;
  std::variant<ResizeQueued, ResizeRunning<Work>> stage;
};

template <class Job, class Draw, class Work>
using ActorPhase =
    std::variant<Live, AwaitingPlan<Job>, FirstDrawing<Draw>, Resizing<Work>>;

template <class Job> struct JobAwaited {
  Job job;
};

template <class Job> struct JobFailed {
  Job job;
  ResolutionLevel level = ResolutionLevel::kFull;
};

template <class Draw> struct Installed {
  ResolutionLevel level = ResolutionLevel::kFull;
  std::optional<Draw> draw;
};

struct FirstDrawEnded {};

struct LevelWanted {
  ResolutionLevel level = ResolutionLevel::kFull;
};

template <class Work> struct ResizeStarted {
  std::uint64_t frame = 0;
  Work work;
};

struct ResizeFailed {};
struct Swapped {};
struct SwapMismatch {};

struct Observed {
  bool inView = true;
  bool small = false;
  std::optional<ResolutionLevel> wanted;
  Millis now = 0;
  Millis hiddenReleaseDelay = 0;
};

struct ReleaseForBudget {};

template <class Job, class Draw, class Work>
using PhaseEvent =
    std::variant<JobAwaited<Job>, JobFailed<Job>, Installed<Draw>,
                 FirstDrawEnded, LevelWanted, ResizeStarted<Work>, ResizeFailed,
                 Swapped, SwapMismatch, Observed, ReleaseForBudget>;

struct SetAppliedLevel {
  ResolutionLevel level = ResolutionLevel::kFull;
  [[nodiscard]] bool operator==(const SetAppliedLevel &) const = default;
};

struct ReleaseHeldIntermediates {
  [[nodiscard]] bool
  operator==(const ReleaseHeldIntermediates &) const = default;
};

using PhaseEffect = std::variant<SetAppliedLevel, ReleaseHeldIntermediates>;

template <class Job, class Draw, class Work> struct PhaseStep {
  ActorPhase<Job, Draw, Work> phase;
  std::vector<PhaseEffect> effects;
};

[[nodiscard]] constexpr IntermediateHolding
HoldingKind(const Holding &a_holding) noexcept {
  return Is<ReleasedIntermediates>(a_holding) ? IntermediateHolding::kReleased
                                              : IntermediateHolding::kHeld;
}

[[nodiscard]] inline Holding HoldingFrom(IntermediateHolding a_holding) {
  if (a_holding == IntermediateHolding::kReleased) {
    return ReleasedIntermediates{};
  }
  return HeldIntermediates{};
}

[[nodiscard]] inline Live LiveAt(ResolutionLevel a_level, Holding a_holding,
                                 std::optional<ResolutionLevel> a_failed = {}) {
  return Live{a_level, std::move(a_holding), {}, std::nullopt, a_failed};
}

template <class Job, class Draw, class Work>
[[nodiscard]] PhaseStep<Job, Draw, Work>
Unchanged(ActorPhase<Job, Draw, Work> a_phase) {
  return {std::move(a_phase), {}};
}

template <class Job, class Draw, class Work>
[[nodiscard]] PhaseStep<Job, Draw, Work>
ResizeOrStay(ResolutionLevel a_from, ResolutionLevel a_to,
             IntermediateHolding a_holding) {
  if (a_from == a_to) {
    return {LiveAt(a_from, HoldingFrom(a_holding)), {}};
  }
  return {Resizing<Work>{a_from, a_to, a_holding, ResizeQueued{}}, {}};
}

template <class Job, class Draw, class Work>
[[nodiscard]] PhaseStep<Job, Draw, Work>
ObserveLive(Live a_live, const Observed &a_observed) {
  if (a_live.failedLevel != a_observed.wanted) {
    a_live.failedLevel.reset();
  }
  a_live.smallOnScreen =
      a_live.smallOnScreen.Update(a_observed.small, a_observed.now);
  if (a_observed.inView) {
    a_live.holding = HeldIntermediates{};
    return {std::move(a_live), {}};
  }
  HeldIntermediates *held = Get<HeldIntermediates>(a_live.holding);
  if (!held) {
    return {std::move(a_live), {}};
  }
  held->outOfView = held->outOfView.Update(true, a_observed.now);
  if (!held->outOfView.Due(a_observed.now, a_observed.hiddenReleaseDelay)) {
    return {std::move(a_live), {}};
  }
  a_live.holding = ReleasedIntermediates{};
  return {std::move(a_live), {ReleaseHeldIntermediates{}}};
}

template <class Job, class Draw, class Work>
[[nodiscard]] PhaseStep<Job, Draw, Work>
StepPhase(ActorPhase<Job, Draw, Work> a_phase,
          PhaseEvent<Job, Draw, Work> a_event) {
  using Next = PhaseStep<Job, Draw, Work>;
  return Match(
      std::move(a_event),
      [&](JobAwaited<Job> &&a_awaited) -> Next {
        return {AwaitingPlan<Job>{std::move(a_awaited.job)}, {}};
      },
      [&](JobFailed<Job> &&a_failed) -> Next {
        const AwaitingPlan<Job> *awaiting = Get<AwaitingPlan<Job>>(a_phase);
        if (!awaiting || !(awaiting->job == a_failed.job)) {
          return Unchanged(std::move(a_phase));
        }
        return {LiveAt(a_failed.level, HeldIntermediates{}),
                {SetAppliedLevel{a_failed.level}}};
      },
      [&](Installed<Draw> &&a_installed) -> Next {
        if (!a_installed.draw) {
          return {LiveAt(a_installed.level, HeldIntermediates{}),
                  {SetAppliedLevel{a_installed.level}}};
        }
        return {FirstDrawing<Draw>{a_installed.level,
                                   std::move(*a_installed.draw), std::nullopt},
                {SetAppliedLevel{a_installed.level}}};
      },
      [&](FirstDrawEnded &&) -> Next {
        FirstDrawing<Draw> *drawing = Get<FirstDrawing<Draw>>(a_phase);
        if (!drawing) {
          return Unchanged(std::move(a_phase));
        }
        return ResizeOrStay<Job, Draw, Work>(
            drawing->level, drawing->next.value_or(drawing->level),
            IntermediateHolding::kHeld);
      },
      [&](LevelWanted &&a_wanted) -> Next {
        if (FirstDrawing<Draw> *drawing = Get<FirstDrawing<Draw>>(a_phase)) {
          drawing->next = a_wanted.level;
          return Unchanged(std::move(a_phase));
        }
        if (Live *live = Get<Live>(a_phase)) {
          if (live->failedLevel == a_wanted.level) {
            return Unchanged(std::move(a_phase));
          }
          return ResizeOrStay<Job, Draw, Work>(live->level, a_wanted.level,
                                               HoldingKind(live->holding));
        }
        if (Resizing<Work> *resizing = Get<Resizing<Work>>(a_phase)) {
          if (resizing->to == a_wanted.level) {
            return Unchanged(std::move(a_phase));
          }
          return ResizeOrStay<Job, Draw, Work>(resizing->from, a_wanted.level,
                                               resizing->holding);
        }
        return Unchanged(std::move(a_phase));
      },
      [&](ResizeStarted<Work> &&a_started) -> Next {
        Resizing<Work> *resizing = Get<Resizing<Work>>(a_phase);
        if (!resizing || !Is<ResizeQueued>(resizing->stage)) {
          return Unchanged(std::move(a_phase));
        }
        resizing->stage =
            ResizeRunning<Work>{a_started.frame, std::move(a_started.work)};
        return Unchanged(std::move(a_phase));
      },
      [&](ResizeFailed &&) -> Next {
        const Resizing<Work> *resizing = Get<Resizing<Work>>(a_phase);
        if (!resizing || !Is<ResizeQueued>(resizing->stage)) {
          return Unchanged(std::move(a_phase));
        }
        return {LiveAt(resizing->from, HoldingFrom(resizing->holding),
                       resizing->to),
                {}};
      },
      [&](Swapped &&) -> Next {
        const Resizing<Work> *resizing = Get<Resizing<Work>>(a_phase);
        if (!resizing || !Is<ResizeRunning<Work>>(resizing->stage)) {
          return Unchanged(std::move(a_phase));
        }
        const ResolutionLevel to = resizing->to;
        return {LiveAt(to, HeldIntermediates{}), {SetAppliedLevel{to}}};
      },
      [&](SwapMismatch &&) -> Next {
        Resizing<Work> *resizing = Get<Resizing<Work>>(a_phase);
        if (!resizing || !Is<ResizeRunning<Work>>(resizing->stage)) {
          return Unchanged(std::move(a_phase));
        }
        resizing->stage = ResizeQueued{};
        return Unchanged(std::move(a_phase));
      },
      [&](Observed &&a_observed) -> Next {
        Live *live = Get<Live>(a_phase);
        if (!live) {
          return Unchanged(std::move(a_phase));
        }
        return ObserveLive<Job, Draw, Work>(std::move(*live), a_observed);
      },
      [&](ReleaseForBudget &&) -> Next {
        Live *live = Get<Live>(a_phase);
        if (!live || !Is<HeldIntermediates>(live->holding)) {
          return Unchanged(std::move(a_phase));
        }
        live->holding = ReleasedIntermediates{};
        return {std::move(a_phase), {ReleaseHeldIntermediates{}}};
      });
}

template <class Job, class Draw, class Work>
[[nodiscard]] std::optional<ResolutionLevel>
TargetLevel(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  return Match(
      a_phase,
      [](const Live &a_live) -> std::optional<ResolutionLevel> {
        return a_live.level;
      },
      [](const AwaitingPlan<Job> &) -> std::optional<ResolutionLevel> {
        return std::nullopt;
      },
      [](const FirstDrawing<Draw> &a_drawing)
          -> std::optional<ResolutionLevel> {
        return a_drawing.next.value_or(a_drawing.level);
      },
      [](const Resizing<Work> &a_resizing) -> std::optional<ResolutionLevel> {
        return a_resizing.to;
      });
}

template <class Job, class Draw, class Work>
[[nodiscard]] std::optional<ResolutionLevel>
AppliedLevel(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  return Match(
      a_phase,
      [](const Live &a_live) -> std::optional<ResolutionLevel> {
        return a_live.level;
      },
      [](const AwaitingPlan<Job> &) -> std::optional<ResolutionLevel> {
        return std::nullopt;
      },
      [](const FirstDrawing<Draw> &a_drawing)
          -> std::optional<ResolutionLevel> { return a_drawing.level; },
      [](const Resizing<Work> &a_resizing) -> std::optional<ResolutionLevel> {
        return a_resizing.from;
      });
}

template <class Job, class Draw, class Work>
[[nodiscard]] IntermediateHolding
HoldingOf(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  if (const Live *live = Get<Live>(a_phase)) {
    return HoldingKind(live->holding);
  }
  if (const Resizing<Work> *resizing = Get<Resizing<Work>>(a_phase)) {
    return resizing->holding;
  }
  return IntermediateHolding::kHeld;
}

template <class Job, class Draw, class Work>
[[nodiscard]] bool
MayRelease(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  const Live *live = Get<Live>(a_phase);
  return live && Is<HeldIntermediates>(live->holding);
}

template <class Job, class Draw, class Work>
[[nodiscard]] bool
BudgetBusy(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  return !Is<Live>(a_phase);
}

template <class Job, class Draw, class Work>
[[nodiscard]] bool
IsResizing(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  return Is<Resizing<Work>>(a_phase);
}

template <class Job, class Draw, class Work>
[[nodiscard]] bool
ResizeQueuedIn(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  const Resizing<Work> *resizing = Get<Resizing<Work>>(a_phase);
  return resizing && Is<ResizeQueued>(resizing->stage);
}

template <class Job, class Draw, class Work>
[[nodiscard]] ResizeRunning<Work> *
RunningResize(ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  Resizing<Work> *resizing = Get<Resizing<Work>>(a_phase);
  return resizing ? Get<ResizeRunning<Work>>(resizing->stage) : nullptr;
}

template <class Job, class Draw, class Work>
[[nodiscard]] const ResizeRunning<Work> *
RunningResize(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  const Resizing<Work> *resizing = Get<Resizing<Work>>(a_phase);
  return resizing ? Get<ResizeRunning<Work>>(resizing->stage) : nullptr;
}

template <class Job, class Draw, class Work>
[[nodiscard]] std::optional<ResizeProgress>
ResizeProgressOf(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  const Resizing<Work> *resizing = Get<Resizing<Work>>(a_phase);
  if (!resizing) {
    return std::nullopt;
  }
  const ResizeRunning<Work> *running =
      Get<ResizeRunning<Work>>(resizing->stage);
  return ResizeProgress{resizing->from, resizing->to,
                        running ? running->beganFrame : 0};
}

template <class Job, class Draw, class Work>
[[nodiscard]] Draw *FirstDrawOf(ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  FirstDrawing<Draw> *drawing = Get<FirstDrawing<Draw>>(a_phase);
  return drawing ? &drawing->draw : nullptr;
}

template <class Job, class Draw, class Work>
[[nodiscard]] bool
IsFirstDrawing(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  return Is<FirstDrawing<Draw>>(a_phase);
}

template <class Job, class Draw, class Work>
[[nodiscard]] bool
DrawsHidden(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  return IsFirstDrawing(a_phase) || RunningResize(a_phase) != nullptr;
}

template <class Job, class Draw, class Work>
[[nodiscard]] std::optional<Job>
AwaitedJob(const ActorPhase<Job, Draw, Work> &a_phase) {
  const AwaitingPlan<Job> *awaiting = Get<AwaitingPlan<Job>>(a_phase);
  return awaiting ? std::optional<Job>{awaiting->job} : std::nullopt;
}

template <class Job, class Draw, class Work>
[[nodiscard]] bool StepDownDue(const ActorPhase<Job, Draw, Work> &a_phase,
                               Millis a_now, Millis a_delay) noexcept {
  const Live *live = Get<Live>(a_phase);
  return live && live->smallOnScreen.Due(a_now, a_delay);
}

template <class Job, class Draw, class Work>
[[nodiscard]] std::string_view
PhaseName(const ActorPhase<Job, Draw, Work> &a_phase) noexcept {
  return Match(
      a_phase, [](const Live &) { return std::string_view{"live"}; },
      [](const AwaitingPlan<Job> &) {
        return std::string_view{"awaiting_plan"};
      },
      [](const FirstDrawing<Draw> &) {
        return std::string_view{"first_drawing"};
      },
      [](const Resizing<Work> &a_resizing) {
        return Is<ResizeQueued>(a_resizing.stage)
                   ? std::string_view{"resize_queued"}
                   : std::string_view{"resizing"};
      });
}

template <class Job, class Draw, class Work>
[[nodiscard]] std::string_view
EventName(const PhaseEvent<Job, Draw, Work> &a_event) noexcept {
  return Match(
      a_event,
      [](const JobAwaited<Job> &) { return std::string_view{"job_awaited"}; },
      [](const JobFailed<Job> &) { return std::string_view{"job_failed"}; },
      [](const Installed<Draw> &) { return std::string_view{"installed"}; },
      [](const FirstDrawEnded &) {
        return std::string_view{"first_draw_ended"};
      },
      [](const LevelWanted &) { return std::string_view{"level_wanted"}; },
      [](const ResizeStarted<Work> &) {
        return std::string_view{"resize_started"};
      },
      [](const ResizeFailed &) { return std::string_view{"resize_failed"}; },
      [](const Swapped &) { return std::string_view{"swapped"}; },
      [](const SwapMismatch &) { return std::string_view{"swap_mismatch"}; },
      [](const Observed &) { return std::string_view{"observed"}; },
      [](const ReleaseForBudget &) {
        return std::string_view{"release_for_budget"};
      });
}
}
