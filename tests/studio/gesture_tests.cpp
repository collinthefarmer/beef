// GPL-3.0-only with the additional permission in COPYING.md.
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
  Check(QueueGestureUpdate(mailbox, Opacity(0.2f), 0) == GestureQueued::kPost,
        "first update posts one delivery");
  Check(QueueGestureUpdate(mailbox, Opacity(0.7f), 0) == GestureQueued::kQueued,
        "newest update replaces the pending value without another task");
  Check(QueueGestureFinish(mailbox, true, 0) == GestureQueued::kQueued,
        "commit shares the pending final-value delivery");
  Check(QueueGestureUpdate(mailbox, Opacity(0.9f), 0) ==
            GestureQueued::kRefused,
        "updates after commit cannot replace the final value");
  const std::optional<TakenGesture> taken = TakeGestureDelivery(mailbox, 0);
  Check(taken && taken->begins && taken->delivery.sequence == 3 &&
            taken->delivery.finish == true && taken->delivery.edits &&
            Describe(*taken->delivery.edits) == Describe(Opacity(0.7f)),
        "coalesced commit retains precisely the latest submitted value");
  Check(mailbox.delivery == GestureDeliveryState::kIdle &&
            !mailbox.latest.edits,
        "taking a delivery releases the queued batch");
}

void CheckHold() {
  GestureMailbox mailbox = OpenGestureMailbox(0);
  Check(mailbox.delivery == GestureDeliveryState::kPosted,
        "an opened gesture has its first delivery posted");
  const std::optional<TakenGesture> first = TakeGestureDelivery(mailbox, 0);
  Check(first && first->begins,
        "the delivery that begins a gesture is never held");
  Check(QueueGestureUpdate(mailbox, Opacity(0.2f), 10) == GestureQueued::kPost,
        "the first drag update posts");
  Check(TakeGestureDelivery(mailbox, 10).has_value(),
        "an update before any apply goes through");
  NoteGestureApplied(mailbox, 10);
  Check(QueueGestureUpdate(mailbox, Opacity(0.3f), 50) == GestureQueued::kPost,
        "the next update posts");
  Check(!TakeGestureDelivery(mailbox, 50) &&
            mailbox.delivery == GestureDeliveryState::kHeld,
        "an update soon after the last applied one waits");
  Check(QueueGestureUpdate(mailbox, Opacity(0.4f), 60) ==
            GestureQueued::kQueued,
        "updates during a hold post nothing new");
  Check(!ReleaseHeldGesture(mailbox, 109),
        "the hold lasts until one interval after the last apply");
  Check(ReleaseHeldGesture(mailbox, 110) &&
            mailbox.delivery == GestureDeliveryState::kPosted,
        "the hold releases one interval after the last apply");
  const std::optional<TakenGesture> released =
      TakeGestureDelivery(mailbox, 110);
  Check(released && released->delivery.edits &&
            Describe(*released->delivery.edits) == Describe(Opacity(0.4f)),
        "the released delivery carries only the newest value");
  NoteGestureApplied(mailbox, 110);
  Check(QueueGestureUpdate(mailbox, Opacity(0.5f), 120) == GestureQueued::kPost,
        "an update after the release posts");
  Check(!TakeGestureDelivery(mailbox, 120), "it is held again");
  Check(QueueGestureFinish(mailbox, true, 130) == GestureQueued::kPost,
        "a finish releases a held delivery");
  Check(TakeGestureDelivery(mailbox, 130).has_value(),
        "the final value is never held");
}

void CheckIdle() {
  GestureMailbox mailbox = OpenGestureMailbox(1000);
  Check(!GestureIdle(mailbox, 3000), "two seconds untouched is not idle yet");
  Check(GestureIdle(mailbox, 3001), "past two seconds untouched is idle");
  TouchGesture(mailbox, 2500);
  Check(!GestureIdle(mailbox, 4000), "a touch restarts the idle time");
  Check(QueueGestureUpdate(mailbox, Opacity(0.2f), 5000) ==
                GestureQueued::kQueued &&
            !GestureIdle(mailbox, 6500),
        "an update restarts the idle time");
  GestureMailbox wrapped = OpenGestureMailbox(0xFFFFFF00u);
  Check(!GestureIdle(wrapped, 0x00000100u),
        "the idle time survives the clock wrapping");
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
  CheckHold();
  CheckIdle();
  return test::Finish("studio_gesture");
}
