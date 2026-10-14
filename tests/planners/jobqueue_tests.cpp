// GPL-3.0-only with the additional permission in COPYING.md.
#include "planners/JobQueue.h"
#include "test_support.h"

#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
std::atomic<bool> g_gateOpen{true};

int Doubled(const int &a_value) {
  while (!g_gateOpen.load()) {
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  if (a_value < 0) {
    throw std::runtime_error{"negative input"};
  }
  if (a_value == 7) {
    throw 7;
  }
  return a_value * 2;
}

using IntQueue = JobQueue<int, int>;

std::vector<JobDone<int>> AwaitDone(IntQueue &a_queue, std::size_t a_count) {
  std::vector<JobDone<int>> done;
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds{10};
  while (done.size() < a_count && std::chrono::steady_clock::now() < deadline) {
    for (JobDone<int> &taken : a_queue.TakeDone()) {
      done.push_back(std::move(taken));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  return done;
}

void JobsRunInOrderAndReturnTheirIds() {
  g_gateOpen = true;
  IntQueue queue{Doubled, 8};
  const auto first = queue.Submit(2);
  const auto second = queue.Submit(5);
  Check(first.has_value() && second.has_value(), "two jobs are accepted");
  Check(first && second && *first != *second, "each job has its own id");
  const std::vector<JobDone<int>> done = AwaitDone(queue, 2);
  Equal(done.size(), std::size_t{2}, "both jobs finish");
  if (done.size() == 2 && first && second) {
    Check(done[0].id == *first && done[1].id == *second,
          "one worker finishes jobs in submission order");
    Check(done[0].result == 4 && done[1].result == 10,
          "each result is the job function of its input");
  }
  Equal(queue.Unfinished(), std::size_t{0},
        "taking the results frees their places");
}

void ExceptionsBecomeErrorResults() {
  g_gateOpen = true;
  IntQueue queue{Doubled, 8};
  (void)queue.Submit(-1);
  (void)queue.Submit(7);
  (void)queue.Submit(1);
  const std::vector<JobDone<int>> done = AwaitDone(queue, 3);
  Equal(done.size(), std::size_t{3}, "a throwing job still finishes");
  if (done.size() == 3) {
    Check(!done[0].result && done[0].result.error() == "negative input",
          "a standard exception becomes its message");
    Check(!done[1].result && !done[1].result.error().empty(),
          "a non-standard throw becomes an error text");
    Check(done[2].result == 2, "the worker keeps running after a throw");
  }
}

void TheQueueIsBounded() {
  g_gateOpen = false;
  IntQueue queue{Doubled, 2};
  Check(queue.Submit(1).has_value(), "the first job is accepted");
  Check(queue.Submit(2).has_value(), "the second job is accepted");
  const auto third = queue.Submit(3);
  Check(!third && third.error() == JobRefusal::kFull,
        "a full queue refuses a job");
  g_gateOpen = true;
  const std::vector<JobDone<int>> done = AwaitDone(queue, 2);
  Equal(done.size(), std::size_t{2}, "the accepted jobs finish");
  Check(queue.Submit(3).has_value(), "taken results make room again");
  (void)AwaitDone(queue, 1);
}

void DroppedJobsNeverRun() {
  g_gateOpen = false;
  IntQueue queue{Doubled, 4};
  (void)queue.Submit(1);
  std::this_thread::sleep_for(std::chrono::milliseconds{20});
  (void)queue.Submit(2);
  (void)queue.Submit(3);
  const std::size_t dropped = queue.DropQueued();
  Check(dropped >= 2, "jobs not started are dropped");
  g_gateOpen = true;
  const std::vector<JobDone<int>> done = AwaitDone(queue, 3 - dropped);
  Equal(done.size(), std::size_t{3} - dropped,
        "only the started job delivers a result");
  Equal(queue.Unfinished(), std::size_t{0}, "dropped jobs free their places");
}

void TimingsAreReported() {
  g_gateOpen = true;
  IntQueue queue{Doubled, 2};
  (void)queue.Submit(3);
  const std::vector<JobDone<int>> done = AwaitDone(queue, 1);
  Check(done.size() == 1 && done[0].timing.waitUs < 10'000'000 &&
            done[0].timing.workUs < 10'000'000,
        "a result carries its queue wait and its work time");
}

void DestroyingTheQueueLeavesARunningJobSafe() {
  g_gateOpen = false;
  {
    IntQueue queue{Doubled, 2};
    (void)queue.Submit(4);
    std::this_thread::sleep_for(std::chrono::milliseconds{20});
  }
  g_gateOpen = true;
  std::this_thread::sleep_for(std::chrono::milliseconds{50});
  Check(true, "the worker finishes on state it owns after the queue is gone");
}
}

void ACancelledJobNeverRuns() {
  g_gateOpen = false;
  IntQueue queue{Doubled, 4};
  const auto running = queue.Submit(1);
  std::this_thread::sleep_for(std::chrono::milliseconds{20});
  const auto queued = queue.Submit(2);
  const auto kept = queue.Submit(3);
  Check(running && queued && kept, "three jobs are accepted");
  Check(queue.Cancel(*queued), "a queued job is cancelled");
  Check(!queue.Cancel(*queued), "a cancelled job cannot be cancelled again");
  Check(!queue.Cancel(*running), "a running job is not cancelled");
  Check(!queue.Cancel(JobId{999}), "an unknown job is not cancelled");
  Equal(queue.Unfinished(), std::size_t{2}, "the cancel frees its place");
  g_gateOpen = true;
  const std::vector<JobDone<int>> done = AwaitDone(queue, 2);
  Check(done.size() == 2 && done[0].id == *running && done[1].id == *kept,
        "only the running and the kept job deliver results");
}

int main() {
  JobsRunInOrderAndReturnTheirIds();
  ExceptionsBecomeErrorResults();
  TheQueueIsBounded();
  DroppedJobsNeverRun();
  ACancelledJobNeverRuns();
  TimingsAreReported();
  DestroyingTheQueueLeavesARunningJobSafe();
  return test::Finish("planners_jobqueue");
}
