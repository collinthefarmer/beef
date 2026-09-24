// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/Gesture.h"

#include <format>
#include <type_traits>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] std::optional<std::string> EditTarget(const RecipeEdit &a_edit) {
  const std::size_t kind = a_edit.index();
  return std::visit(
      [kind]<class T>(const T &a_value) -> std::optional<std::string> {
        if constexpr (std::is_same_v<T, SetLayerOpacity>) {
          return std::format("{}:{}:{}", kind, a_value.output, a_value.layer);
        } else if constexpr (std::is_same_v<T, SetScalar> ||
                             std::is_same_v<T, SetLightParam>) {
          return std::format("{}:{}:{}", kind, a_value.output,
                             static_cast<int>(a_value.field));
        } else if constexpr (std::is_same_v<T, SetConstant> ||
                             std::is_same_v<T, SetSignal> ||
                             std::is_same_v<T, SetExpression>) {
          return std::format("{}:{}", kind, a_value.signal);
        } else if constexpr (std::is_same_v<T, SetCurve>) {
          return std::format("{}:{}", kind, a_value.curve);
        } else if constexpr (std::is_same_v<T, SetMask>) {
          return std::format("{}:{}", kind, a_value.mask);
        } else if constexpr (std::is_same_v<T, SetSource>) {
          return std::format("{}:{}", kind, a_value.name);
        } else if constexpr (std::is_same_v<T, SetShellParam>) {
          return std::format("{}:{}", kind, static_cast<int>(a_value.field));
        } else if constexpr (std::is_same_v<T, SetShellAlphaTest> ||
                             std::is_same_v<T, SetClockSpeed> ||
                             std::is_same_v<T, SetPriority>) {
          return std::to_string(kind);
        }
        return std::nullopt;
      },
      a_edit);
}
}

std::optional<std::string>
CheckEditRevision(std::optional<std::uint64_t> a_expected,
                  std::uint64_t a_current) {
  if (a_expected && (*a_expected == 0 || *a_expected != a_current)) {
    return "The recipe changed before this edit was applied.";
  }
  return std::nullopt;
}

std::expected<RecipeGesture, std::string>
BeginRecipeGesture(const Recipe &a_recipe, std::uint64_t a_expectedRevision,
                   std::uint64_t a_revision, std::string a_propertyID) {
  if (a_expectedRevision == 0 || a_expectedRevision != a_revision) {
    return std::unexpected("The recipe changed before tuning began.");
  }
  if (a_propertyID.empty()) {
    return std::unexpected("The tuned property is not identified.");
  }
  return RecipeGesture{a_recipe, a_recipe, a_revision, std::move(a_propertyID),
                       std::nullopt};
}

bool GestureMatches(const RecipeGesture &a_gesture, const Recipe &a_current,
                    std::uint64_t a_revision) {
  return a_gesture.revision == a_revision && a_gesture.applied == a_current;
}

std::expected<GestureUpdate, std::string>
PrepareGestureUpdate(const RecipeGesture &a_gesture,
                     const GestureDocument &a_document,
                     std::string_view a_propertyID, const EditBatch &a_edits) {
  if (!GestureMatches(a_gesture, a_document.recipe, a_document.revision)) {
    return std::unexpected("The recipe changed outside this tuning gesture.");
  }
  if (a_propertyID != a_gesture.propertyID || a_edits.edits.size() != 1) {
    return std::unexpected("A tuning gesture edits one identified property.");
  }
  const std::optional<std::string> target = EditTarget(a_edits.edits.front());
  if (!target || (a_gesture.editTarget && *target != *a_gesture.editTarget)) {
    return std::unexpected("The tuned property changed.");
  }
  const std::expected<Recipe, Diagnostic> prepared =
      PrepareEdits(a_document.recipe, a_edits);
  if (!prepared) {
    return std::unexpected(std::format("{}: {}", prepared.error().where,
                                       prepared.error().message));
  }
  if (prepared->signals.size() != a_document.recipe.signals.size() ||
      prepared->sources.size() != a_document.recipe.sources.size() ||
      prepared->curves.size() != a_document.recipe.curves.size() ||
      prepared->masks.size() != a_document.recipe.masks.size()) {
    return std::unexpected("Tuning cannot create a new resource.");
  }
  return GestureUpdate{*prepared, *target};
}

std::expected<GestureFinish, std::string>
FinishRecipeGesture(const RecipeGesture &a_gesture, const Recipe &a_current,
                    std::uint64_t a_revision, bool a_commit) {
  if (!GestureMatches(a_gesture, a_current, a_revision)) {
    return std::unexpected("The recipe changed outside this tuning gesture.");
  }
  GestureFinish finish{a_commit ? a_current : a_gesture.original, std::nullopt};
  if (a_commit && a_current != a_gesture.original) {
    finish.undo = a_gesture.original;
  }
  return finish;
}

bool QueueGestureUpdate(GestureMailbox &a_mailbox, EditBatch a_edits) {
  if (a_mailbox.closed) {
    return false;
  }
  a_mailbox.latest.edits = std::move(a_edits);
  ++a_mailbox.latest.sequence;
  return true;
}

bool QueueGestureFinish(GestureMailbox &a_mailbox, bool a_commit) {
  if (a_mailbox.closed) {
    return false;
  }
  a_mailbox.closed = true;
  a_mailbox.latest.finish = a_commit;
  ++a_mailbox.latest.sequence;
  return true;
}

bool ScheduleGestureDelivery(GestureMailbox &a_mailbox) {
  return !std::exchange(a_mailbox.scheduled, true);
}

GestureDelivery TakeGestureDelivery(GestureMailbox &a_mailbox) {
  a_mailbox.scheduled = false;
  GestureDelivery out = std::move(a_mailbox.latest);
  a_mailbox.latest = {};
  a_mailbox.latest.sequence = out.sequence;
  return out;
}
}
