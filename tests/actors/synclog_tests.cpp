// GPL-3.0-only with the additional permission in COPYING.md.
#include "actors/SyncLog.h"
#include "actors/fixtures.h"
#include "test_support.h"

#include <optional>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects::Actors;
using namespace actorfixtures;
using test::Check;
using test::Equal;

namespace {
ActorPlan Circuit() {
  return PlanBuilder{}.With(Instance("circuit"), Geo(0x1, "Body")).plan;
}

ActorState Installed() { return {Circuit(), Circuit(), {}, {}}; }

Transition Moved(ActorState a_state) {
  return {std::move(a_state), {RebuildRender{}}};
}

void KindsFollowTheTransition() {
  const std::optional<ActorState> none;
  const std::optional<ActorState> installed = Installed();
  Check(KindOf(installed, {std::nullopt, {}}, Reinstall::kIfChanged) ==
            SyncKind::kForgotten,
        "a transition without a state forgets");
  Check(KindOf(none, {std::nullopt, {}}, Reinstall::kIfChanged) ==
            SyncKind::kUnchanged,
        "an actor without a record has nothing to forget");
  Check(KindOf(installed, {Installed(), {}}, Reinstall::kIfChanged) ==
            SyncKind::kUnchanged,
        "a transition without commands changes nothing");
  Check(KindOf(none, Moved(Installed()), Reinstall::kIfChanged) ==
            SyncKind::kInstalled,
        "a first install is an install");
  Check(KindOf(installed, Moved({Circuit(), {}, {}, {}}),
               Reinstall::kIfChanged) == SyncKind::kEvicted,
        "losing everything installed is an eviction");
  Check(KindOf(installed, Moved(Installed()), Reinstall::kAlways) ==
            SyncKind::kReinstalled,
        "a demanded reinstall is a reinstall");
  ActorState claimed = Installed();
  claimed.claims.push_back({Geo(0x1, "Body"), ModelToken{1}});
  Check(KindOf(installed, Moved(claimed), Reinstall::kIfChanged) ==
            SyncKind::kClaimed,
        "a new claim is a claim");
  Check(KindOf(installed, Moved(Installed()), Reinstall::kIfChanged) ==
            SyncKind::kChanged,
        "other commands are a change");
}

void TheLogKeepsTwoSeconds() {
  std::vector<SyncRecord> log;
  log = Recorded(std::move(log), {1, "Old", SyncKind::kInstalled, 1000});
  log = Recorded(std::move(log), {2, "Kept", SyncKind::kChanged, 2500});
  log = Recorded(std::move(log), {3, "New", SyncKind::kChanged, 3100});
  Equal(log.size(), std::size_t{2}, "a record older than two seconds goes");
  for (std::uint32_t i = 0; i < 100; ++i) {
    log = Recorded(std::move(log), {i, "Many", SyncKind::kChanged, 3200});
  }
  Equal(log.size(), kMaxSyncRecords, "the log holds at most its limit");
}

void TheSummaryGroupsByKind() {
  const std::vector<SyncRecord> log{{1, "Bandit", SyncKind::kReinstalled, 1000},
                                    {2, "Mage", SyncKind::kReinstalled, 1500},
                                    {3, "Guard", SyncKind::kEvicted, 1900},
                                    {4, "Old", SyncKind::kInstalled, 100},
                                    {5, "Quiet", SyncKind::kUnchanged, 1900}};
  Equal(SyncSummary(log, 2000, 1000),
        std::string{"reinstalled 2 (Bandit, Mage); evicted 1 (Guard)"},
        "the summary groups the window's syncs by kind");
  Equal(SyncSummary(log, 9000, 1000), std::string{},
        "an old log summarises to nothing");
}
}

int main() {
  KindsFollowTheTransition();
  TheLogKeepsTwoSeconds();
  TheSummaryGroupsByKind();
  return test::Finish("actor sync log");
}
