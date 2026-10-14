// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/MaterialGrouper.h"
#include "test_support.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <thread>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
std::atomic<int> g_runs{0};
std::atomic<bool> g_release{true};

GroupingResult CountingWork(const GroupingJob &a_job) {
  while (!g_release.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  ++g_runs;
  if (a_job.key == 99) {
    return std::unexpected("the material has no groups");
  }
  auto grouping = std::make_shared<MaterialGrouping>();
  grouping->settings.smoothing = static_cast<std::uint32_t>(a_job.key);
  return grouping;
}

GroupingTicket TicketFor(std::uint64_t a_key) {
  GroupingTicket ticket;
  ticket.job.key = a_key;
  return ticket;
}

std::optional<GroupingResult> Settled(MaterialGrouper &a_grouper,
                                      const GroupingTicket &a_ticket) {
  for (int tries = 0; tries < 2000; ++tries) {
    if (auto result = a_grouper.Poll(a_ticket)) {
      return result;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return std::nullopt;
}
}

int main() {
  {
    MaterialGrouper grouper(&CountingWork, 4, 4);
    const auto first = Settled(grouper, TicketFor(1));
    Check(first && *first && (**first)->settings.smoothing == 1,
          "a polled ticket settles into its grouping");
    const int runs = g_runs.load();
    const auto again = Settled(grouper, TicketFor(1));
    Check(again && *again && **again == **first && g_runs.load() == runs,
          "the same key is served from the cache without another run");
    const auto failed = Settled(grouper, TicketFor(99));
    Check(failed && !*failed, "a failed grouping is reported, not waited on");
  }
  {
    g_release = false;
    MaterialGrouper grouper(&CountingWork, 1, 4);
    Check(!grouper.Poll(TicketFor(10)).has_value(), "the first job is queued");
    Check(!grouper.Poll(TicketFor(11)).has_value(),
          "a refused job is still waiting, not failed");
    g_release = true;
    const auto late = Settled(grouper, TicketFor(11));
    Check(late && *late && (**late)->settings.smoothing == 11,
          "a job refused while the queue was full runs once there is room");
  }
  {
    MaterialGrouper grouper(&CountingWork, 4, 1);
    const auto evicted = Settled(grouper, TicketFor(20));
    static_cast<void>(Settled(grouper, TicketFor(21)));
    const int runs = g_runs.load();
    const auto back = Settled(grouper, TicketFor(20));
    Check(
        evicted && back && *back && (**back)->settings.smoothing == 20 &&
            g_runs.load() == runs + 1,
        "an evicted result is computed again for the ticket that still waits");
  }
  return test::Finish("planners_materialgrouper");
}
