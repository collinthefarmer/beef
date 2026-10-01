// GPL-3.0-only with the additional permission in COPYING.md.
#include "diagnostics/Trace.h"

#include "Core.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <sstream>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

namespace BetterEnchantmentEffects::Trace {
namespace {
std::atomic<std::uint64_t> nextID{1};
std::atomic<std::uint64_t> session{0};
thread_local std::optional<Context> context;
constexpr Named<Event> kEventNames[]{
    {Event::kStartup, "startup"}, {Event::kSettings, "settings"},
    {Event::kRecipe, "recipe"},   {Event::kCommand, "command"},
    {Event::kPage, "page"},       {Event::kQueue, "queue"},
    {Event::kLoad, "load"},       {Event::kApplication, "application"},
    {Event::kRetire, "retire"},   {Event::kBinding, "binding"},
    {Event::kRestore, "restore"}, {Event::kTexture, "texture"},
    {Event::kShell, "shell"},     {Event::kPreview, "preview"},
    {Event::kMetrics, "metrics"}, {Event::kCaptureFailure, "capture_failure"},
};
}

Recorder::Recorder(std::uint64_t a_segmentBytes, std::uint64_t a_segmentsKept)
    : segmentBytes_(a_segmentBytes),
      segmentsKept_(std::max<std::uint64_t>(2, a_segmentsKept)) {}

bool Recorder::Open(const std::filesystem::path &a_path, std::string a_run) {
  std::scoped_lock lock{lock_};
  if (file_.is_open()) {
    return false;
  }
  firstSegment_ = a_path;
  run_ = std::move(a_run);
  return OpenSegment(1);
}

std::filesystem::path Recorder::SegmentPath(std::uint64_t a_segment) const {
  if (a_segment <= 1) {
    return firstSegment_;
  }
  std::filesystem::path path = firstSegment_;
  const std::string extension = path.extension().string();
  path.replace_extension();
  return path.concat(std::format("-{}", a_segment)).concat(extension);
}

bool Recorder::OpenSegment(std::uint64_t a_segment) {
  const std::filesystem::path path = SegmentPath(a_segment);
  std::error_code ec;
  if (std::filesystem::exists(path, ec)) {
    status_.fileFailed = true;
    return false;
  }
  file_.open(path, std::ios::out | std::ios::binary);
  status_.fileFailed = !file_;
  status_.segment = a_segment;
  status_.bytes = 0;
  return !status_.fileFailed;
}

void Recorder::Rotate() {
  file_.close();
  const std::uint64_t next = status_.segment + 1;
  if (!OpenSegment(next)) {
    return;
  }
  ++status_.rotations;
  if (const std::uint64_t oldest = next - segmentsKept_;
      next > segmentsKept_ && oldest > 1) {
    std::error_code ec;
    std::filesystem::remove(SegmentPath(oldest), ec);
  }
  nlohmann::json fields =
      identity_.empty() ? nlohmann::json::object()
                        : nlohmann::json::parse(identity_, nullptr, false);
  if (!fields.is_object()) {
    fields = nlohmann::json::object();
  }
  fields["segment"] = next;
  fields["previous"] = SegmentPath(next - 1).filename().string();
  fields["dropped"] = status_.dropped;
  const nlohmann::json event{{"schema", 1},
                             {"run", run_},
                             {"seq", status_.events},
                             {"event", "rotated"},
                             {"fields", std::move(fields)}};
  const std::string line = event.dump() + "\n";
  file_ << line;
  file_.flush();
  status_.bytes += line.size();
  status_.fileFailed = !file_;
}

void Recorder::Enable(bool a_enabled) noexcept {
  std::scoped_lock lock{lock_};
  status_.enabled = a_enabled;
}

void Recorder::Record(Event a_event, Context a_context,
                      std::initializer_list<Field> a_fields) noexcept {
  try {
    std::scoped_lock lock{lock_};
    if (!status_.enabled) {
      ++status_.dropped;
      return;
    }
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    std::ostringstream thread;
    thread << std::this_thread::get_id();
    nlohmann::json fields = nlohmann::json::object();
    std::size_t count = 0;
    bool truncated = false;
    for (const Field &field : a_fields) {
      if (++count > 64) {
        truncated = true;
        break;
      }
      truncated = truncated || field.value.size() > 2048;
      fields[std::string{field.name.substr(0, 64)}] =
          field.value.substr(0, 2048);
    }
    if (a_event == Event::kStartup) {
      identity_ = fields.dump();
    }
    nlohmann::json event{
        {"schema", 1},
        {"run", run_},
        {"seq", ++status_.events},
        {"unix_ms",
         std::chrono::duration_cast<std::chrono::milliseconds>(now).count()},
        {"thread", thread.str()},
        {"session", a_context.session},
        {"command", a_context.command},
        {"event", NameOf(kEventNames, a_event)},
        {"fields", std::move(fields)},
        {"truncated", truncated},
        {"dropped", status_.dropped}};
    std::string line =
        event.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (line.size() > 16384) {
      event["fields"] = {{"omitted", "event exceeds 16 KiB"}};
      event["truncated"] = true;
      line = event.dump();
    }
    if (recent_.size() >= 512) {
      recent_.pop_front();
    }
    recent_.push_back(line);
    Write(line);
  } catch (...) {
    std::scoped_lock lock{lock_};
    ++status_.dropped;
    status_.fileFailed = true;
  }
}

void Recorder::Write(std::string_view a_line) {
  if (!file_.is_open() || status_.fileFailed) {
    return;
  }
  if (status_.bytes + a_line.size() + 1 > segmentBytes_) {
    Rotate();
    if (status_.fileFailed) {
      ++status_.dropped;
      return;
    }
  }
  file_ << a_line << '\n';
  file_.flush();
  status_.bytes += a_line.size() + 1;
  status_.fileFailed = !file_;
}

Status Recorder::Inspect() const {
  std::scoped_lock lock{lock_};
  return status_;
}

std::filesystem::path Recorder::FirstSegment() const {
  std::scoped_lock lock{lock_};
  return firstSegment_;
}

std::vector<std::string> Recorder::Recent() const {
  std::scoped_lock lock{lock_};
  return {recent_.begin(), recent_.end()};
}

Recorder &Get() {
  static Recorder recorder;
  return recorder;
}

Context Current() noexcept {
  return context.value_or(Context{0, session.load(std::memory_order_relaxed)});
}

Scope::Scope(Context a_context) noexcept : previous_(context) {
  context = a_context;
}

Scope::~Scope() { context = previous_; }

std::uint64_t NextID() noexcept {
  return nextID.fetch_add(1, std::memory_order_relaxed);
}

std::uint64_t BeginSession() noexcept {
  return session.fetch_add(1, std::memory_order_relaxed) + 1;
}

Context Command(std::string_view a_reason) {
  const Context parent = Current();
  const Context next{NextID(), session.load(std::memory_order_relaxed)};
  const Scope scope{next};
  Emit(Event::kCommand, {{"reason", std::string{a_reason}},
                         {"parent", std::to_string(parent.command)}});
  return next;
}

void Emit(Event a_event, std::initializer_list<Field> a_fields) noexcept {
  Get().Record(a_event, Current(), a_fields);
}

void Page(std::string_view a_page, std::string a_selection) {
  static std::mutex lock;
  static std::string lastPage;
  static std::string lastSelection;
  std::scoped_lock held{lock};
  if (lastPage == a_page && lastSelection == a_selection) {
    return;
  }
  lastPage = a_page;
  lastSelection = std::move(a_selection);
  Emit(Event::kPage, {{"page", lastPage}, {"selection", lastSelection}});
}

std::string Pointer(const void *a_pointer) {
  return std::format("{:016X}", reinterpret_cast<std::uintptr_t>(a_pointer));
}

std::string Fingerprint(std::string_view a_text) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const unsigned char byte : a_text) {
    hash ^= byte;
    hash *= 1099511628211ULL;
  }
  return std::format("{:016X}", hash);
}
}
