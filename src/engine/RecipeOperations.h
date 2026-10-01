// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "studio/Gesture.h"
#include "studio/Snapshot.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
inline constexpr std::string_view kTuningCanceledByLoad =
    "Tuning was canceled by game load.";
inline constexpr std::string_view kEditCanceledByLoad =
    "Recipe edit was canceled by game load.";
inline constexpr std::string_view kFileCanceledByLoad =
    "File operation was canceled by game load.";

struct FileOperationJournal {
  mutable std::mutex lock;
  std::uint64_t nextID = 0;
  std::vector<Studio::FileOperationResult> results;
  struct EditEntry {
    Studio::RecipeEditResult result;
    bool pending = true;
  };
  std::vector<EditEntry> edits;
  std::optional<Studio::GestureResult> gesture;
  Studio::GestureMailbox gestureMailbox;
  bool gestureStarted = false;
  std::chrono::steady_clock::time_point gestureTouched;

  std::uint64_t BeginGesture(const std::string &a_id, std::uint64_t a_revision,
                             std::string a_propertyID) {
    std::scoped_lock guard{lock};
    if (gesture && gesture->state == Studio::GesturePhase::kActive) {
      return 0;
    }
    const std::uint64_t id = ++nextID;
    gesture = Studio::GestureResult{
        id,         a_id,        std::move(a_propertyID),
        a_revision, 0,           Studio::GesturePhase::kActive,
        true,       std::nullopt};
    gestureMailbox = {};
    gestureMailbox.scheduled = true;
    gestureStarted = false;
    gestureTouched = std::chrono::steady_clock::now();
    return id;
  }

  bool UpdateGesture(std::uint64_t a_id, std::string_view a_propertyID,
                     Studio::EditBatch a_edits) {
    std::scoped_lock guard{lock};
    if (!gesture || gesture->gestureID != a_id ||
        gesture->state != Studio::GesturePhase::kActive ||
        gesture->propertyID != a_propertyID ||
        !Studio::QueueGestureUpdate(gestureMailbox, std::move(a_edits))) {
      return false;
    }
    gesture->pending = true;
    gestureTouched = std::chrono::steady_clock::now();
    return Studio::ScheduleGestureDelivery(gestureMailbox);
  }

  bool EndGesture(std::uint64_t a_id, bool a_commit) {
    std::scoped_lock guard{lock};
    if (!gesture || gesture->gestureID != a_id ||
        gesture->state != Studio::GesturePhase::kActive ||
        !Studio::QueueGestureFinish(gestureMailbox, a_commit)) {
      return false;
    }
    gesture->pending = true;
    gestureTouched = std::chrono::steady_clock::now();
    return Studio::ScheduleGestureDelivery(gestureMailbox);
  }

  void PublishGesture(Studio::GestureResult a_result) {
    std::scoped_lock guard{lock};
    if (!gesture || gesture->gestureID != a_result.gestureID ||
        gesture->state != Studio::GesturePhase::kActive) {
      return;
    }
    a_result.pending = a_result.state == Studio::GesturePhase::kActive &&
                       (gestureMailbox.scheduled ||
                        gestureMailbox.latest.sequence > a_result.sequence);
    gesture = std::move(a_result);
    if (gesture->state != Studio::GesturePhase::kActive) {
      gestureMailbox = {};
      gestureMailbox.closed = true;
    }
  }

  void AbandonGesture(std::uint64_t a_id, std::string a_error) {
    std::scoped_lock guard{lock};
    if (gesture && gesture->gestureID == a_id &&
        gesture->state == Studio::GesturePhase::kActive) {
      gesture->state = Studio::GesturePhase::kRefused;
      gesture->error = std::move(a_error);
      gesture->pending = false;
      gestureMailbox = {};
      gestureMailbox.closed = true;
    }
  }

  std::uint64_t BeginEdit(const std::string &a_id) {
    std::scoped_lock guard{lock};
    std::erase_if(edits, [&](const EditEntry &a_entry) {
      return a_entry.result.recipeID == a_id;
    });
    if (edits.size() >= 64) {
      edits.erase(edits.begin());
    }
    const std::uint64_t id = ++nextID;
    edits.push_back({{id, a_id, std::nullopt}, true});
    return id;
  }

  void FinishEdit(std::uint64_t a_id, std::optional<Diagnostic> a_error) {
    std::scoped_lock guard{lock};
    for (EditEntry &entry : edits) {
      if (entry.result.requestID == a_id && entry.pending) {
        entry.result.error = std::move(a_error);
        entry.pending = false;
        return;
      }
    }
  }

  std::uint64_t Begin(const std::string &a_id, Studio::FileAction a_action) {
    std::scoped_lock guard{lock};
    std::erase_if(results, [&](const Studio::FileOperationResult &result) {
      return result.recipeID == a_id;
    });
    if (results.size() >= 64) {
      results.erase(results.begin());
    }
    const std::uint64_t id = ++nextID;
    results.push_back(
        {id, a_id, a_action, Studio::FileOperationState::kPending, {}, {}});
    return id;
  }

  void CancelPending() {
    std::scoped_lock guard{lock};
    for (EditEntry &entry : edits) {
      if (entry.pending) {
        entry.result.error =
            MakeDiagnostic(Severity::kError, entry.result.recipeID,
                           std::string{kEditCanceledByLoad});
        entry.pending = false;
      }
    }
    for (Studio::FileOperationResult &result : results) {
      if (result.state == Studio::FileOperationState::kPending) {
        result.state = Studio::FileOperationState::kFailed;
        result.error = MakeDiagnostic(Severity::kError, result.recipeID,
                                      std::string{kFileCanceledByLoad});
      }
    }
  }

  void Finish(std::uint64_t a_id, std::string a_path,
              std::optional<Diagnostic> a_error) {
    std::scoped_lock guard{lock};
    Studio::FileOperationResult *found =
        FindBy(results, a_id, &Studio::FileOperationResult::requestID);
    if (!found || found->state != Studio::FileOperationState::kPending) {
      return;
    }
    found->state = a_error ? Studio::FileOperationState::kFailed
                           : Studio::FileOperationState::kSucceeded;
    found->path = std::move(a_path);
    found->error = std::move(a_error);
  }
  std::vector<Studio::RecipeEditResult> EditResults() const {
    std::scoped_lock guard{lock};
    std::vector<Studio::RecipeEditResult> published;
    for (const FileOperationJournal::EditEntry &entry : edits) {
      if (!entry.pending) {
        published.push_back(entry.result);
      }
    }
    return published;
  }

  std::vector<Studio::FileOperationResult> FileOperations() const {
    std::scoped_lock guard{lock};
    return results;
  }
};

struct PendingFileOperation {
  std::shared_ptr<FileOperationJournal> journal;
  std::uint64_t id;

  PendingFileOperation(std::shared_ptr<FileOperationJournal> a_journal,
                       const std::string &a_recipe, Studio::FileAction a_action)
      : journal(std::move(a_journal)), id(journal->Begin(a_recipe, a_action)) {}

  PendingFileOperation(const PendingFileOperation &) = delete;
  PendingFileOperation &operator=(const PendingFileOperation &) = delete;
  PendingFileOperation(PendingFileOperation &&) = delete;
  PendingFileOperation &operator=(PendingFileOperation &&) = delete;

  ~PendingFileOperation() {
    journal->Finish(id, {},
                    MakeDiagnostic(Severity::kError, "file",
                                   "File operation was canceled before "
                                   "completion."));
  }

  void Finish(std::string a_path,
              std::optional<Diagnostic> a_error = std::nullopt) const {
    journal->Finish(id, std::move(a_path), std::move(a_error));
  }
};

struct PendingGestureTask {
  std::shared_ptr<FileOperationJournal> journal;
  std::uint64_t id;
  bool executed = false;
  std::string cancellationError = "Tuning task was canceled before completion.";

  PendingGestureTask(std::shared_ptr<FileOperationJournal> a_journal,
                     std::uint64_t a_id)
      : journal(std::move(a_journal)), id(a_id) {}
  PendingGestureTask(const PendingGestureTask &) = delete;
  PendingGestureTask &operator=(const PendingGestureTask &) = delete;
  PendingGestureTask(PendingGestureTask &&) = delete;
  PendingGestureTask &operator=(PendingGestureTask &&) = delete;
  ~PendingGestureTask() {
    if (!executed) {
      journal->AbandonGesture(id, std::move(cancellationError));
    }
  }
};

struct PendingRecipeEdit {
  std::shared_ptr<FileOperationJournal> journal;
  std::uint64_t id;

  PendingRecipeEdit(std::shared_ptr<FileOperationJournal> a_journal,
                    const std::string &a_recipe)
      : journal(std::move(a_journal)), id(journal->BeginEdit(a_recipe)) {}

  PendingRecipeEdit(const PendingRecipeEdit &) = delete;
  PendingRecipeEdit &operator=(const PendingRecipeEdit &) = delete;
  PendingRecipeEdit(PendingRecipeEdit &&) = delete;
  PendingRecipeEdit &operator=(PendingRecipeEdit &&) = delete;

  ~PendingRecipeEdit() {
    journal->FinishEdit(id, MakeDiagnostic(Severity::kError, "edit",
                                           "Recipe edit was canceled before "
                                           "completion."));
  }

  void Finish(std::optional<Diagnostic> a_error = std::nullopt) const {
    journal->FinishEdit(id, std::move(a_error));
  }
};

}
