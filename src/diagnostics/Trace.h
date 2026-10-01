// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace BetterEnchantmentEffects::Trace {
enum class Event {
  kStartup,
  kSettings,
  kRecipe,
  kCommand,
  kPage,
  kQueue,
  kLoad,
  kApplication,
  kRetire,
  kBinding,
  kRestore,
  kTexture,
  kShell,
  kPreview,
  kMetrics,
  kCaptureFailure,
};

struct Field {
  std::string_view name;
  std::string value;
};

struct Context {
  std::uint64_t command = 0;
  std::uint64_t session = 0;
};

struct Status {
  std::uint64_t events = 0;
  std::uint64_t dropped = 0;
  std::uint64_t bytes = 0;
  std::uint64_t segment = 0;
  std::uint64_t rotations = 0;
  bool enabled = true;
  bool fileFailed = false;
};

inline constexpr std::uint64_t kTraceSegmentBytes = 32 * 1024 * 1024;
inline constexpr std::uint64_t kTraceSegmentsKept = 2;

class Recorder {
public:
  explicit Recorder(std::uint64_t a_segmentBytes = kTraceSegmentBytes,
                    std::uint64_t a_segmentsKept = kTraceSegmentsKept);
  bool Open(const std::filesystem::path &a_path, std::string a_run);
  void Enable(bool a_enabled) noexcept;
  void Record(Event a_event, Context a_context,
              std::initializer_list<Field> a_fields) noexcept;
  [[nodiscard]] Status Inspect() const;
  [[nodiscard]] std::filesystem::path FirstSegment() const;
  [[nodiscard]] std::vector<std::string> Recent() const;

private:
  void Write(std::string_view a_line);
  [[nodiscard]] std::filesystem::path
  SegmentPath(std::uint64_t a_segment) const;
  [[nodiscard]] bool OpenSegment(std::uint64_t a_segment);
  void Rotate();

  mutable std::mutex lock_;
  std::ofstream file_;
  std::filesystem::path firstSegment_;
  std::string run_;
  std::string identity_;
  std::deque<std::string> recent_;
  Status status_;
  std::uint64_t segmentBytes_;
  std::uint64_t segmentsKept_;
};

class Scope {
public:
  explicit Scope(Context a_context) noexcept;
  ~Scope();
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

private:
  std::optional<Context> previous_;
};

[[nodiscard]] Recorder &Get();
[[nodiscard]] Context Current() noexcept;
[[nodiscard]] Context Command(std::string_view a_reason);
[[nodiscard]] std::uint64_t NextID() noexcept;
[[nodiscard]] std::uint64_t BeginSession() noexcept;
void Emit(Event a_event, std::initializer_list<Field> a_fields) noexcept;
template <class Capture> void Safely(Capture &&a_capture) noexcept {
  try {
    if (!Get().Inspect().enabled)
      return;
    a_capture();
  } catch (...) {
    Emit(Event::kCaptureFailure, {});
  }
}
inline void EmitSafely(Event a_event,
                       std::initializer_list<Field> a_fields) noexcept {
  Safely([&] { Emit(a_event, a_fields); });
}
void Page(std::string_view a_page, std::string a_selection);
[[nodiscard]] std::string Pointer(const void *a_pointer);
[[nodiscard]] std::string Fingerprint(std::string_view a_text);
}
