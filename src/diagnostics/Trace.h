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
  bool enabled = true;
  bool fileFailed = false;
  bool limitReached = false;
};

class Recorder {
public:
  explicit Recorder(std::uint64_t a_byteLimit = 32 * 1024 * 1024);
  bool Open(const std::filesystem::path &a_path, std::string a_run);
  void Enable(bool a_enabled) noexcept;
  void Record(Event a_event, Context a_context,
              std::initializer_list<Field> a_fields) noexcept;
  [[nodiscard]] Status Inspect() const;
  [[nodiscard]] std::vector<std::string> Recent() const;

private:
  void Write(std::string_view a_line);

  mutable std::mutex lock_;
  std::ofstream file_;
  std::string run_;
  std::deque<std::string> recent_;
  Status status_;
  std::uint64_t byteLimit_;
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
void Page(std::string_view a_page, std::string a_selection);
[[nodiscard]] std::string Pointer(const void *a_pointer);
[[nodiscard]] std::string Fingerprint(std::string_view a_text);
}
