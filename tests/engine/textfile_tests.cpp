// GPL-3.0-only with the additional permission in COPYING.md.
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

void FailedReplacementCleansStaging(const std::filesystem::path &a_dir) {
  const auto path = a_dir / "blocked.json";
  const auto retained = path / "retained.json";
  Check(WriteText(retained, "keep"),
        "replacement obstruction contains data that must survive");
  Check(!WriteText(path, "replacement"),
        "replacement failure after staging reports failure");
  Check(ReadText(retained).value_or("") == "keep",
        "failed replacement preserves the obstructing directory and data");
  const auto staged = std::filesystem::path{path.string() + ".writing"};
  Check(!std::filesystem::exists(staged),
        "failed replacement removes its staged candidate");
  std::filesystem::remove(retained);
  std::filesystem::remove(path);
  Check(WriteText(path, "retry") && ReadText(path).value_or("") == "retry",
        "replacement succeeds after the obstruction is resolved");
}

void CreatingNeverReplaces(const std::filesystem::path &a_dir) {
  const auto path = a_dir / "imported.json";
  Check(WriteText(path, "{ not json"), "a malformed file is in place");
  Check(CreateText(path, "{\"format\": 1}") == Created::kExists,
        "CreateText refuses a path that holds a file");
  Equal(ReadText(path).value_or(""), std::string{"{ not json"},
        "the refused file keeps its bytes");
  const auto fresh = a_dir / "fresh" / "new.json";
  Check(CreateText(fresh, "x") == Created::kWritten &&
            ReadText(fresh).value_or("") == "x",
        "CreateText writes a new file and its folders");
}

void SettingAsideKeepsBothFiles(const std::filesystem::path &a_dir) {
  const auto path = a_dir / "edited.json";
  Check(WriteText(path, "hand edit"), "an edited file is in place");
  const auto aside = SetAside(path);
  Check(aside.has_value() && !std::filesystem::exists(path) &&
            ReadText(*aside).value_or("") == "hand edit" &&
            aside->filename() == "edited.json.unloaded",
        "SetAside moves the file to its set-aside name with its bytes");
  Check(WriteText(path, "second"), "a second file takes the name");
  Check(!SetAside(path).has_value(),
        "SetAside refuses when the set-aside name is taken");
  Check(ReadText(path).value_or("") == "second" &&
            ReadText(*aside).value_or("") == "hand edit",
        "a refused set-aside leaves both files intact");
  Check(!SetAside(a_dir / "absent.json").has_value(),
        "SetAside of a missing file moves nothing");
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
  FailedReplacementCleansStaging(dir);
  CreatingNeverReplaces(dir);
  SettingAsideKeepsBothFiles(dir);
  return test::Finish("engine textfile");
}
