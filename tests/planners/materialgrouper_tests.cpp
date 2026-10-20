// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/MaterialGrouper.h"
#include "test_support.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

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
  {
    MaterialGrouper grouper(&CountingWork, 4, 4);
    Check(grouper.Known(7) == nullptr, "an unseen identity is unknown");
    GroupingTicket ticket = TicketFor(30);
    ticket.job.identity = 7;
    const auto grouped = Settled(grouper, ticket);
    const std::shared_ptr<const MaterialGrouping> known = grouper.Known(7);
    Check(grouped && *grouped && known && known == **grouped,
          "a settled grouping is found again by the identity of its maps");
    GroupingTicket broken = TicketFor(99);
    broken.job.identity = 8;
    static_cast<void>(Settled(grouper, broken));
    Check(grouper.Known(8) == nullptr,
          "a failed grouping is not served by identity");
  }
  {
    MaterialPixels pixels{2, std::vector<std::uint8_t>(16, 128),
                          std::vector<std::uint8_t>(16, 64)};
    const MaterialSample sample = MaterialSampleOf(pixels);
    Check(sample.width == 2 && sample.texels.size() == 4 &&
              sample.texels.front().roughness == 128.0f / 255.0f &&
              sample.texels.front().diffuse.x == 64.0f / 255.0f,
          "raw map bytes convert to a sample on the worker");
    MaterialPixels changed = pixels;
    changed.diffuse[5] = 65;
    Check(GroupingKeyOf(pixels, 1, {}) == GroupingKeyOf(pixels, 1, {}) &&
              GroupingKeyOf(pixels, 1, {}) != GroupingKeyOf(changed, 1, {}) &&
              GroupingKeyOf(pixels, 1, {}) != GroupingKeyOf(pixels, 2, {}),
          "the content key follows every byte and the mesh");
    const std::vector<std::string> maps{"a:0:rmaos.dds", "b:0:diffuse.dds"};
    const std::vector<std::string> other{"a:0:rmaos.dds", "c:0:diffuse.dds"};
    Check(IdentityKeyOf(maps, 1, {}) != IdentityKeyOf(other, 1, {}) &&
              IdentityKeyOf(maps, 1, {}) != IdentityKeyOf(maps, 2, {}),
          "the identity key follows each map and the mesh");
  }
  return test::Finish("planners_materialgrouper");
}
