#include "engine/TextFile.h"
#include "test_support.h"

#include <filesystem>
#include <string>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
void ReadsWhatWasWritten(const std::filesystem::path &a_dir) {
  const std::filesystem::path path = a_dir / "recipe.json";
  Check(WriteText(path, "{\"format\": 1}"), "WriteText writes a new file");
  const auto text = ReadText(path);
  Check(text.has_value(), "ReadText reads a file that exists");
  Equal(text.value_or(""), std::string{"{\"format\": 1}"},
        "ReadText returns the file's bytes");
}

void EmptyFileIsEmptyText(const std::filesystem::path &a_dir) {
  const std::filesystem::path path = a_dir / "empty.json";
  Check(WriteText(path, ""), "WriteText writes an empty file");
  const auto text = ReadText(path);
  Check(text.has_value() && text->empty(),
        "an empty file reads as empty text, not as a failure");
}

void MissingPathIsNamed(const std::filesystem::path &a_dir) {
  const auto text = ReadText(a_dir / "absent.json");
  Check(!text.has_value(), "a missing file is a failure");
  Equal(text.error_or(""), std::string{"does not exist"},
        "a missing file is reported as missing");
}

void OversizeFileIsRefused(const std::filesystem::path &a_dir) {
  const std::filesystem::path path = a_dir / "oversize.json";
  Check(WriteText(path, std::string(kMaxTextFileBytes + 1, ' ')),
        "WriteText writes a file over the cap");
  const auto text = ReadText(path);
  Check(!text.has_value(), "a file over the cap is a failure");
  Check(text.error_or("").contains("larger than"),
        "a file over the cap is reported as too large");
  Check(WriteText(path, std::string(kMaxTextFileBytes, ' ')),
        "WriteText writes a file at the cap");
  Check(ReadText(path).has_value(), "a file exactly at the cap is read");
}

void WritingCreatesParentFolders(const std::filesystem::path &a_dir) {
  const std::filesystem::path path = a_dir / "nested" / "deeper" / "r.json";
  Check(WriteText(path, "x"), "WriteText creates missing parent folders");
  Equal(ReadText(path).value_or(""), std::string{"x"},
        "the nested file reads back");
}

void FailedWritePreservesTheFile(const std::filesystem::path &a_dir) {
  const std::filesystem::path path = a_dir / "kept.json";
  std::filesystem::path staged = path;
  staged += ".writing";
  Check(WriteText(path, "old"), "WriteText writes the file to keep");
  Check(!std::filesystem::exists(staged),
        "a completed write leaves no staging file");
  std::filesystem::create_directory(staged);
  Check(!WriteText(path, "new"), "a write that cannot stage reports failure");
  Equal(ReadText(path).value_or(""), std::string{"old"},
        "a failed write preserves the previous file");
  std::error_code ec;
  std::filesystem::remove_all(staged, ec);
  Check(WriteText(path, "new"), "the next write succeeds");
  Equal(ReadText(path).value_or(""), std::string{"new"},
        "a completed write replaces the file");
}
}

int main() {
  const std::filesystem::path dir = test::ScratchDir("engine_textfile");
  ReadsWhatWasWritten(dir);
  EmptyFileIsEmptyText(dir);
  MissingPathIsNamed(dir);
  OversizeFileIsRefused(dir);
  WritingCreatesParentFolders(dir);
  FailedWritePreservesTheFile(dir);
  return test::Finish("engine textfile");
}
