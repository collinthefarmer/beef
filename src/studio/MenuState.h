#pragma once

#include "Core.h"
#include "studio/EditResult.h"
#include "studio/History.h"
#include "studio/Intent.h"
#include "studio/Mask.h"
#include "studio/Navigation.h"
#include "studio/PaintSession.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/SourcePlan.h"
#include "studio/View.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
using FieldKey = std::uint32_t;
inline constexpr FieldKey kNoField = 0;

[[nodiscard]] FieldKey HashFieldKey(std::string_view a_scope,
                                    std::string_view a_leaf);
[[nodiscard]] FieldKey HashFieldKey(std::string_view a_scope,
                                    std::string_view a_field,
                                    std::string_view a_leaf);

using TextBuffer = std::array<char, 1024>;
using NumberBuffer = std::array<float, 3>;

struct FiringDraft {
  std::string node;
  Vec3 offset;
  float random = 0.0f;
  float value = 1.0f;
};

struct ExpressionDraft {
  std::string text;
  std::uint64_t revision = 0;
};

struct TuningGesture {
  std::uint64_t id = 0;
  FieldKey field = kNoField;
  std::string recipeID;
  InspectorSubject subject;
  float value = 0.0f;
  std::function<std::optional<RecipeEdit>(const std::string &)> bind;
  bool seen = false;
  bool finishing = false;
};

struct MenuState {
  Mode mode = Mode::kCompose;
  Layout layout;
  Selection selection;
  Navigation navigation;
  std::optional<InspectorSubject> pendingSelection;
  std::optional<PropertyLocation> revealedProperty;
  std::string referencePopup;
  std::optional<PreviewPin> previewPin;
  std::optional<PendingIndexedEdit> pendingIndexedEdit;
  std::optional<PendingIndexedEdit> pendingRecipeFile;
  std::optional<TuningGesture> tuning;
  std::unordered_map<FieldKey, std::pair<float, float>> tuningRanges;
  std::unordered_map<FieldKey, ExpressionDraft> expressionDrafts;
  bool settings = false;
  MaskStack mask;
  std::optional<PaintSession> paint;
  std::uint64_t nextPaintCommitID = 1;
  std::uint64_t nextPaintSessionID = 1;
  std::uint64_t lastPaintReset = 0;
  ResourceTab resource = ResourceTab::kSignals;
  float navigatorShare = 0.34f;
  float inspectorShare = 0.71f;
  std::unordered_map<FieldKey, TextBuffer> textBuffers;
  std::unordered_map<FieldKey, NumberBuffer> numberBuffers;
  FieldKey activeField = kNoField;
  std::unordered_map<FieldKey, bool> comboMode;
  FieldKey focusField = kNoField;
  std::string fieldScope;
  Mode modeDrawn = Mode::kCompose;
  FiringDraft firing;
  History<MaskStack> maskHistory;
};

[[nodiscard]] inline MenuState &State() {
  static MenuState state;
  return state;
}

[[nodiscard]] SourceCatalog PaintSources(const MenuState &a_state,
                                         const RecipeRow &a_recipe,
                                         const Intents &a_pending);
[[nodiscard]] bool AcceptIntent(const MenuState &a_state,
                                const Intent &a_intent);
void Reduce(MenuState &a_state, const Intent &a_intent);
void ObservePaintRecipe(MenuState &a_state, const RecipeRow *a_recipe);
[[nodiscard]] bool MaskTaskActive(const MenuState &a_state);
void ReconcilePaintMode(MenuState &a_state);
void ResolveEditorSelection(MenuState &a_state, const Snapshot &a_snapshot);
void AcknowledgeEditorOperations(MenuState &a_state,
                                 const Snapshot &a_snapshot);
void AcknowledgePaintUpdate(MenuState &a_state,
                            const PaintUpdateResult &a_result);
[[nodiscard]] std::optional<UpdatePaint>
PendingPaintUpdate(const MenuState &a_state);
void AcknowledgePaintCommit(MenuState &a_state,
                            const PaintCommitResult &a_result);
}
