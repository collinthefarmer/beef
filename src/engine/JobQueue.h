// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <expected>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
enum class JobId : std::uint64_t {};

enum class JobRefusal { kFull, kNoWorker };

struct JobTiming {
  std::uint64_t waitUs = 0;
  std::uint64_t workUs = 0;
};

template <class Output> struct JobDone {
  JobId id{};
  std::expected<Output, std::string> result;
  JobTiming timing;
};

template <class Input, class Output> class JobQueue {
public:
  using Work = Output (*)(const Input &);

  JobQueue(Work a_work, std::size_t a_capacity)
      : shared_(std::make_shared<Shared>(a_work, a_capacity)) {}
  ~JobQueue() {
    shared_->stop.store(true);
    shared_->wake.notify_all();
  }
  JobQueue(const JobQueue &) = delete;
  JobQueue &operator=(const JobQueue &) = delete;
  JobQueue(JobQueue &&) = delete;
  JobQueue &operator=(JobQueue &&) = delete;

  [[nodiscard]] std::expected<JobId, JobRefusal> Submit(Input a_input) {
    if (!shared_->work || shared_->stopped.load()) {
      return std::unexpected(JobRefusal::kNoWorker);
    }
    {
      const std::scoped_lock held{shared_->lock};
      if (shared_->unfinished >= shared_->capacity) {
        return std::unexpected(JobRefusal::kFull);
      }
    }
    if (!StartWorker()) {
      return std::unexpected(JobRefusal::kNoWorker);
    }
    const JobId id{++lastId_};
    {
      const std::scoped_lock held{shared_->lock};
      shared_->queued.push_back({id, std::move(a_input), Clock::now()});
      ++shared_->unfinished;
    }
    shared_->wake.notify_one();
    return id;
  }

  [[nodiscard]] std::vector<JobDone<Output>> TakeDone() {
    std::vector<JobDone<Output>> taken;
    const std::scoped_lock held{shared_->lock};
    if (shared_->stopped.load()) {
      if (shared_->running) {
        shared_->done.push_back(
            {*shared_->running,
             std::unexpected(std::string{"the job worker stopped"}),
             {}});
        shared_->running.reset();
      }
      for (Queued &stranded : shared_->queued) {
        shared_->done.push_back(
            {stranded.id,
             std::unexpected(std::string{"the job worker stopped"}),
             {}});
      }
      shared_->queued.clear();
    }
    taken.swap(shared_->done);
    shared_->unfinished -= std::min(shared_->unfinished, taken.size());
    return taken;
  }

  std::size_t DropQueued() {
    const std::scoped_lock held{shared_->lock};
    const std::size_t dropped = shared_->queued.size();
    shared_->queued.clear();
    shared_->unfinished -= std::min(shared_->unfinished, dropped);
    return dropped;
  }

  [[nodiscard]] std::size_t Unfinished() const {
    const std::scoped_lock held{shared_->lock};
    return shared_->unfinished;
  }

private:
  using Clock = std::chrono::steady_clock;
  enum class Worker { kNotStarted, kRunning, kFailed };
  struct Queued {
    JobId id{};
    Input input;
    Clock::time_point submitted;
  };
  struct Shared {
    Shared(Work a_work, std::size_t a_capacity)
        : work(a_work), capacity(a_capacity) {}
    const Work work;
    const std::size_t capacity;
    mutable std::mutex lock;
    std::condition_variable wake;
    std::deque<Queued> queued;
    std::vector<JobDone<Output>> done;
    std::size_t unfinished = 0;
    std::optional<JobId> running;
    std::atomic<bool> stop{false};
    std::atomic<bool> stopped{false};
  };

  static std::uint64_t MicrosBetween(Clock::time_point a_from,
                                     Clock::time_point a_to) noexcept {
    return a_to > a_from
               ? static_cast<std::uint64_t>(
                     std::chrono::duration_cast<std::chrono::microseconds>(
                         a_to - a_from)
                         .count())
               : 0;
  }

  static std::expected<Output, std::string> Attempt(Work a_work,
                                                    const Input &a_input) {
    try {
      return a_work(a_input);
    } catch (const std::exception &error) {
      return std::unexpected(std::string{error.what()});
    } catch (...) {
      return std::unexpected(std::string{"the job threw a non-standard value"});
    }
  }

  static std::optional<Queued> NextQueued(Shared &a_shared) {
    std::unique_lock held{a_shared.lock};
    a_shared.wake.wait(held, [&a_shared] {
      return a_shared.stop.load() || !a_shared.queued.empty();
    });
    if (a_shared.stop.load()) {
      return std::nullopt;
    }
    std::optional<Queued> next{std::move(a_shared.queued.front())};
    a_shared.queued.pop_front();
    a_shared.running = next->id;
    return next;
  }

  static void Finish(Shared &a_shared, Queued a_job) {
    const Clock::time_point started = Clock::now();
    std::expected<Output, std::string> result =
        Attempt(a_shared.work, a_job.input);
    const Clock::time_point finished = Clock::now();
    JobDone<Output> done{a_job.id,
                         std::move(result),
                         {MicrosBetween(a_job.submitted, started),
                          MicrosBetween(started, finished)}};
    const std::scoped_lock held{a_shared.lock};
    a_shared.done.push_back(std::move(done));
    a_shared.running.reset();
  }

  static void Run(const std::shared_ptr<Shared> a_shared) noexcept {
    while (!a_shared->stop.load()) {
      try {
        std::optional<Queued> next = NextQueued(*a_shared);
        if (!next) {
          return;
        }
        Finish(*a_shared, std::move(*next));
      } catch (...) {
        a_shared->stopped.store(true);
        return;
      }
    }
  }

  bool StartWorker() {
    if (worker_ == Worker::kNotStarted) {
      try {
        std::thread(Run, shared_).detach();
        worker_ = Worker::kRunning;
      } catch (const std::system_error &) {
        worker_ = Worker::kFailed;
      }
    }
    return worker_ == Worker::kRunning;
  }

  std::shared_ptr<Shared> shared_;
  Worker worker_ = Worker::kNotStarted;
  std::uint64_t lastId_ = 0;
};
}
