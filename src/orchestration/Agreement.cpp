// GPL-3.0-only with the additional permission in COPYING.md.
#include "orchestration/Agreement.h"

#include "Core.h"
#include "orchestration/Tick.h"

#include <cstdint>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects::Orchestration {
namespace {
using Findings = std::vector<std::string>;

std::string_view HoldingName(IntermediateHolding a_holding) noexcept {
  return a_holding == IntermediateHolding::kHeld ? "held" : "released";
}

std::string CountText(const UnpublishedCount &a_count) {
  return std::format("{}/{}", a_count.drawn, a_count.total);
}

UnpublishedCount CountOf(const UnpublishedDraw &a_draw) noexcept {
  return {a_draw.total, a_draw.drawn};
}

void NoRender(Findings &a_findings, const HeldResources &a_held) {
  if (a_held.render) {
    a_findings.emplace_back("a render the core does not expect");
  }
}

void RenderAt(Findings &a_findings, const HeldResources &a_held,
              ResolutionLevel a_level,
              std::optional<IntermediateHolding> a_holding) {
  if (!a_held.render) {
    a_findings.push_back(
        std::format("no render where the core expects {}", LevelName(a_level)));
    return;
  }
  if (a_held.render->level != a_level) {
    a_findings.push_back(std::format("a render at {} where the core expects {}",
                                     LevelName(a_held.render->level),
                                     LevelName(a_level)));
  }
  if (a_holding && a_held.render->holding != *a_holding) {
    a_findings.push_back(std::format(
        "intermediates {} where the core expects {}",
        HoldingName(a_held.render->holding), HoldingName(*a_holding)));
  }
}

void NoFirstDraw(Findings &a_findings, const HeldResources &a_held) {
  if (a_held.firstDraw) {
    a_findings.emplace_back("a first draw the core does not expect");
  }
}

void FirstDrawOf(Findings &a_findings, const HeldResources &a_held,
                 const UnpublishedCount &a_count) {
  if (!a_held.firstDraw) {
    a_findings.emplace_back("no first draw where the core expects one");
    return;
  }
  if (*a_held.firstDraw != a_count) {
    a_findings.push_back(
        std::format("a first draw at {} where the core expects {}",
                    CountText(*a_held.firstDraw), CountText(a_count)));
  }
}

void NoResize(Findings &a_findings, const HeldResources &a_held) {
  if (a_held.resize) {
    a_findings.emplace_back("a resize the core does not expect");
  }
}

void ResizeOf(Findings &a_findings, const HeldResources &a_held,
              const HeldResize &a_resize) {
  if (!a_held.resize) {
    a_findings.emplace_back("no resize where the core expects one");
    return;
  }
  if (a_held.resize->to != a_resize.to) {
    a_findings.push_back(std::format("a resize to {} where the core expects {}",
                                     LevelName(a_held.resize->to),
                                     LevelName(a_resize.to)));
  }
  if (a_held.resize->draw != a_resize.draw) {
    a_findings.push_back(std::format("a resize at {} where the core expects {}",
                                     CountText(a_held.resize->draw),
                                     CountText(a_resize.draw)));
  }
}

void NoAwaited(Findings &a_findings, const HeldResources &a_held) {
  if (a_held.awaited) {
    a_findings.emplace_back("a plan job the core does not expect");
  }
}

void AwaitedOf(Findings &a_findings, const HeldResources &a_held,
               PlanJobId a_job) {
  if (a_held.awaited != a_job) {
    a_findings.push_back(std::format(
        "plan job {} where the core expects {}",
        a_held.awaited
            ? std::to_string(static_cast<std::uint64_t>(*a_held.awaited))
            : std::string{"none"},
        static_cast<std::uint64_t>(a_job)));
  }
}

Findings FindingsFor(const RenderPhase &a_phase, const HeldResources &a_held) {
  Findings findings;
  Match(
      a_phase,
      [&](const Empty &) {
        NoRender(findings, a_held);
        NoFirstDraw(findings, a_held);
        NoResize(findings, a_held);
        NoAwaited(findings, a_held);
      },
      [&](const Planning &a_planning) {
        NoRender(findings, a_held);
        NoFirstDraw(findings, a_held);
        NoResize(findings, a_held);
        AwaitedOf(findings, a_held, a_planning.job);
      },
      [&](const Drawing &a_drawing) {
        RenderAt(findings, a_held, a_drawing.level, std::nullopt);
        FirstDrawOf(findings, a_held, CountOf(a_drawing.draw));
        NoResize(findings, a_held);
        NoAwaited(findings, a_held);
      },
      [&](const Shown &a_shown) {
        RenderAt(findings, a_held, a_shown.level,
                 HoldingOf(a_shown.intermediates));
        NoFirstDraw(findings, a_held);
        NoResize(findings, a_held);
        NoAwaited(findings, a_held);
      },
      [&](const Queued &a_queued) {
        const Resize &resize = a_queued.resize;
        RenderAt(findings, a_held, resize.from, resize.holding);
        NoFirstDraw(findings, a_held);
        NoResize(findings, a_held);
        NoAwaited(findings, a_held);
      },
      [&](const Running &a_running) {
        const Resize &resize = a_running.resize;
        RenderAt(findings, a_held, resize.from, resize.holding);
        NoFirstDraw(findings, a_held);
        ResizeOf(findings, a_held,
                 HeldResize{resize.to, CountOf(a_running.draw)});
        NoAwaited(findings, a_held);
      });
  return findings;
}

const InstalledRender *RenderOf(const World &a_world, ActorId a_actor) {
  const auto found = a_world.actors.find(a_actor);
  if (found == a_world.actors.end() || !found->second.render) {
    return nullptr;
  }
  return &*found->second.render;
}

bool AbandonsItsResize(const CommandResult &a_result) {
  const ActorEvent *event = Get<ActorEvent>(a_result);
  return event && Is<ResizeNotSwapped>(event->event);
}

void Append(std::vector<Disagreement> &a_into, ActorId a_actor,
            Findings a_findings) {
  for (std::string &what : a_findings) {
    a_into.push_back({a_actor, std::move(what)});
  }
}
}

std::vector<Disagreement>
Disagreements(const World &a_world,
              const std::map<ActorId, HeldResources> &a_held) {
  std::vector<Disagreement> disagreements;
  for (const auto &[actor, held] : a_held) {
    if (!RenderOf(a_world, actor)) {
      disagreements.push_back({actor, "resources the core does not track"});
    }
  }
  for (const auto &[actor, record] : a_world.actors) {
    if (!record.render) {
      continue;
    }
    const auto found = a_held.find(actor);
    if (found == a_held.end()) {
      if (!Is<Empty>(record.render->phase)) {
        Append(disagreements, actor,
               FindingsFor(record.render->phase, HeldResources{}));
      }
      continue;
    }
    Append(disagreements, actor,
           FindingsFor(record.render->phase, found->second));
  }
  return disagreements;
}

World SettledWorld(const World &a_world, const TickFacts &a_facts) {
  TickFacts settled = a_facts;
  settled.jobs.clear();
  std::erase_if(settled.results, AbandonsItsResize);
  TickOutput synced = RecordSyncResults(a_world, settled);
  return RecordRenderResults(std::move(synced.world), settled).world;
}
}
