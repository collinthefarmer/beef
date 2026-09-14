#pragma once

#include "studio/Edits.h"

#include <array>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Studio {
enum class GesturePhase { kActive, kCommitted, kCanceled, kRefused, kCount };
inline constexpr std::array<std::string_view, 4> kGesturePhaseNames{
    "Active", "Committed", "Canceled", "Refused"};
static_assert(kGesturePhaseNames.size() ==
              static_cast<std::size_t>(GesturePhase::kCount));

struct GestureResult {
  std::uint64_t gestureID = 0;
  std::string recipeID;
  std::string propertyID;
  std::uint64_t documentRevision = 0;
  std::uint64_t sequence = 0;
  GesturePhase state = GesturePhase::kActive;
  bool pending = false;
  std::optional<std::string> error;
};

struct RecipeGesture {
  Recipe original;
  Recipe applied;
  std::uint64_t revision = 0;
  std::string propertyID;
  std::optional<std::string> editTarget;
};

struct GestureDocument {
  const Recipe &recipe;
  std::uint64_t revision = 0;
};

struct GestureUpdate {
  Recipe recipe;
  std::string editTarget;
};

struct GestureFinish {
  Recipe recipe;
  std::optional<Recipe> undo;
};

struct GestureDelivery {
  std::optional<EditBatch> edits;
  std::optional<bool> finish;
  std::uint64_t sequence = 0;
};

struct GestureMailbox {
  GestureDelivery latest;
  bool scheduled = false;
  bool closed = false;
};

[[nodiscard]] std::optional<std::string>
CheckEditRevision(std::optional<std::uint64_t> a_expected,
                  std::uint64_t a_current);

[[nodiscard]] std::expected<RecipeGesture, std::string>
BeginRecipeGesture(const Recipe &a_recipe, std::uint64_t a_expectedRevision,
                   std::uint64_t a_revision, std::string a_propertyID);
[[nodiscard]] bool GestureMatches(const RecipeGesture &a_gesture,
                                  const Recipe &a_current,
                                  std::uint64_t a_revision);
[[nodiscard]] std::expected<GestureUpdate, std::string>
PrepareGestureUpdate(const RecipeGesture &a_gesture,
                     const GestureDocument &a_document,
                     std::string_view a_propertyID, const EditBatch &a_edits);
[[nodiscard]] std::expected<GestureFinish, std::string>
FinishRecipeGesture(const RecipeGesture &a_gesture, const Recipe &a_current,
                    std::uint64_t a_revision, bool a_commit);
[[nodiscard]] bool QueueGestureUpdate(GestureMailbox &a_mailbox,
                                      EditBatch a_edits);
[[nodiscard]] bool QueueGestureFinish(GestureMailbox &a_mailbox, bool a_commit);
[[nodiscard]] bool ScheduleGestureDelivery(GestureMailbox &a_mailbox);
[[nodiscard]] GestureDelivery TakeGestureDelivery(GestureMailbox &a_mailbox);
}
