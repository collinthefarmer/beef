#include "diagnostics/Trace.h"
#include "test_support.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

using namespace BetterEnchantmentEffects;
using test::Check;

int main() {
  Trace::Safely([] { throw 42; });
  Check(nlohmann::json::parse(Trace::Get().Recent().back())["event"] ==
            "capture_failure",
        "diagnostic formatting failures are contained and recorded");
  bool captured = false;
  Trace::Get().Enable(false);
  Trace::Safely([&] { captured = true; });
  Check(!captured, "disabled tracing does not evaluate diagnostic capture");
  Trace::Get().Enable(true);
  Trace::Recorder recorder;
  recorder.Record(Trace::Event::kCommand, {7, 3},
                  {{"reason", "quoted \"command\"\nnext line"}});
  const auto event = nlohmann::json::parse(recorder.Recent().back());
  Check(event["command"] == 7 && event["session"] == 3,
        "serialized command keeps its session");
  Check(event["fields"]["reason"] == "quoted \"command\"\nnext line",
        "JSONL preserves escaping without splitting an event");
  recorder.Record(Trace::Event::kRecipe, {},
                  {{"content", std::string(3000, 'x')}});
  Check(nlohmann::json::parse(recorder.Recent().back())["truncated"] == true,
        "oversized fields are explicitly truncated");
  recorder.Enable(false);
  recorder.Record(Trace::Event::kCommand, {}, {});
  Check(recorder.Inspect().dropped == 1, "disabled events are counted");
  recorder.Enable(true);
  std::vector<std::thread> writers;
  for (int i = 0; i < 4; ++i) {
    writers.emplace_back([&] {
      for (int j = 0; j < 200; ++j) {
        recorder.Record(Trace::Event::kQueue, {}, {});
      }
    });
  }
  for (auto &writer : writers) {
    writer.join();
  }
  const auto recent = recorder.Recent();
  Check(recent.size() == 512,
        "recent history remains bounded under concurrent writes");
  std::uint64_t previous = 0;
  for (const auto &line : recent) {
    const auto next = nlohmann::json::parse(line)["seq"].get<std::uint64_t>();
    Check(next > previous, "file and ring sequence order is monotonic");
    previous = next;
  }
  const auto outer = Trace::Command("outer");
  {
    const Trace::Scope scope{outer};
    const auto inner = Trace::Command("inner");
    {
      const Trace::Scope nested{inner};
      Check(Trace::Current().command == inner.command,
            "nested command is active");
      std::thread other{[] {
        Check(Trace::Current().command == 0, "command context is thread local");
      }};
      other.join();
    }
    Check(Trace::Current().command == outer.command,
          "nested scope restores its parent");
  }
  Check(Trace::Current().command == 0, "scope exits without leaking a command");
  {
    const Trace::Scope oldSession{{0, 8}};
    [[maybe_unused]] const auto advanced = Trace::BeginSession();
    Check(Trace::Current().session == 8,
          "unscoped captured work keeps its old session after a load");
  }
  Check(Trace::Fingerprint("hello") == "A430D84680AABD0B",
        "fingerprint uses stable FNV-1a 64");
  const auto file =
      std::filesystem::temp_directory_path() /
      ("beef-trace-test-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()) +
       ".jsonl");
  const auto segment = [&](int a_index) {
    std::filesystem::path path = file;
    path.replace_extension();
    return path.concat("-" + std::to_string(a_index)).concat(".jsonl");
  };
  {
    Trace::Recorder bounded{1024, 2};
    Check(bounded.Open(file, "test"), "trace file opens");
    bounded.Record(Trace::Event::kStartup, {}, {{"build", "test-build"}});
    for (int i = 0; i < 100; ++i) {
      bounded.Record(Trace::Event::kTexture, {}, {});
    }
    const Trace::Status status = bounded.Inspect();
    Check(status.rotations >= 2 && status.segment == status.rotations + 1,
          "a full segment rotates into the next one");
    Check(status.dropped == 0, "rotation drops no events");
    Check(!std::filesystem::exists(file) &&
              !std::filesystem::exists(segment(status.segment - 2)),
          "segments before the previous one are deleted");
    Check(std::filesystem::exists(segment(status.segment - 1)) &&
              std::filesystem::exists(segment(status.segment)),
          "the previous and current segments are kept");
    Check(std::filesystem::file_size(segment(status.segment - 1)) <= 1024 &&
              std::filesystem::file_size(segment(status.segment)) <= 1024,
          "no segment exceeds the segment size");
    std::ifstream current{segment(status.segment)};
    std::string first;
    std::getline(current, first);
    const auto header = nlohmann::json::parse(first);
    Check(header["event"] == "rotated" &&
              header["fields"]["build"] == "test-build" &&
              header["fields"]["segment"] == status.segment,
          "each new segment opens with the startup identity");
    std::string last;
    std::string line;
    while (std::getline(current, line)) {
      last = line;
    }
    Check(nlohmann::json::parse(last)["seq"] == status.events,
          "the newest segment ends with the newest event");
    for (std::uint64_t i = 1; i <= status.segment; ++i) {
      std::filesystem::remove(i == 1 ? file : segment(static_cast<int>(i)));
    }
  }
  {
    test::WriteFile(file, "");
    Trace::Recorder next;
    Check(!next.Open(file, "second"), "existing evidence is never truncated");
  }
  std::filesystem::remove(file);
  return test::Finish("diagnostic trace");
}
