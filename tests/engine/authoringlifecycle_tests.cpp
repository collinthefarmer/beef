#include "engine/SessionQueue.h"
#include "engine/TextFile.h"
#include "studio/Gesture.h"
#include "studio/History.h"
#include "studio/PaintSession.h"
#include "test_support.h"

#include <filesystem>
#include <utility>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
Recipe Fixture() {
  const auto path = test::Fixtures().parent_path().parent_path() / "schema" /
                    "example-magicka.json";
  const auto parsed = ParseRecipe(test::ReadFile(path), "lifecycle");
  Check(parsed.recipe.has_value(), "authoring fixture decodes");
  return parsed.recipe.value_or(Recipe{});
}

void CheckReload(const std::filesystem::path &a_path,
                 const Recipe &a_expected) {
  const auto text = ReadText(a_path);
  Check(text.has_value(), "persisted recipe can be read after reopening");
  if (!text) {
    return;
  }
  const auto parsed = ParseRecipe(*text, a_expected.id);
  Check(parsed.recipe && !parsed.HasErrors(),
        "persisted recipe decodes without errors");
  if (parsed.recipe) {
    Check(SerializeRecipe(*parsed.recipe) == SerializeRecipe(a_expected),
          "reopened recipe preserves the complete authored document");
  }
}

void EditHistoryAndFailedSave(const std::filesystem::path &a_dir) {
  const Recipe original = Fixture();
  Recipe current = original;
  EditHistory history;
  const auto path = a_dir / "history.json";
  Check(WriteText(path, SerializeRecipe(original)), "initial save succeeds");
  const auto edited = PrepareEdits(current, {{SetPriority{17}}});
  Check(edited.has_value(), "ordinary edit prepares");
  if (!edited) {
    return;
  }
  history.Push(current);
  current = *edited;
  const auto undone = history.Undo(current);
  Check(undone == original, "undo restores the original document");
  if (!undone) {
    return;
  }
  current = *undone;
  Check(!PrepareEdits(current, {{SetPriority{19}, RemoveOutput{999}}}),
        "invalid trailing edit refuses the entire batch");
  Check(current == original && history.RedoDepth() == 1,
        "refused batch preserves the document and redo branch");
  const auto redone = history.Redo(current);
  Check(redone && *redone == *edited,
        "redo restores the complete accepted edit");
  if (!redone) {
    return;
  }
  current = *redone;
  const auto staged = std::filesystem::path{path.string() + ".writing"};
  std::filesystem::create_directory(staged);
  Check(!WriteText(path, SerializeRecipe(current)),
        "staging failure refuses saving the edited document");
  Check(current == *edited && history.UndoDepth() == 1 &&
            history.RedoDepth() == 0,
        "failed persistence preserves the working document and history");
  CheckReload(path, original);
  std::filesystem::remove(staged);
  Check(WriteText(path, SerializeRecipe(current)), "save retry succeeds");
  CheckReload(path, current);
  const auto back = history.Undo(current);
  Check(back == original, "successful persistence does not consume undo");
  if (!back) {
    return;
  }
  const auto branch = PrepareEdits(*back, {{SetPriority{23}}});
  if (!branch) {
    Check(false, "new history branch prepares");
    return;
  }
  history.Push(*back);
  Check(history.RedoDepth() == 0 && !history.Redo(*branch),
        "accepted edit after undo discards the abandoned redo branch");
  CheckReload(path, *edited);
}

void PaintCommitHistoryAndReload(const std::filesystem::path &a_dir) {
  const Recipe original = Fixture();
  Recipe current = original;
  const Recipe paint = PaintRecipe(original, RecipeKey{}, Surface::kMaterial);
  PaintCommitRequest request{1, original.id, "kept", "0.5"};
  request.assignment = PaintAssignment{0, 0, 7};
  EditHistory history;
  Check(!PreparePaintCommit(&paint, &current, request, 8),
        "paint assignment refuses a stale document revision");
  Check(current == original && history.UndoDepth() == 0,
        "stale paint refusal leaves no partial mask or history entry");
  request.assignment->documentRevision = 8;
  const auto edits = PreparePaintCommit(&paint, &current, request, 8);
  Check(edits.has_value(), "paint assignment retries at the current revision");
  if (!edits) {
    return;
  }
  const auto committed = PrepareEdits(current, *edits);
  Check(committed.has_value(), "paint commit is one atomic edit batch");
  if (!committed) {
    return;
  }
  history.Push(current);
  current = *committed;
  Check(current.FindMask("kept") && history.UndoDepth() == 1,
        "paint creates its mask and one undo entry together");
  Check(history.Undo(current) == original,
        "one undo removes both the kept mask and its layer assignment");
  Check(history.Redo(original) == current,
        "one redo restores the kept mask and assignment together");
  const auto path = a_dir / "paint.json";
  Check(WriteText(path, SerializeRecipe(current)), "paint commit saves");
  CheckReload(path, current);
}

void QueuedSaveDoesNotCrossGameLoad(const std::filesystem::path &a_dir) {
  const Recipe original = Fixture();
  Recipe next = original;
  next.priority = 33;
  const auto path = a_dir / "queued.json";
  Check(WriteText(path, SerializeRecipe(original)),
        "queued-save baseline saves");
  std::vector<SessionQueue::Task> tasks;
  SessionQueue queue{[&](SessionQueue::Task a_task) {
                       tasks.push_back(std::move(a_task));
                       return true;
                     },
                     [](std::uint32_t) {}};
  queue.Resume();
  int writes = 0;
  queue.Post([&] {
    ++writes;
    Check(WriteText(path, SerializeRecipe(next)), "queued save writes");
  });
  queue.BeginLoad();
  queue.Resume();
  queue.Post([&] {
    ++writes;
    Check(WriteText(path, SerializeRecipe(original)),
          "new-session save writes");
  });
  Check(tasks.size() == 2, "both session deliveries reach the host queue");
  if (tasks.size() != 2) {
    return;
  }
  tasks[1]();
  tasks[0]();
  Check(writes == 1,
        "late delivery from the old session cannot overwrite the new save");
  CheckReload(path, original);
}
}

int main() {
  const auto dir = test::ScratchDir("authoring_lifecycle");
  EditHistoryAndFailedSave(dir);
  PaintCommitHistoryAndReload(dir);
  QueuedSaveDoesNotCrossGameLoad(dir);
  return test::Finish("authoring lifecycle");
}
