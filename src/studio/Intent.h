#pragma once

#include "Core.h"
#include "recipe/Recipe.h"
#include "studio/EditResult.h"
#include "studio/Edits.h"
#include "studio/History.h"
#include "studio/Mask.h"
#include "studio/Navigation.h"
#include "studio/PaintSession.h"
#include "studio/Selection.h"
#include "studio/Snapshot.h"
#include "studio/SourcePlan.h"
#include "studio/View.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
using FieldKey = std::uint32_t;
inline constexpr FieldKey kNoField = 0;

enum class ResourceTab {
  kSignals,
  kCurves,
  kSources,
  kMasks,
};
inline constexpr std::array<ResourceTab, 4> kResourceTabs{
    ResourceTab::kSignals, ResourceTab::kCurves, ResourceTab::kSources,
    ResourceTab::kMasks};
[[nodiscard]] std::string_view ResourceTabName(ResourceTab a_tab) noexcept;

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
  std::optional<PropertyLocation> revealedProperty;
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
  std::unordered_map<FieldKey, TextBuffer> textBuffers;
  std::unordered_map<FieldKey, NumberBuffer> numberBuffers;
  FieldKey activeField = kNoField;
  std::unordered_map<FieldKey, bool> comboMode;
  FieldKey focusField = kNoField;
  Mode modeDrawn = Mode::kCompose;
  FiringDraft firing;
  History<MaskStack> maskHistory;
};

[[nodiscard]] inline MenuState &State() {
  static MenuState state;
  return state;
}

struct SetMode {
  Mode mode = Mode::kCompose;
};
struct PickPiece {
  PieceRef piece;
};
struct PickRecipe {
  std::string recipeID;
  bool document = false;
};
struct PinRecipe {
  std::string recipeID;
};
struct PickTarget {
  Target target = Target::kMaterial;
};
struct PickSlot {
  Slot slot = Slot::kEmissive;
};
struct PickCell {
  Surface surface = Surface::kMaterial;
  Slot slot = Slot::kEmissive;
  std::optional<std::size_t> topLayer;
};
struct PickLayer {
  std::size_t index = 0;
};
struct ViewGeometry {
  std::string name;
};
struct SetStackSplit {
  float ratio = 0.5f;
};
struct ScratchRebuilt {};
struct ShowSettings {
  bool on = false;
};
struct ShowResource {
  ResourceTab tab = ResourceTab::kSignals;
};
struct AddTerm {
  Term term;
  std::vector<RecipeEdit> sources{};
};
struct SetTermOp {
  std::size_t index = 0;
  TermOp op = TermOp::kAnd;
};
struct SetTermText {
  std::size_t index = 0;
  std::string text;
};
struct SetTermKind {
  std::size_t index = 0;
  TermKind kind;
  std::string text;
  std::string label;
  std::vector<RecipeEdit> sources{};
};
struct RemoveTerm {
  std::size_t index = 0;
};
struct MoveTerm {
  std::size_t from = 0;
  std::size_t to = 0;
};
struct PickTerm {
  std::size_t index = 0;
};
struct SoloTerm {
  std::size_t index = 0;
  bool on = false;
};
struct MuteTerm {
  std::size_t index = 0;
  bool on = false;
};
struct LoadMask {
  std::vector<Term> terms;
  std::string editing;
};
struct ClearMask {};
struct UndoMask {};
struct RedoMask {};
struct BeginPaint {
  std::string recipeID;
  RecipeKey key;
  Surface surface = Surface::kMaterial;
  std::uint64_t sessionID = 0;
  std::uint64_t resetID = 0;
  std::optional<PaintAssignment> assignment{};
};
struct SetPaintSurface {
  Surface surface = Surface::kMaterial;
};
struct KeepPaint {
  PaintCommitRequest request;
};
struct EndPaint {};
struct UpdatePaint {
  PaintUpdateRequest request;
};
struct ReadMesh {
  FormID actorID = 0;
  std::string geometry;
};
struct EditRecipe {
  std::string recipeID;
  std::vector<RecipeEdit> edits;
  std::optional<std::uint64_t> expectedRevision = std::nullopt;
};
struct SoloRecipe {
  std::string recipeID;
  bool on = false;
};
struct SoloOutput {
  std::string recipeID;
  std::size_t output = 0;
  bool on = false;
};
struct SoloLayer {
  std::string recipeID;
  std::size_t output = 0;
  std::size_t layer = 0;
  bool on = false;
};
struct MuteLayer {
  std::string recipeID;
  std::size_t output = 0;
  std::size_t layer = 0;
  bool on = false;
};
struct SetFreeze {
  bool on = false;
  float at = 0.0f;
};
struct SetScrub {
  float seconds = 0.0f;
};
struct SetSpeed {
  float speed = 1.0f;
};
struct StepClock {};
struct Undo {
  std::string recipeID;
};
struct Redo {
  std::string recipeID;
};
struct CreateRecipe {
  std::string recipeID;
  RecipeKey key;
  std::string geometry;
};
struct RenameRecipe {
  std::string from;
  std::string to;
};
struct FireTrigger {
  FormID actorID = 0;
  std::string event;
  std::string node;
  Vec3 offset;
  float random = 0.0f;
  float value = 1.0f;
};

using Intent =
    std::variant<SetMode, PickPiece, PickRecipe, PinRecipe, PickTarget,
                 PickSlot, PickCell, PickLayer, ViewGeometry, SetStackSplit,
                 ShowSettings, ShowResource, ReadMesh, AddTerm, SetTermOp,
                 SetTermText, SetTermKind, RemoveTerm, MoveTerm, PickTerm,
                 SoloTerm, MuteTerm, LoadMask, ClearMask, UndoMask, RedoMask,
                 ScratchRebuilt, BeginPaint, SetPaintSurface, KeepPaint,
                 EndPaint, UpdatePaint, EditRecipe, SoloRecipe, SoloOutput,
                 SoloLayer, MuteLayer, SetFreeze, SetScrub, SetSpeed, StepClock,
                 Undo, Redo, CreateRecipe, RenameRecipe, FireTrigger>;
inline constexpr std::size_t kIntentCount = 46;
static_assert(std::variant_size_v<Intent> == kIntentCount);

using Intents = std::vector<Intent>;

void Post(Intents &a_out, Intent a_intent);
void Post(Intents &a_out, const std::string &a_recipe, RecipeEdit a_edit);

[[nodiscard]] SourceCatalog PaintSources(const MenuState &a_state,
                                         const RecipeRow &a_recipe,
                                         const Intents &a_pending);
[[nodiscard]] bool AcceptIntent(const MenuState &a_state,
                                const Intent &a_intent);
void Reduce(MenuState &a_state, const Intent &a_intent);
void ObservePaintRecipe(MenuState &a_state, const RecipeRow *a_recipe);
[[nodiscard]] bool MaskTaskActive(const MenuState &a_state);
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
