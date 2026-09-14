#include "studio/Gesture.h"
#include "studio/History.h"

#include "test_support.h"

#include <filesystem>
#include <optional>
#include <utility>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
[[nodiscard]] Recipe SampleRecipe() {
  const auto path = test::Fixtures().parent_path().parent_path() / "schema" /
                    "example-magicka.json";
  const auto parsed = ParseRecipe(test::ReadFile(path), "gesture");
  Check(parsed.recipe.has_value(), "gesture fixture parses");
  return parsed.recipe.value_or(Recipe{});
}

[[nodiscard]] EditBatch Opacity(float a_value) {
  return {{SetLayerOpacity{0, 0, a_value}}};
}

void CheckGestureHistory(const Recipe &a_base) {
  auto started = BeginRecipeGesture(a_base, 10, 10, "layer.opacity");
  Check(started.has_value(), "gesture captures a current identified property");
  if (!started) {
    return;
  }
  Recipe current = a_base;
  EditHistory history;
  std::uint64_t revision = 10;
  for (float value : {0.2f, 0.4f, 0.8f}) {
    const auto update = PrepareGestureUpdate(*started, {current, revision},
                                             "layer.opacity", Opacity(value));
    Check(update.has_value(), "live opacity update prepares");
    if (!update) {
      return;
    }
    current = update->recipe;
    started->applied = current;
    started->revision = ++revision;
    started->editTarget = update->editTarget;
  }
  const auto finish = FinishRecipeGesture(*started, current, revision, true);
  Check(finish && finish->undo && finish->recipe == current,
        "commit retains the final live value and offers original once");
  if (finish && finish->undo) {
    history.Push(*finish->undo);
  }
  Check(history.UndoDepth() == 1 && history.Undo(current) == a_base,
        "three live updates commit as one original-state undo");
  Check(history.Redo(a_base) == current, "redo restores the final tuned value");
  const auto cancel = FinishRecipeGesture(*started, current, revision, false);
  Check(cancel && !cancel->undo && cancel->recipe == a_base,
        "cancel restores original without adding history");

  Recipe external = current;
  external.priority = external.priority.value_or(0) + 1;
  Check(!FinishRecipeGesture(*started, external, revision, false),
        "cancel refuses external mutation even without a revision bump");
  Check(!FinishRecipeGesture(*started, current, revision + 1, false),
        "cancel refuses a changed revision even if values happen to match");
  Check(!PrepareGestureUpdate(*started, {external, revision}, "layer.opacity",
                              Opacity(0.1f)),
        "update cannot overwrite external modifications");
  Check(!PrepareGestureUpdate(*started, {current, revision}, "other.opacity",
                              Opacity(0.1f)),
        "update cannot transfer the gesture to another UI property");
  Check(!PrepareGestureUpdate(*started, {current, revision}, "layer.opacity",
                              {{SetClockSpeed{2.0f}}}),
        "same UI token cannot silently change the authored property");
  Check(!PrepareGestureUpdate(*started, {current, revision}, "layer.opacity",
                              {{SetLayerOpacity{999, 0, 0.2f}}}),
        "removed output target is refused");
}

void CheckNoOpAndRefusal(const Recipe &a_base) {
  Check(!BeginRecipeGesture(a_base, 9, 10, "opacity"),
        "stale begin is refused");
  Check(!BeginRecipeGesture(a_base, 10, 10, ""),
        "unidentified begin is refused");
  auto started = BeginRecipeGesture(a_base, 10, 10, "opacity");
  if (!started) {
    return;
  }
  const auto finish = FinishRecipeGesture(*started, a_base, 10, true);
  Check(finish && !finish->undo && finish->recipe == a_base,
        "unchanged gesture does not clear redo or add undo");
  const auto invalid =
      PrepareGestureUpdate(*started, {a_base, 10}, "opacity",
                           {{SetLayerOpacity{0, 0, Ref{"missing"}}}});
  Check(!invalid && started->applied == a_base,
        "invalid reference preserves the last valid state");
  Check(!PrepareGestureUpdate(*started, {a_base, 10}, "opacity",
                              {{RemoveOutput{0}}}),
        "structural removal cannot be a slider update");
}

void CheckMailbox() {
  GestureMailbox mailbox;
  Check(QueueGestureUpdate(mailbox, Opacity(0.2f)) &&
            ScheduleGestureDelivery(mailbox),
        "first update schedules one delivery");
  Check(QueueGestureUpdate(mailbox, Opacity(0.7f)) &&
            !ScheduleGestureDelivery(mailbox),
        "newest update replaces the pending value without another task");
  Check(QueueGestureFinish(mailbox, true) && !ScheduleGestureDelivery(mailbox),
        "commit shares the pending final-value delivery");
  Check(!QueueGestureUpdate(mailbox, Opacity(0.9f)),
        "updates after commit cannot replace the final value");
  const GestureDelivery delivery = TakeGestureDelivery(mailbox);
  Check(delivery.sequence == 3 && delivery.finish == true && delivery.edits &&
            Describe(*delivery.edits) == Describe(Opacity(0.7f)),
        "coalesced commit retains precisely the latest submitted value");
  Check(!mailbox.scheduled && !mailbox.latest.edits,
        "taking a delivery releases the queued batch");
}
}

int main() {
  Check(!CheckEditRevision(std::nullopt, 3),
        "ordinary edits retain optional revision contract");
  Check(!CheckEditRevision(3, 3),
        "captured revision accepts matching document");
  Check(CheckEditRevision(2, 3).has_value(),
        "captured revision rejects stale exact input");
  Check(CheckEditRevision(0, 3).has_value(),
        "unknown captured revision is refused");
  const Recipe base = SampleRecipe();
  CheckGestureHistory(base);
  CheckNoOpAndRefusal(base);
  CheckMailbox();
  return test::Finish("studio_gesture");
}
