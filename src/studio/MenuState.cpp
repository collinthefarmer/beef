// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/MenuState.h"

#include "recipe/Expression.h"
#include "studio/Intent.h"
#include "studio/View.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] FieldKey FoldFieldKey(const std::string &a_composed) {
  const std::size_t hash = std::hash<std::string_view>{}(a_composed);
  const auto folded = static_cast<FieldKey>(hash ^ (hash >> 32));
  return folded == kNoField ? FieldKey{1} : folded;
}
}

FieldKey HashFieldKey(std::string_view a_scope, std::string_view a_leaf) {
  std::string composed;
  composed.reserve(a_scope.size() + a_leaf.size() + 1);
  composed.append(a_scope);
  composed.push_back('\x1f');
  composed.append(a_leaf);
  return FoldFieldKey(composed);
}

FieldKey HashFieldKey(std::string_view a_scope, std::string_view a_field,
                      std::string_view a_leaf) {
  std::string composed;
  composed.reserve(a_scope.size() + a_field.size() + a_leaf.size() + 2);
  composed.append(a_scope);
  composed.push_back('\x1f');
  composed.append(a_field);
  composed.push_back('\x1f');
  composed.append(a_leaf);
  return FoldFieldKey(composed);
}

namespace {
[[nodiscard]] std::optional<std::size_t>
IndexAfterRemoval(std::size_t a_at, std::size_t a_removed) {
  if (a_at == a_removed) {
    return std::nullopt;
  }
  return a_at > a_removed ? a_at - 1 : a_at;
}

[[nodiscard]] std::size_t IndexAfterMove(std::size_t a_at, std::size_t a_from,
                                         std::size_t a_to) {
  if (a_at == a_from) {
    return a_to;
  }
  if (a_from < a_at && a_at <= a_to) {
    return a_at - 1;
  }
  if (a_to <= a_at && a_at < a_from) {
    return a_at + 1;
  }
  return a_at;
}

void ReduceAddLayer(Selection &a_selection, const AddLayer &a_edit) {
  if (a_edit.at) {
    a_selection.layer = *a_edit.at;
  }
}

void ReduceRemoveLayer(Selection &a_selection, const RemoveLayer &a_edit) {
  if (a_selection.layer) {
    a_selection.layer = IndexAfterRemoval(*a_selection.layer, a_edit.layer);
  }
}

void ReduceMoveLayer(Selection &a_selection, const MoveLayer &a_edit) {
  if (a_selection.layer) {
    a_selection.layer =
        IndexAfterMove(*a_selection.layer, a_edit.from, a_edit.to);
  }
}

void ReduceAddOutput(Selection &a_selection, const AddOutput &a_edit) {
  a_selection.target = TargetOf(a_edit.surface);
  a_selection.slot = a_edit.slot;
  a_selection.layer.reset();
}

void ReduceEdit(Selection &a_selection, const RecipeEdit &a_edit) {
  Match(
      a_edit, [&](const AddLayer &a_e) { ReduceAddLayer(a_selection, a_e); },
      [&](const RemoveLayer &a_e) { ReduceRemoveLayer(a_selection, a_e); },
      [&](const MoveLayer &a_e) { ReduceMoveLayer(a_selection, a_e); },
      [&](const ClearLayers &) { a_selection.layer.reset(); },
      [&](const RemoveOutput &) { a_selection.layer.reset(); },
      [&](const AddOutput &a_e) { ReduceAddOutput(a_selection, a_e); },
      [](const SetLayerSource &) {}, [](const SetLayerCurve &) {},
      [](const SetLayerBlend &) {}, [](const SetLayerOpacity &) {},
      [](const SetLayerColor &) {}, [](const SetLayerMask &) {},
      [](const SetLayerChannels &) {}, [](const SetScalar &) {},
      [](const SetColorScalar &) {}, [](const SetOutputReplace &) {},
      [](const SetOutputSelector &) {}, [](const AddKey &) {},
      [](const RemoveKey &) {}, [](const ClearOutputs &) {},
      [](const ClearResources &) {}, [](const ClearRecipe &) {},
      [](const SetPriority &) {}, [](const SetOverride &) {},
      [](const SetClockSpeed &) {}, [](const SetConstant &) {},
      [](const SetExpression &) {}, [](const SetSignal &) {},
      [](const SetSignalCurve &) {}, [](const SetCurve &) {},
      [](const SetMask &) {}, [](const AddSignal &) {}, [](const AddCurve &) {},
      [](const RenameSignal &) {}, [](const RenameCurve &) {},
      [](const RemoveSignal &) {}, [](const RemoveCurve &) {},
      [](const AddMask &) {}, [](const RenameMask &) {},
      [](const RemoveMask &) {}, [](const AddSource &) {},
      [](const SetSource &) {}, [](const RenameSource &) {},
      [](const RemoveSource &) {}, [](const AddLight &) {},
      [](const SetLightParam &) {}, [](const SetLightVector &) {},
      [](const SetLightShadow &) {}, [](const SetLightBones &) {},
      [](const SetLightReplace &) {}, [](const SetLightSelector &) {},
      [](const ResetLight &) {}, [](const ResetOutput &) {},
      [](const SetShellParam &) {}, [](const SetShellVector &) {},
      [](const SetShellPoint &) {}, [](const SetShellMaterial &) {},
      [](const SetShellBlend &) {}, [](const SetShellDepthBias &) {},
      [](const SetShellAlphaTest &) {}, [](const ResetShell &) {});
}

template <class Map> void RemapMask(MaskStack &a_mask, const Map &a_map) {
  const auto remap = [&](std::optional<std::size_t> &a_index) {
    if (a_index) {
      a_index = a_map(*a_index);
    }
  };
  remap(a_mask.selected);
  remap(a_mask.solo);
  std::set<std::size_t> muted;
  for (const std::size_t index : a_mask.muted) {
    if (const std::optional<std::size_t> to = a_map(index)) {
      muted.insert(*to);
    }
  }
  a_mask.muted = std::move(muted);
}
}

std::string_view ModeName(Mode a_mode) noexcept {
  switch (a_mode) {
  case Mode::kCompose:
    return "Compose";
  case Mode::kPaint:
    return "Paint";
  }
  return "?";
}

Layout LayoutFor(Mode a_mode) noexcept {
  for (const Layout &layout : kLayouts) {
    if (layout.mode == a_mode) {
      return layout;
    }
  }
  return Layout{};
}

std::string_view ResourceTabName(ResourceTab a_tab) noexcept {
  switch (a_tab) {
  case ResourceTab::kSignals:
    return "Signals";
  case ResourceTab::kCurves:
    return "Curves";
  case ResourceTab::kSources:
    return "Sources";
  case ResourceTab::kMasks:
    return "Masks";
  }
  return "?";
}

void Post(Intents &a_out, Intent a_intent) {
  a_out.push_back(std::move(a_intent));
}

void Post(Intents &a_out, const std::string &a_recipe, RecipeEdit a_edit) {
  std::vector<RecipeEdit> edits;
  edits.push_back(std::move(a_edit));
  a_out.emplace_back(std::in_place_type<EditRecipe>, a_recipe,
                     std::move(edits));
}

namespace {
struct ReduceVisitor {
  MenuState &state;
  Selection &selection;
  MaskStack &mask;

  explicit ReduceVisitor(MenuState &a_state)
      : state(a_state), selection(a_state.selection), mask(a_state.mask) {}

  void EndSession() {
    if (state.paint) {
      selection = state.paint->origin;
      state.navigation.scroll = state.paint->originScroll;
    }
    state.paint.reset();
    state.mode = Mode::kCompose;
    const float split = state.layout.stackSplit;
    state.layout = LayoutFor(Mode::kCompose);
    state.layout.stackSplit = split;
    mask = MaskStack{};
    state.maskHistory.Clear();
  }

  void Remember() { state.maskHistory.Push(mask); }

  void InvalidateAssignment(const std::string &a_recipeID) {
    if (state.paint && state.paint->recipeID == a_recipeID &&
        state.paint->assignment) {
      state.paint->assignment.reset();
      state.paint->assignmentInvalid = true;
      state.paint->origin.subject = RecipeSubject{};
      state.paint->origin.property.reset();
      state.paint->origin.layer.reset();
    }
  }

  void SelectRecipe(const std::string &a_id) {
    if (selection.recipeID != a_id) {
      state.previewPin.reset();
      if (!state.paint) {
        mask = MaskStack{};
      } else {
        operator()(SetMode{Mode::kCompose});
      }
      state.navigation = Navigation{};
      selection.subject = RecipeSubject{};
      selection.property.reset();
      state.revealedProperty.reset();
    }
    selection.recipeID = a_id;
    selection.layer.reset();
  }

  void FocusRecipe(const std::string &a_id) {
    state.previewPin.reset();
    if (state.paint) {
      operator()(SetMode{Mode::kCompose});
    }
    state.navigation = Navigation{};
    selection.subject = RecipeSubject{};
    selection.property.reset();
    state.revealedProperty.reset();
    selection.recipeID = a_id;
    selection.document = true;
    selection.layer.reset();
    if (!state.paint) {
      mask = MaskStack{};
    }
  }

  void operator()(const SetMode &a_i) {
    if (state.mode != a_i.mode) {
      const float split = state.layout.stackSplit;
      state.mode = a_i.mode;
      state.layout = LayoutFor(a_i.mode);
      state.layout.stackSplit = split;
      if (a_i.mode == Mode::kPaint) {
        state.resource = ResourceTab::kMasks;
        if (state.paint) {
          selection = state.paint->origin;
          selection.document = true;
          state.navigation.scroll = state.paint->originScroll;
        }
      }
    }
  }

  void operator()(const PickPiece &a_i) {
    state.previewPin.reset();
    if (state.paint) {
      operator()(SetMode{Mode::kCompose});
    }
    selection = Selection{};
    state.navigation = Navigation{};
    selection.piece = a_i.piece;
    if (!state.paint) {
      mask = MaskStack{};
    }
  }

  void operator()(const PickRecipe &a_i) {
    SelectRecipe(a_i.recipeID);
    selection.document = a_i.document;
  }

  void operator()(const PinRecipe &a_i) {
    SelectRecipe(a_i.recipeID);
    selection.document = false;
  }

  void operator()(const PickTarget &a_i) {
    if (selection.target != a_i.target) {
      selection.target = a_i.target;
      selection.slot.reset();
      selection.layer.reset();
    }
  }

  void operator()(const PickSlot &a_i) {
    selection.slot = a_i.slot;
    selection.layer.reset();
  }

  void operator()(const PickCell &a_i) {
    selection.target = TargetOf(a_i.surface);
    selection.slot = a_i.slot;
    selection.layer = a_i.topLayer;
  }

  void operator()(const PickLayer &a_i) { selection.layer = a_i.index; }

  void operator()(const ViewGeometry &a_i) {
    selection.geometry = a_i.name;
    if (state.paint && state.mode == Mode::kPaint) {
      state.paint->previewGeometry = a_i.name;
    }
  }

  void operator()(const SetStackSplit &a_i) {
    state.layout.stackSplit = std::clamp(a_i.ratio, 0.05f, 0.95f);
  }

  void operator()(const SetWorkspaceSplit &a_i) {
    float &share = a_i.pane == WorkspacePane::kNavigator ? state.navigatorShare
                                                         : state.inspectorShare;
    share = std::clamp(a_i.ratio, 0.05f, 0.95f);
  }

  void operator()(const ShowSettings &a_i) { state.settings = a_i.on; }

  void operator()(const ShowResource &a_i) { state.resource = a_i.tab; }

  void operator()(const ReadMesh &a_i) {
    if (state.paint) {
      state.paint->readGeometries.insert(a_i.geometry);
    }
  }

  void operator()(const AddTerm &a_i) {
    if (mask.terms.size() >= kMaxTerms) {
      return;
    }
    Remember();
    Term term = a_i.term;
    if (mask.terms.empty()) {
      term.op = TermOp::kSet;
    } else if (term.op == TermOp::kSet) {
      term.op = TermOp::kAnd;
    }
    if (state.paint) {
      state.paint->sources.insert(state.paint->sources.end(),
                                  a_i.sources.begin(), a_i.sources.end());
    }
    mask.terms.push_back(std::move(term));
    mask.selected = mask.terms.size() - 1;
    mask.dirty = true;
  }

  void operator()(const SetTermOp &a_i) {
    Remember();
    if (a_i.index < mask.terms.size() && a_i.index > 0) {
      mask.terms[a_i.index].op = a_i.op == TermOp::kSet ? TermOp::kAnd : a_i.op;
      mask.dirty = true;
    }
  }

  void operator()(const SetTermText &a_i) {
    Remember();
    if (a_i.index < mask.terms.size()) {
      mask.terms[a_i.index].text = a_i.text;
      mask.terms[a_i.index].label = std::string{kExpressionLabel};
      mask.terms[a_i.index].kind = RawTerm{};
      mask.dirty = true;
    }
  }

  void operator()(const SetTermKind &a_i) {
    Remember();
    if (a_i.index < mask.terms.size()) {
      if (state.paint) {
        state.paint->sources.insert(state.paint->sources.end(),
                                    a_i.sources.begin(), a_i.sources.end());
      }
      mask.terms[a_i.index].kind = a_i.kind;
      mask.terms[a_i.index].text = a_i.text;
      mask.terms[a_i.index].label = a_i.label;
      mask.dirty = true;
    }
  }

  void operator()(const RemoveTerm &a_i) {
    Remember();
    if (a_i.index >= mask.terms.size()) {
      return;
    }
    mask.terms.erase(mask.terms.begin() +
                     static_cast<std::ptrdiff_t>(a_i.index));
    if (!mask.terms.empty()) {
      mask.terms.front().op = TermOp::kSet;
    }
    RemapMask(mask, [&](std::size_t a_at) -> std::optional<std::size_t> {
      return IndexAfterRemoval(a_at, a_i.index);
    });
    mask.dirty = true;
  }

  void operator()(const MoveTerm &a_i) {
    Remember();
    const std::size_t count = mask.terms.size();
    if (a_i.from >= count || a_i.to >= count || a_i.from == a_i.to) {
      return;
    }
    Term moved = mask.terms[a_i.from];
    mask.terms.erase(mask.terms.begin() +
                     static_cast<std::ptrdiff_t>(a_i.from));
    mask.terms.insert(mask.terms.begin() + static_cast<std::ptrdiff_t>(a_i.to),
                      std::move(moved));
    for (std::size_t k = 1; k < mask.terms.size(); ++k) {
      if (mask.terms[k].op == TermOp::kSet) {
        mask.terms[k].op = TermOp::kAnd;
      }
    }
    mask.terms.front().op = TermOp::kSet;
    RemapMask(mask, [&](std::size_t a_at) -> std::optional<std::size_t> {
      return IndexAfterMove(a_at, a_i.from, a_i.to);
    });
    mask.dirty = true;
  }

  void operator()(const PickTerm &a_i) {
    if (a_i.index < mask.terms.size()) {
      mask.selected = a_i.index;
    }
  }

  void operator()(const SoloTerm &a_i) {
    if (a_i.index >= mask.terms.size()) {
      return;
    }
    if (a_i.on) {
      mask.solo = a_i.index;
    } else if (mask.solo == a_i.index) {
      mask.solo.reset();
    }
    mask.dirty = true;
  }

  void operator()(const SetPeek &a_i) {
    if (!state.paint) {
      return;
    }
    state.paint->peek = a_i.target;
    mask.dirty = true;
  }

  void operator()(const MuteTerm &a_i) {
    if (a_i.index >= mask.terms.size()) {
      return;
    }
    if (a_i.on) {
      mask.muted.insert(a_i.index);
    } else {
      mask.muted.erase(a_i.index);
    }
    mask.dirty = true;
  }

  void operator()(const LoadMask &a_i) {
    Remember();
    mask = MaskStack{};
    const std::size_t kept = std::min(a_i.terms.size(), kMaxTerms);
    mask.terms.assign(a_i.terms.begin(),
                      a_i.terms.begin() + static_cast<std::ptrdiff_t>(kept));
    if (!mask.terms.empty()) {
      mask.terms.front().op = TermOp::kSet;
      mask.selected = 0;
    }
    mask.editing = a_i.editing;
    mask.dirty = !mask.terms.empty();
  }

  void operator()(const ClearMask &) {
    Remember();
    const std::string editing = mask.editing;
    mask = MaskStack{};
    mask.editing = editing;
    mask.dirty = true;
  }

  void operator()(const UndoMask &) {
    if (std::optional<MaskStack> past = state.maskHistory.Undo(mask)) {
      mask = std::move(*past);
      mask.dirty = true;
    }
  }

  void operator()(const RedoMask &) {
    if (std::optional<MaskStack> next = state.maskHistory.Redo(mask)) {
      mask = std::move(*next);
      mask.dirty = true;
    }
  }

  void operator()(const BeginPaint &a_i) {
    state.paint = PaintSession{};
    state.paint->recipeID = a_i.recipeID;
    state.paint->surface = a_i.surface;
    state.paint->sessionID = a_i.sessionID;
    state.paint->origin = selection;
    state.paint->previewGeometry = selection.geometry;
    state.paint->origin.recipeID = a_i.recipeID;
    state.paint->origin.document = true;
    state.paint->originScroll = state.navigation.scroll;
    state.paint->assignment = a_i.assignment;
    state.paint->createdMask = a_i.createdMask;
    selection.document = true;
    mask.dirty = true;
    state.maskHistory.Clear();
  }

  void operator()(const SetPaintSurface &a_i) {
    if (state.paint) {
      state.paint->surface = a_i.surface;
    }
  }

  void operator()(const KeepPaint &a_i) {
    if (state.paint && state.paint->recipeID == a_i.request.recipeID) {
      state.paint->pendingCommit = a_i.request.id;
      state.paint->problem.reset();
    }
  }

  void operator()(const EndPaint &) { EndSession(); }

  void operator()(const UpdatePaint &a_i) {
    if (state.paint) {
      state.paint->pendingRevision = a_i.request.revision;
      state.paint->problem.reset();
    }
  }

  void operator()(const EditRecipe &a_i) {
    if (selection.recipeID == a_i.recipeID) {
      for (const RecipeEdit &edit : a_i.edits) {
        ReduceEdit(selection, edit);
      }
    }
    if (ShouldInvalidateIndexedSubjects(a_i.edits)) {
      InvalidateAssignment(a_i.recipeID);
      InvalidateIndexedSubjects(state.navigation, selection, a_i.recipeID);
      InvalidatePreviewPin(state.previewPin, a_i.recipeID);
    }
  }

  void operator()(const RenameRecipe &a_i) {
    if (selection.recipeID == a_i.from) {
      state.navigation = Navigation{};
      selection.recipeID = a_i.to;
    }
    if (state.paint && state.paint->recipeID == a_i.from) {
      state.paint->recipeID = a_i.to;
      state.paint->origin.recipeID = a_i.to;
    }
  }

  void operator()(const CreateRecipe &a_i) { FocusRecipe(a_i.recipeID); }

  void operator()(const DuplicateRecipe &a_i) { FocusRecipe(a_i.to); }

  void operator()(const DeleteRecipe &a_i) {
    if (state.paint && state.paint->recipeID == a_i.recipeID) {
      operator()(SetMode{Mode::kCompose});
    }
    if (selection.recipeID == a_i.recipeID) {
      state.previewPin.reset();
      state.navigation = Navigation{};
      selection.subject = RecipeSubject{};
      selection.property.reset();
      state.revealedProperty.reset();
    }
  }

  void operator()(const SoloRecipe &) {}
  void operator()(const SoloPiece &) {}
  void operator()(const SoloOutput &) {}
  void operator()(const SoloLayer &) {}
  void operator()(const MuteLayer &) {}
  void operator()(const SetFreeze &) {}
  void operator()(const SetScrub &) {}
  void operator()(const SetSpeed &) {}
  void operator()(const StepClock &) {}
  void operator()(const Undo &a_i) {
    InvalidateAssignment(a_i.recipeID);
    InvalidateIndexedSubjects(state.navigation, selection, a_i.recipeID);
    InvalidatePreviewPin(state.previewPin, a_i.recipeID);
  }
  void operator()(const Redo &a_i) {
    InvalidateAssignment(a_i.recipeID);
    InvalidateIndexedSubjects(state.navigation, selection, a_i.recipeID);
    InvalidatePreviewPin(state.previewPin, a_i.recipeID);
  }
  void operator()(const FireTrigger &) {}
};
}

SourceCatalog PaintSources(const MenuState &a_state, const RecipeRow &a_recipe,
                           const Intents &a_pending) {
  SourceCatalog catalog = SourceCatalogOf(a_recipe);
  const auto append = [&](const std::vector<RecipeEdit> &a_edits) {
    for (const RecipeEdit &edit : a_edits) {
      if (const auto *source = Get<AddSource>(edit)) {
        catalog.sources.push_back(Source{source->name, source->kind});
        catalog.reservedNames.push_back(source->name);
      }
    }
  };
  if (a_state.paint) {
    append(a_state.paint->sources);
  }
  for (const Intent &intent : a_pending) {
    Match(
        intent, [&](const AddTerm &a_i) { append(a_i.sources); },
        [&](const SetTermKind &a_i) { append(a_i.sources); },
        [](const SetMode &) {}, [](const PickPiece &) {},
        [](const PickRecipe &) {}, [](const PinRecipe &) {},
        [](const PickTarget &) {}, [](const PickSlot &) {},
        [](const PickCell &) {}, [](const PickLayer &) {},
        [](const ViewGeometry &) {}, [](const SetStackSplit &) {},
        [](const SetWorkspaceSplit &) {}, [](const ShowSettings &) {},
        [](const ShowResource &) {}, [](const ReadMesh &) {},
        [](const SetTermOp &) {}, [](const SetTermText &) {},
        [](const RemoveTerm &) {}, [](const MoveTerm &) {},
        [](const PickTerm &) {}, [](const SoloTerm &) {},
        [](const MuteTerm &) {}, [](const SetPeek &) {},
        [](const LoadMask &) {}, [](const ClearMask &) {},
        [](const UndoMask &) {}, [](const RedoMask &) {},
        [](const BeginPaint &) {}, [](const SetPaintSurface &) {},
        [](const KeepPaint &) {}, [](const EndPaint &) {},
        [](const UpdatePaint &) {}, [](const EditRecipe &) {},
        [](const SoloRecipe &) {}, [](const SoloPiece &) {},
        [](const SoloOutput &) {}, [](const SoloLayer &) {},
        [](const MuteLayer &) {}, [](const SetFreeze &) {},
        [](const SetScrub &) {}, [](const SetSpeed &) {},
        [](const StepClock &) {}, [](const Undo &) {}, [](const Redo &) {},
        [](const CreateRecipe &) {}, [](const RenameRecipe &) {},
        [](const DeleteRecipe &) {}, [](const DuplicateRecipe &) {},
        [](const FireTrigger &) {});
  }
  return catalog;
}

namespace {
[[nodiscard]] bool ChangesPaint(const Intent &a_intent) {
  return Match(
      a_intent, [](const AddTerm &) { return true; },
      [](const SetTermKind &) { return true; },
      [](const SetTermText &) { return true; },
      [](const SetTermOp &) { return true; },
      [](const RemoveTerm &) { return true; },
      [](const MoveTerm &) { return true; },
      [](const SoloTerm &) { return true; },
      [](const MuteTerm &) { return true; },
      [](const SetPeek &) { return true; },
      [](const LoadMask &) { return true; },
      [](const ClearMask &) { return true; },
      [](const UndoMask &) { return true; },
      [](const RedoMask &) { return true; },
      [](const SetPaintSurface &) { return true; },
      [](const SetMode &) { return false; },
      [](const PickPiece &) { return false; },
      [](const PickRecipe &) { return false; },
      [](const PinRecipe &) { return false; },
      [](const PickTarget &) { return false; },
      [](const PickSlot &) { return false; },
      [](const PickCell &) { return false; },
      [](const PickLayer &) { return false; },
      [](const ViewGeometry &) { return false; },
      [](const SetStackSplit &) { return false; },
      [](const SetWorkspaceSplit &) { return false; },
      [](const ShowSettings &) { return false; },
      [](const ShowResource &) { return false; },
      [](const ReadMesh &) { return false; },
      [](const PickTerm &) { return false; },
      [](const BeginPaint &) { return false; },
      [](const KeepPaint &) { return false; },
      [](const EndPaint &) { return false; },
      [](const UpdatePaint &) { return false; },
      [](const EditRecipe &) { return false; },
      [](const SoloRecipe &) { return false; },
      [](const SoloPiece &) { return false; },
      [](const SoloOutput &) { return false; },
      [](const SoloLayer &) { return false; },
      [](const MuteLayer &) { return false; },
      [](const SetFreeze &) { return false; },
      [](const SetScrub &) { return false; },
      [](const SetSpeed &) { return false; },
      [](const StepClock &) { return false; },
      [](const Undo &) { return false; }, [](const Redo &) { return false; },
      [](const CreateRecipe &) { return false; },
      [](const RenameRecipe &) { return false; },
      [](const DeleteRecipe &) { return false; },
      [](const DuplicateRecipe &) { return false; },
      [](const FireTrigger &) { return false; });
}

[[nodiscard]] bool RequiresSettledEditor(const Intent &a_intent) {
  return Is<EditRecipe>(a_intent) || Is<Undo>(a_intent) || Is<Redo>(a_intent) ||
         Is<CreateRecipe>(a_intent) || Is<RenameRecipe>(a_intent) ||
         Is<DeleteRecipe>(a_intent) || Is<DuplicateRecipe>(a_intent) ||
         Is<BeginPaint>(a_intent) || Is<KeepPaint>(a_intent) ||
         Is<UpdatePaint>(a_intent) || Is<SoloOutput>(a_intent) ||
         Is<SoloLayer>(a_intent) || Is<MuteLayer>(a_intent) ||
         Is<PickCell>(a_intent) || Is<PickLayer>(a_intent) ||
         ChangesPaint(a_intent);
}

[[nodiscard]] bool EditorSettled(const MenuState &a_state) {
  const bool editorPending = a_state.pendingIndexedEdit ||
                             a_state.pendingRecipeFile ||
                             a_state.pendingEditorChange;
  const bool commitPending = a_state.paint && a_state.paint->pendingCommit;
  return !editorPending && !commitPending;
}

[[nodiscard]] bool AcceptBeginPaint(const MenuState &a_state,
                                    const BeginPaint &a_begin) {
  return a_state.mode == Mode::kPaint && !a_state.paint &&
         a_begin.recipeID != kPaintRecipe &&
         a_begin.resetID == a_state.lastPaintReset;
}

[[nodiscard]] bool AcceptKeepPaint(const MenuState &a_state,
                                   const KeepPaint &a_keep) {
  return a_state.paint && a_state.paint->ready &&
         !a_state.paint->pendingCommit &&
         a_state.paint->sessionID == a_keep.request.sessionID &&
         a_state.paint->recipeID == a_keep.request.recipeID;
}

[[nodiscard]] bool AcceptUpdatePaint(const MenuState &a_state,
                                     const UpdatePaint &a_update) {
  return a_state.paint && a_state.paint->ready &&
         !a_state.paint->pendingRevision && !a_state.paint->pendingCommit &&
         a_state.paint->sessionID == a_update.request.sessionID;
}

[[nodiscard]] bool AcceptEndPaint(const MenuState &a_state) {
  return !a_state.paint || !a_state.paint->pendingCommit;
}

struct AcceptVisitor {
  const MenuState &state;

  [[nodiscard]] bool operator()(const BeginPaint &a_begin) const {
    return AcceptBeginPaint(state, a_begin);
  }
  [[nodiscard]] bool operator()(const KeepPaint &a_keep) const {
    return AcceptKeepPaint(state, a_keep);
  }
  [[nodiscard]] bool operator()(const UpdatePaint &a_update) const {
    return AcceptUpdatePaint(state, a_update);
  }
  [[nodiscard]] bool operator()(const EndPaint &) const {
    return AcceptEndPaint(state);
  }
  [[nodiscard]] bool operator()(const SetMode &) const { return true; }
  [[nodiscard]] bool operator()(const PickPiece &) const { return true; }
  [[nodiscard]] bool operator()(const PickRecipe &) const { return true; }
  [[nodiscard]] bool operator()(const PinRecipe &) const { return true; }
  [[nodiscard]] bool operator()(const PickTarget &) const { return true; }
  [[nodiscard]] bool operator()(const PickSlot &) const { return true; }
  [[nodiscard]] bool operator()(const PickCell &) const { return true; }
  [[nodiscard]] bool operator()(const PickLayer &) const { return true; }
  [[nodiscard]] bool operator()(const ViewGeometry &) const { return true; }
  [[nodiscard]] bool operator()(const SetStackSplit &) const { return true; }
  [[nodiscard]] bool operator()(const SetWorkspaceSplit &) const {
    return true;
  }
  [[nodiscard]] bool operator()(const ShowSettings &) const { return true; }
  [[nodiscard]] bool operator()(const ShowResource &) const { return true; }
  [[nodiscard]] bool operator()(const ReadMesh &) const { return true; }
  [[nodiscard]] bool operator()(const AddTerm &) const { return true; }
  [[nodiscard]] bool operator()(const SetTermOp &) const { return true; }
  [[nodiscard]] bool operator()(const SetTermText &) const { return true; }
  [[nodiscard]] bool operator()(const SetTermKind &) const { return true; }
  [[nodiscard]] bool operator()(const RemoveTerm &) const { return true; }
  [[nodiscard]] bool operator()(const MoveTerm &) const { return true; }
  [[nodiscard]] bool operator()(const PickTerm &) const { return true; }
  [[nodiscard]] bool operator()(const SoloTerm &) const { return true; }
  [[nodiscard]] bool operator()(const MuteTerm &) const { return true; }
  [[nodiscard]] bool operator()(const SetPeek &) const { return true; }
  [[nodiscard]] bool operator()(const LoadMask &) const { return true; }
  [[nodiscard]] bool operator()(const ClearMask &) const { return true; }
  [[nodiscard]] bool operator()(const UndoMask &) const { return true; }
  [[nodiscard]] bool operator()(const RedoMask &) const { return true; }
  [[nodiscard]] bool operator()(const SetPaintSurface &) const { return true; }
  [[nodiscard]] bool operator()(const EditRecipe &) const { return true; }
  [[nodiscard]] bool operator()(const SoloRecipe &) const { return true; }
  [[nodiscard]] bool operator()(const SoloPiece &) const { return true; }
  [[nodiscard]] bool operator()(const SoloOutput &) const { return true; }
  [[nodiscard]] bool operator()(const SoloLayer &) const { return true; }
  [[nodiscard]] bool operator()(const MuteLayer &) const { return true; }
  [[nodiscard]] bool operator()(const SetFreeze &) const { return true; }
  [[nodiscard]] bool operator()(const SetScrub &) const { return true; }
  [[nodiscard]] bool operator()(const SetSpeed &) const { return true; }
  [[nodiscard]] bool operator()(const StepClock &) const { return true; }
  [[nodiscard]] bool operator()(const Undo &) const { return true; }
  [[nodiscard]] bool operator()(const Redo &) const { return true; }
  [[nodiscard]] bool operator()(const CreateRecipe &) const { return true; }
  [[nodiscard]] bool operator()(const RenameRecipe &) const { return true; }
  [[nodiscard]] bool operator()(const DeleteRecipe &) const { return true; }
  [[nodiscard]] bool operator()(const DuplicateRecipe &) const { return true; }
  [[nodiscard]] bool operator()(const FireTrigger &) const { return true; }
};

[[nodiscard]] std::size_t SourceRoom(const MenuState &a_state) {
  const std::size_t used = a_state.paint ? a_state.paint->sources.size() : 0;
  return kMaxRecipeRows - std::min(kMaxRecipeRows, used);
}

struct MaskLimitVisitor {
  const MenuState &state;

  [[nodiscard]] bool operator()(const AddTerm &a_add) const {
    return state.mask.terms.size() >= kMaxTerms ||
           (state.paint && a_add.sources.size() > SourceRoom(state));
  }
  [[nodiscard]] bool operator()(const SetTermKind &a_set) const {
    return state.paint && a_set.sources.size() > SourceRoom(state);
  }
  [[nodiscard]] bool operator()(const LoadMask &a_load) const {
    return a_load.terms.size() > kMaxTerms;
  }
  [[nodiscard]] bool operator()(const SetMode &) const { return false; }
  [[nodiscard]] bool operator()(const PickPiece &) const { return false; }
  [[nodiscard]] bool operator()(const PickRecipe &) const { return false; }
  [[nodiscard]] bool operator()(const PinRecipe &) const { return false; }
  [[nodiscard]] bool operator()(const PickTarget &) const { return false; }
  [[nodiscard]] bool operator()(const PickSlot &) const { return false; }
  [[nodiscard]] bool operator()(const PickCell &) const { return false; }
  [[nodiscard]] bool operator()(const PickLayer &) const { return false; }
  [[nodiscard]] bool operator()(const ViewGeometry &) const { return false; }
  [[nodiscard]] bool operator()(const SetStackSplit &) const { return false; }
  [[nodiscard]] bool operator()(const SetWorkspaceSplit &) const {
    return false;
  }
  [[nodiscard]] bool operator()(const ShowSettings &) const { return false; }
  [[nodiscard]] bool operator()(const ShowResource &) const { return false; }
  [[nodiscard]] bool operator()(const ReadMesh &) const { return false; }
  [[nodiscard]] bool operator()(const SetTermOp &) const { return false; }
  [[nodiscard]] bool operator()(const SetTermText &) const { return false; }
  [[nodiscard]] bool operator()(const RemoveTerm &) const { return false; }
  [[nodiscard]] bool operator()(const MoveTerm &) const { return false; }
  [[nodiscard]] bool operator()(const PickTerm &) const { return false; }
  [[nodiscard]] bool operator()(const SoloTerm &) const { return false; }
  [[nodiscard]] bool operator()(const MuteTerm &) const { return false; }
  [[nodiscard]] bool operator()(const SetPeek &) const { return false; }
  [[nodiscard]] bool operator()(const ClearMask &) const { return false; }
  [[nodiscard]] bool operator()(const UndoMask &) const { return false; }
  [[nodiscard]] bool operator()(const RedoMask &) const { return false; }
  [[nodiscard]] bool operator()(const BeginPaint &) const { return false; }
  [[nodiscard]] bool operator()(const SetPaintSurface &) const { return false; }
  [[nodiscard]] bool operator()(const KeepPaint &) const { return false; }
  [[nodiscard]] bool operator()(const EndPaint &) const { return false; }
  [[nodiscard]] bool operator()(const UpdatePaint &) const { return false; }
  [[nodiscard]] bool operator()(const EditRecipe &) const { return false; }
  [[nodiscard]] bool operator()(const SoloRecipe &) const { return false; }
  [[nodiscard]] bool operator()(const SoloPiece &) const { return false; }
  [[nodiscard]] bool operator()(const SoloOutput &) const { return false; }
  [[nodiscard]] bool operator()(const SoloLayer &) const { return false; }
  [[nodiscard]] bool operator()(const MuteLayer &) const { return false; }
  [[nodiscard]] bool operator()(const SetFreeze &) const { return false; }
  [[nodiscard]] bool operator()(const SetScrub &) const { return false; }
  [[nodiscard]] bool operator()(const SetSpeed &) const { return false; }
  [[nodiscard]] bool operator()(const StepClock &) const { return false; }
  [[nodiscard]] bool operator()(const Undo &) const { return false; }
  [[nodiscard]] bool operator()(const Redo &) const { return false; }
  [[nodiscard]] bool operator()(const CreateRecipe &) const { return false; }
  [[nodiscard]] bool operator()(const RenameRecipe &) const { return false; }
  [[nodiscard]] bool operator()(const DeleteRecipe &) const { return false; }
  [[nodiscard]] bool operator()(const DuplicateRecipe &) const { return false; }
  [[nodiscard]] bool operator()(const FireTrigger &) const { return false; }
};

[[nodiscard]] bool ExceedsMaskLimits(const MenuState &a_state,
                                     const Intent &a_intent) {
  return Match(a_intent, MaskLimitVisitor{a_state});
}

void ReportMaskLimit(MenuState &a_state) {
  if (a_state.paint) {
    a_state.paint->problem = MakeDiagnostic(
        Severity::kError, "paint", "the mask term or source limit was reached");
  }
}

void MarkPaintChanged(MenuState &a_state) {
  if (a_state.paint) {
    ++a_state.paint->revision;
    a_state.mask.dirty = true;
    a_state.paint->problem.reset();
  }
}
}

bool AcceptIntent(const MenuState &a_state, const Intent &a_intent) {
  if (!EditorSettled(a_state) && RequiresSettledEditor(a_intent)) {
    return false;
  }
  return Match(a_intent, AcceptVisitor{a_state});
}

void ResolveEditorSelection(MenuState &a_state, const Snapshot &a_snapshot) {
  if (!a_state.selection.property) {
    a_state.revealedProperty.reset();
  }
  const Selection previous = a_state.selection;
  ResolveSelection(a_state.selection, a_snapshot);
  if (previous.piece != a_state.selection.piece ||
      previous.recipeID != a_state.selection.recipeID) {
    a_state.navigation = Navigation{};
    a_state.selection.subject = RecipeSubject{};
    a_state.selection.property.reset();
    a_state.revealedProperty.reset();
    a_state.selection.layer.reset();
  } else if (previous.geometry != a_state.selection.geometry) {
    a_state.navigation = Navigation{};
  }
  ResolvePreviewPin(a_state.previewPin, a_state.selection,
                    SelectedRecipe(a_snapshot, a_state.selection),
                    a_state.lastPaintReset);
}

namespace {
std::optional<InspectorSubject>
RenamedSubject(const RecipeEdit &a_edit, const InspectorSubject &a_subject) {
  if (const auto *rename = Get<RenameSignal>(a_edit);
      rename && a_subject == InspectorSubject{SignalSubject{rename->from}}) {
    return SignalSubject{rename->to};
  }
  if (const auto *rename = Get<RenameSource>(a_edit);
      rename && a_subject == InspectorSubject{SourceSubject{rename->from}}) {
    return SourceSubject{rename->to};
  }
  if (const auto *rename = Get<RenameMask>(a_edit);
      rename && a_subject == InspectorSubject{MaskSubject{rename->from}}) {
    return MaskSubject{rename->to};
  }
  if (const auto *rename = Get<RenameCurve>(a_edit);
      rename && a_subject == InspectorSubject{CurveSubject{rename->from}}) {
    return CurveSubject{rename->to};
  }
  return std::nullopt;
}
}

void TrackEditorChange(MenuState &a_state, std::uint64_t a_request,
                       const Intent &a_intent) {
  std::string recipe;
  if (const auto *rename = Get<RenameRecipe>(a_intent)) {
    recipe = rename->from;
  } else if (const auto *remove = Get<DeleteRecipe>(a_intent)) {
    recipe = remove->recipeID;
  } else if (const auto *edit = Get<EditRecipe>(a_intent);
             edit && std::ranges::any_of(edit->edits, [&](const RecipeEdit &e) {
               return RenamedSubject(e, a_state.selection.subject).has_value();
             })) {
    recipe = edit->recipeID;
  } else {
    return;
  }
  a_state.pendingEditorChange =
      PendingEditorChange{a_request, recipe, a_intent, a_state.selection};
}

namespace {
void FollowEditorChange(MenuState &a_state,
                        const PendingEditorChange &a_pending) {
  const auto *edit = Get<EditRecipe>(a_pending.intent);
  if (!edit) {
    Reduce(a_state, a_pending.intent);
    return;
  }
  if (a_state.selection.recipeID != a_pending.recipeID ||
      a_state.selection.subject != a_pending.selection.subject) {
    return;
  }
  InspectorSubject subject = a_pending.selection.subject;
  for (const RecipeEdit &change : edit->edits) {
    if (const auto renamed = RenamedSubject(change, subject)) {
      subject = *renamed;
    }
  }
  a_state.pendingSelection = std::move(subject);
}

void AcknowledgeEditorChange(MenuState &a_state,
                             const RecipeEditResult &a_result) {
  if (!a_state.pendingEditorChange ||
      a_state.pendingEditorChange->requestID != a_result.requestID ||
      a_state.pendingEditorChange->recipeID != a_result.recipeID) {
    return;
  }
  const PendingEditorChange pending = std::move(*a_state.pendingEditorChange);
  a_state.pendingEditorChange.reset();
  if (!a_result.error) {
    FollowEditorChange(a_state, pending);
  }
}
}

void AcknowledgeEditorOperations(MenuState &a_state,
                                 const Snapshot &a_snapshot) {
  for (const RecipeEditResult &result : a_snapshot.editResults) {
    (void)AcknowledgeIndexedEdit(a_state.pendingIndexedEdit, &result);
    AcknowledgeEditorChange(a_state, result);
  }
  for (const FileOperationResult &result : a_snapshot.fileOperations) {
    if (result.state != FileOperationState::kPending &&
        a_state.pendingRecipeFile &&
        a_state.pendingRecipeFile->requestID == result.requestID &&
        a_state.pendingRecipeFile->recipeID == result.recipeID) {
      if (result.action == FileAction::kRevert) {
        InvalidatePreviewPin(a_state.previewPin, result.recipeID);
      }
      a_state.pendingRecipeFile.reset();
    }
  }
}

void Reduce(MenuState &a_state, const Intent &a_intent) {
  if (!AcceptIntent(a_state, a_intent)) {
    return;
  }
  if (ExceedsMaskLimits(a_state, a_intent)) {
    ReportMaskLimit(a_state);
    return;
  }
  Match(a_intent, ReduceVisitor{a_state});
  if (ChangesPaint(a_intent)) {
    MarkPaintChanged(a_state);
  }
}

std::vector<RecipeEdit> PaintDependencies(const MenuState &a_state) {
  if (!a_state.paint) {
    return {};
  }
  std::set<std::string> reads;
  for (const Term &term : a_state.mask.terms) {
    const auto program = Program::Parse(term.text);
    if (!program) {
      return a_state.paint->sources;
    }
    for (const std::string &name : program->References()) {
      reads.insert(name);
    }
  }
  if (a_state.paint->peek) {
    const auto program = Program::Parse(a_state.paint->peek->expression);
    if (!program) {
      return a_state.paint->sources;
    }
    for (const std::string &name : program->References()) {
      reads.insert(name);
    }
  }
  std::vector<RecipeEdit> sources;
  for (const RecipeEdit &edit : a_state.paint->sources) {
    if (const auto *source = Get<AddSource>(edit);
        source && reads.contains(source->name)) {
      sources.push_back(edit);
    }
  }
  return sources;
}

std::optional<UpdatePaint> PendingPaintUpdate(const MenuState &a_state) {
  if (!a_state.paint || !a_state.paint->ready || !a_state.mask.dirty ||
      a_state.paint->pendingRevision || a_state.paint->pendingCommit ||
      a_state.paint->problem) {
    return std::nullopt;
  }
  const PaintSession &paint = *a_state.paint;
  const auto built = CheckedBuildMask(a_state.mask.terms, a_state.mask.solo,
                                      a_state.mask.muted);
  if (!built) {
    return std::nullopt;
  }
  std::string expression = *built;
  if (expression.empty()) {
    expression = "0";
  }
  std::string peek;
  std::vector<RecipeEdit> peekSources;
  if (paint.peek) {
    peek = paint.peek->expression;
    peekSources = paint.peek->sources;
  }
  return UpdatePaint{PaintUpdateRequest{.sessionID = paint.sessionID,
                                        .revision = paint.revision,
                                        .expression = std::move(expression),
                                        .sources = PaintDependencies(a_state),
                                        .peek = std::move(peek),
                                        .peekSources = std::move(peekSources),
                                        .surface = paint.surface}};
}

bool MaskTaskActive(const MenuState &a_state) {
  return a_state.paint.has_value() && a_state.mode == Mode::kPaint;
}

bool CanKeepMask(const MenuState &a_state) {
  const auto expression = CheckedBuildMask(a_state.mask.terms);
  return a_state.paint && a_state.paint->ready &&
         !a_state.paint->pendingCommit && expression && !expression->empty();
}

std::array<RuleButton, 4> MaskRuleButtonsOf(const MenuState &a_state) {
  return {{
      {RuleAction::kUndo,
       {},
       Width::Fit(),
       a_state.maskHistory.UndoDepth() > 0},
      {RuleAction::kRedo,
       {},
       Width::Fit(),
       a_state.maskHistory.RedoDepth() > 0},
      {RuleAction::kClear, {}, Width::Fit(), !a_state.mask.terms.empty()},
      {RuleAction::kAdd, "Keep", Width::Fit(), CanKeepMask(a_state)},
  }};
}

std::string MaskProblemsOf(const MenuState &a_state) {
  std::string problems;
  if (const auto expression = CheckedBuildMask(a_state.mask.terms);
      !expression) {
    problems = expression.error().message;
  }
  if (a_state.paint && a_state.paint->problem) {
    if (!problems.empty()) {
      problems += '\n';
    }
    problems += a_state.paint->problem->message;
  }
  return problems;
}

void ReconcilePaintMode(MenuState &a_state) {
  const auto *subject = Get<MaskSubject>(a_state.selection.subject);
  const bool onDraft = a_state.paint && subject &&
                       !a_state.mask.editing.empty() &&
                       subject->name == a_state.mask.editing;
  const Mode target = onDraft ? Mode::kPaint : Mode::kCompose;
  if (a_state.mode == target) {
    return;
  }
  const float split = a_state.layout.stackSplit;
  a_state.mode = target;
  a_state.layout = LayoutFor(target);
  a_state.layout.stackSplit = split;
  if (target == Mode::kPaint) {
    a_state.resource = ResourceTab::kMasks;
  }
}

void ObservePaintRecipe(MenuState &a_state, const RecipeRow *a_recipe) {
  if (!a_state.paint || !a_state.paint->ready) {
    return;
  }
  if (a_recipe && a_recipe->id == kPaintRecipe) {
    a_state.paint->projected = true;
  }
  if (a_recipe && a_recipe->id == a_state.paint->recipeID &&
      a_state.paint->assignment &&
      a_recipe->documentRevision !=
          a_state.paint->assignment->documentRevision) {
    a_state.paint->assignment.reset();
    a_state.paint->assignmentInvalid = true;
    a_state.paint->origin.subject = RecipeSubject{};
    a_state.paint->origin.property.reset();
    a_state.paint->origin.layer.reset();
  }
}

void AcknowledgePaintUpdate(MenuState &a_state,
                            const PaintUpdateResult &a_result) {
  if (a_result.ended) {
    if (a_result.revision > a_state.lastPaintReset) {
      a_state.lastPaintReset = a_result.revision;
      a_state.previewPin.reset();
      a_state.navigation = Navigation{};
      a_state.selection.subject = RecipeSubject{};
      a_state.selection.property.reset();
      a_state.revealedProperty.reset();
      a_state.selection.layer.reset();
      if (a_state.paint) {
        a_state.paint->origin = a_state.selection;
        a_state.paint->originScroll = 0.0f;
        a_state.paint->pendingCommit.reset();
        Reduce(a_state, EndPaint{});
      }
    }
    return;
  }
  if (!a_state.paint || a_state.paint->sessionID != a_result.sessionID) {
    return;
  }
  PaintSession &paint = *a_state.paint;
  if (a_result.revision == 0) {
    if (paint.ready || paint.problem) {
      return;
    }
    paint.problem = a_result.problem;
    paint.ready = !a_result.problem;
    return;
  }
  if (paint.pendingRevision != a_result.revision) {
    return;
  }
  paint.pendingRevision.reset();
  paint.problem =
      paint.revision == a_result.revision ? a_result.problem : std::nullopt;
  if (!a_result.problem && paint.revision == a_result.revision) {
    a_state.mask.dirty = false;
  }
}

void AcknowledgePaintCommit(MenuState &a_state,
                            const PaintCommitResult &a_result) {
  if (!a_state.paint || a_state.paint->pendingCommit != a_result.requestID) {
    return;
  }
  if (a_result.problem) {
    a_state.paint->pendingCommit.reset();
    a_state.paint->problem = a_result.problem;
    return;
  }
  a_state.paint->pendingCommit.reset();
  Reduce(a_state, EndPaint{});
}
}
