// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "orchestration/Agreement.h"
#include "orchestration/sim/Shell.h"

#include <map>
#include <optional>

namespace sim {
[[nodiscard]] inline bool Holds(const EngineActor &a_actor) {
  return a_actor.render || a_actor.resize || a_actor.awaiting ||
         a_actor.staged || a_actor.withoutRender;
}

[[nodiscard]] inline UnpublishedCount CountOf(const UnpublishedWork &a_work) {
  return {a_work.total, a_work.drawn};
}

[[nodiscard]] inline HeldResources HeldOf(const EngineActor &a_actor) {
  HeldResources held;
  if (a_actor.render) {
    held.render = HeldRender{a_actor.render->level, a_actor.render->holding};
    if (a_actor.render->firstDraw) {
      held.firstDraw = CountOf(*a_actor.render->firstDraw);
    }
  }
  if (a_actor.resize) {
    held.resize = HeldResize{a_actor.resize->to, CountOf(a_actor.resize->draw)};
  }
  if (a_actor.awaiting) {
    held.awaited = a_actor.awaiting->job;
  }
  if (a_actor.staged) {
    const EngineStage &staged = *a_actor.staged;
    held.restage = HeldRestage{};
    if (staged.awaiting) {
      held.restage->awaited = staged.awaiting->job;
    }
    if (staged.render && staged.render->firstDraw) {
      held.restage->draw = CountOf(*staged.render->firstDraw);
    }
  }
  return held;
}

[[nodiscard]] inline std::map<ActorId, HeldResources>
HeldOf(const Shell &a_shell) {
  std::map<ActorId, HeldResources> held;
  for (const auto &[id, actor] : a_shell.actors) {
    if (Holds(actor)) {
      held.emplace(id, HeldOf(actor));
    }
  }
  return held;
}
}
