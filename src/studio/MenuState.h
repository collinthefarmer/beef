// GPL-3.0-only with the additional permission in COPYING.md.
#pragma once

#include "Core.h"
#include "recipe/Expression.h"
#include "studio/ClusterReadout.h"
#include "studio/EditResult.h"
#include "studio/History.h"
#include "studio/Intent.h"
#include "studio/Mask.h"
#include "studio/Navigation.h"
#include "studio/PaintSession.h"
#include "studio/PreviewWindows.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/SourcePlan.h"
#include "studio/View.h"
#include "studio/Widgets.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
using FieldKey = std::uint32_t;
inline constexpr FieldKey kNoField = 0;

[[nodiscard]] FieldKey HashFieldKey(std::string_view a_scope,
                                    std::string_view a_leaf);
[[nodiscard]] FieldKey HashFieldKey(std::string_view a_scope,
                                    std::string_view a_field,
                                    std::string_view a_leaf);

using TextBuffer = std::array<char, kMaxExpressionLength + 1>;
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

struct NameDraft {
  FieldKey label = kNoField;
  std::string text;
  bool awaitingFocus = true;
};

struct TuningGesture {
  std::uint64_t id = 0;
  FieldKey field = kNoField;
  std::string recipeID;
  InspectorSubject subject;
  float value = 0.0f;
  std::function<std::optional<RecipeEdit>(const std::string &)> bind;
  std::optional<std::pair<float, float>> span;
  std::optional<std::string> refusal;
  bool seen = false;
  bool finishing = false;
};

struct AwaitedPiece {
  PieceRef piece;
  std::string recipeID;
  std::string label;
};

struct ComboChoice {
  bool combo = false;
  bool basis = false;
};

struct FieldPost {
  FieldKey field = kNoField;
  std::string recipeID;
  std::uint64_t request = 0;
};

struct FieldRefusal {
  FieldKey field = kNoField;
  std::string recipeID;
  Diagnostic problem;
};

struct PendingEditorChange {
  std::uint64_t requestID = 0;
  std::string recipeID;
  Intent intent;
  Selection selection;
};

struct MenuState {
  Mode mode = Mode::kCompose;
  Layout layout;
  Selection selection;
  Navigation navigation;
  std::optional<PendingSubject> pendingSelection;
  std::uint64_t seenEditResult = 0;
  std::optional<PropertyLocation> revealedProperty;
  std::string referencePopup;
  PreviewWindows previews;
  std::optional<PendingIndexedEdit> pendingIndexedEdit;
  std::optional<PendingIndexedEdit> pendingRecipeFile;
  std::optional<PendingEditorChange> pendingEditorChange;
  std::optional<Diagnostic> creationRefusal;
  FieldKey drawingField = kNoField;
  std::optional<FieldPost> fieldPost;
  std::optional<FieldRefusal> fieldRefusal;
  std::optional<TuningGesture> tuning;
  std::optional<FieldKey> termDrag;
  std::unordered_map<FieldKey, std::pair<float, float>> tuningRanges;
  std::unordered_map<FieldKey, ExpressionDraft> expressionDrafts;
  std::optional<NameDraft> nameDraft;
  bool settings = false;
  MaskStack mask;
  std::optional<PaintSession> paint;
  std::uint64_t nextPaintCommitID = 1;
  std::uint64_t nextPaintSessionID = 1;
  std::uint64_t lastPaintReset = 0;
  ResourceTab resource = ResourceTab::kSignals;
  float navigatorShare = 0.34f;
  float footerHeight = 0.0f;
  std::unordered_map<FieldKey, TextBuffer> textBuffers;
  std::unordered_set<FieldKey> unappliedTexts;
  std::unordered_map<FieldKey, NumberBuffer> numberBuffers;
  FieldKey activeField = kNoField;
  std::unordered_map<FieldKey, ComboChoice> comboMode;
  FieldKey focusField = kNoField;
  std::string fieldScope;
  Mode modeDrawn = Mode::kCompose;
  FiringDraft firing;
  History<MaskStack> maskHistory;
  ClusterMemo clusterMemo;
  std::optional<PieceRef> pendingPiece;
  std::optional<AwaitedPiece> awaitedPiece;
  std::string pieceLabel;
  ClusterMemo sourceClusterMemo;
};

[[nodiscard]] inline MenuState &State() {
  static MenuState state;
  return state;
}

[[nodiscard]] std::vector<RecipeEdit>
PaintDependencies(const MenuState &a_state);
[[nodiscard]] SourceCatalog PaintSources(const MenuState &a_state,
                                         const RecipeRow &a_recipe,
                                         const Intents &a_pending);
[[nodiscard]] bool AcceptIntent(const MenuState &a_state,
                                const Intent &a_intent);
void Reduce(MenuState &a_state, const Intent &a_intent);
void ObservePaintRecipe(MenuState &a_state, const RecipeRow *a_recipe);
[[nodiscard]] bool MaskTaskActive(const MenuState &a_state);
[[nodiscard]] bool IsRenameBlocked(const MenuState &a_state);
[[nodiscard]] bool CanKeepMask(const MenuState &a_state);
[[nodiscard]] std::array<RuleButton, 4>
MaskRuleButtonsOf(const MenuState &a_state);
[[nodiscard]] std::string MaskProblemsOf(const MenuState &a_state);
void ReconcilePaintMode(MenuState &a_state);
void ResolveEditorSelection(MenuState &a_state, const Snapshot &a_snapshot);
void AcknowledgeEditorOperations(MenuState &a_state,
                                 const Snapshot &a_snapshot);
void TrackEditorChange(MenuState &a_state, std::uint64_t a_request,
                       const Intent &a_intent);
void NoteFieldPost(MenuState &a_state, std::string a_recipeID);
void NoteFieldRequest(MenuState &a_state, std::string_view a_recipeID,
                      std::uint64_t a_request);
[[nodiscard]] const Diagnostic *FieldRefusalOf(const MenuState &a_state,
                                               FieldKey a_field);
void AcknowledgePaintUpdate(MenuState &a_state,
                            const PaintUpdateResult &a_result);
[[nodiscard]] std::optional<UpdatePaint>
PendingPaintUpdate(const MenuState &a_state);
void AcknowledgePaintCommit(MenuState &a_state,
                            const PaintCommitResult &a_result);
}
