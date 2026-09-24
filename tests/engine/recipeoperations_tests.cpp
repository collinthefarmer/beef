// GPL-3.0-only with the additional permission in COPYING.md.
#include "engine/RecipeOperations.h"
#include "engine/SessionQueue.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
void DroppedTasks() {
  for (const bool loading : {false, true}) {
    auto journal = std::make_shared<FileOperationJournal>();
    SessionQueue queue{[](SessionQueue::Task) { return false; }, {}};
    if (!loading) {
      queue.Resume();
    }
    auto file = std::make_shared<PendingFileOperation>(journal, "recipe",
                                                       FileAction::kSave);
    auto edit = std::make_shared<PendingRecipeEdit>(journal, "recipe");
    const auto gestureID = journal->BeginGesture("recipe", 1, "priority");
    auto gesture = std::make_shared<PendingGestureTask>(journal, gestureID);
    bool executed = false;
    queue.Post([file, edit, gesture, &executed] { executed = true; });
    file.reset();
    edit.reset();
    gesture.reset();
    const auto files = journal->FileOperations();
    const auto edits = journal->EditResults();
    Check(!executed && files.size() == 1 &&
              files.front().state == FileOperationState::kFailed &&
              files.front().error && edits.size() == 1 && edits.front().error,
          "loading and rejected submissions publish file/edit cancellations");
    Check(journal->gesture && !journal->gesture->pending &&
              journal->gesture->state == GesturePhase::kRefused &&
              journal->gesture->error,
          "dropped gesture task publishes a terminal refusal");
  }
}

void LoadCancellation() {
  auto journal = std::make_shared<FileOperationJournal>();
  std::vector<SessionQueue::Task> tasks;
  SessionQueue queue{[&](SessionQueue::Task task) {
                       tasks.push_back(std::move(task));
                       return true;
                     },
                     {}};
  queue.Resume();
  auto file = std::make_shared<PendingFileOperation>(journal, "recipe",
                                                     FileAction::kRevert);
  auto edit = std::make_shared<PendingRecipeEdit>(journal, "recipe");
  const auto gestureID = journal->BeginGesture("recipe", 1, "priority");
  auto gesture = std::make_shared<PendingGestureTask>(journal, gestureID);
  bool executed = false;
  queue.Post([file, edit, gesture, &executed] {
    executed = true;
    file->Finish("old.json");
    edit->Finish();
    gesture->executed = true;
  });
  Check(journal->EditResults().empty() &&
            journal->FileOperations().front().state ==
                FileOperationState::kPending,
        "UI hides pending edits and exposes pending file operations");
  queue.BeginLoad();
  journal->AbandonGesture(gestureID, "Tuning was canceled by game load.");
  journal->CancelPending();
  const auto fileError = journal->FileOperations().front().error->message;
  const auto editError = journal->EditResults().front().error->message;
  Check(fileError.find("game load") != std::string::npos &&
            editError.find("game load") != std::string::npos,
        "load publishes cancellations before old callbacks are released");
  file->Finish("late.json");
  edit->Finish();
  Check(journal->FileOperations().front().error->message == fileError &&
            journal->EditResults().front().error->message == editError,
        "late completion cannot overwrite load cancellation");
  queue.Resume();
  auto replacement = std::make_shared<PendingFileOperation>(journal, "recipe",
                                                            FileAction::kSave);
  queue.Post([replacement] { replacement->Finish("new.json"); });
  tasks.back()();
  tasks.front()();
  tasks.clear();
  file.reset();
  edit.reset();
  gesture.reset();
  replacement.reset();
  const auto files = journal->FileOperations();
  Check(!executed && files.size() == 1 && files.front().path == "new.json" &&
            files.front().state == FileOperationState::kSucceeded &&
            !files.front().error,
        "stale queue work and guard destruction preserve a newer same-ID save");
  Check(journal->EditResults().front().error->message == editError,
        "guard destruction preserves the specific load cancellation reason");
}

void TerminalGestures() {
  auto journal = std::make_shared<FileOperationJournal>();
  const auto id = journal->BeginGesture("recipe", 1, "priority");
  auto stale = *journal->gesture;
  journal->AbandonGesture(id, "load canceled");
  journal->PublishGesture(stale);
  Check(journal->gesture->state == GesturePhase::kRefused &&
            !journal->gesture->pending &&
            journal->gesture->error == "load canceled",
        "late active publication cannot revive a canceled gesture");
  const auto next = journal->BeginGesture("recipe", 2, "priority");
  Check(next > id, "terminal gesture permits a fresh gesture with a new ID");
  journal->AbandonGesture(id, "old task destroyed");
  journal->PublishGesture(stale);
  Check(journal->gesture->gestureID == next &&
            journal->gesture->state == GesturePhase::kActive,
        "old gesture guard and publication cannot affect a replacement");
  auto finished = *journal->gesture;
  finished.state = GesturePhase::kCommitted;
  journal->PublishGesture(finished);
  journal->AbandonGesture(next, "late destruction");
  Check(!journal->gesture->pending && !journal->gesture->error &&
            journal->gesture->state == GesturePhase::kCommitted,
        "successful gesture remains terminal after guard destruction");
}

void QueueDestruction() {
  auto journal = std::make_shared<FileOperationJournal>();
  SessionQueue::Task retained;
  bool executed = false;
  {
    SessionQueue queue{[&](SessionQueue::Task task) {
                         retained = std::move(task);
                         return true;
                       },
                       {}};
    queue.Resume();
    auto operation = std::make_shared<PendingRecipeEdit>(journal, "recipe");
    queue.Post([operation, &executed] {
      executed = true;
      operation->Finish();
    });
  }
  retained();
  retained = {};
  const auto results = journal->EditResults();
  Check(!executed && results.size() == 1 && results.front().error,
        "callback outliving its queue cancels safely when released");
  {
    PendingRecipeEdit completed{journal, "recipe"};
    completed.Finish();
  }
  Check(journal->EditResults().size() == 1 &&
            !journal->EditResults().front().error,
        "completed edit stays successful after its guard is destroyed");
}

void BoundedResults() {
  FileOperationJournal journal;
  const auto old = journal.Begin("same", FileAction::kSave);
  const auto current = journal.Begin("same", FileAction::kRevert);
  journal.Finish(old, "old.json", std::nullopt);
  Check(journal.FileOperations().size() == 1 &&
            journal.FileOperations().front().requestID == current &&
            journal.FileOperations().front().state ==
                FileOperationState::kPending,
        "same-recipe file replacement ignores the previous result");
  const auto oldEdit = journal.BeginEdit("same");
  const auto currentEdit = journal.BeginEdit("same");
  journal.FinishEdit(oldEdit, std::nullopt);
  Check(journal.EditResults().empty(),
        "replaced edit cannot acknowledge a newer request");
  journal.FinishEdit(currentEdit, std::nullopt);
  Check(journal.EditResults().size() == 1 &&
            journal.EditResults().front().requestID == currentEdit,
        "current edit completion is visible");
  for (int i = 0; i < 70; ++i) {
    const auto name = std::to_string(i);
    journal.Begin(name, FileAction::kSave);
    journal.BeginEdit(name);
  }
  journal.CancelPending();
  Check(journal.FileOperations().size() == 64 &&
            journal.EditResults().size() == 64,
        "file and edit result histories stay bounded through cancellation");
}
}

int main() {
  DroppedTasks();
  LoadCancellation();
  TerminalGestures();
  QueueDestruction();
  BoundedResults();
  return test::Finish("recipe operations");
}
