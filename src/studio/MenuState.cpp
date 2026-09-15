#include "studio/MenuState.h"

#include "studio/Intent.h"
#include "studio/View.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
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
void ReduceEdit(Selection &a_selection, const RecipeEdit &a_edit) {
  Match(
      a_edit,
      [&](const AddLayer &a_e) {
        if (a_e.at) {
          a_selection.layer = *a_e.at;
        }
      },
      [&](const RemoveLayer &a_e) {
        if (!a_selection.layer) {
          return;
        }
        if (*a_selection.layer == a_e.layer) {
          a_selection.layer.reset();
        } else if (*a_selection.layer > a_e.layer) {
          --*a_selection.layer;
        }
      },
      [&](const MoveLayer &a_e) {
        if (!a_selection.layer) {
          return;
        }
        const std::size_t at = *a_selection.layer;
        if (at == a_e.from) {
          a_selection.layer = a_e.to;
        } else if (a_e.from < at && at <= a_e.to) {
          --*a_selection.layer;
        } else if (a_e.to <= at && at < a_e.from) {
          ++*a_selection.layer;
        }
      },
      [&](const ClearLayers &) { a_selection.layer.reset(); },
      [&](const RemoveOutput &) { a_selection.layer.reset(); },
      [&](const AddOutput &a_e) {
        a_selection.target = TargetOf(a_e.surface);
        a_selection.slot = a_e.slot;
        a_selection.layer.reset();
      },
      [](const SetLayerSource &) {}, [](const SetLayerCurve &) {},
      [](const SetLayerBlend &) {}, [](const SetLayerOpacity &) {},
      [](const SetLayerColor &) {}, [](const SetLayerMask &) {},
      [](const SetLayerChannels &) {}, [](const SetScalar &) {},
      [](const SetColorScalar &) {}, [](const SetOutputReplace &) {},
      [](const SetOutputSelector &) {}, [](const AddKey &) {},
      [](const RemoveKey &) {}, [](const ClearOutputs &) {},
      [](const ClearResources &) {}, [](const ClearRecipe &) {},
      [](const SetPriority &) {}, [](const SetClockSpeed &) {},
      [](const SetConstant &) {}, [](const SetExpression &) {},
      [](const SetSignal &) {}, [](const SetSignalCurve &) {},
      [](const SetCurve &) {}, [](const SetMask &) {}, [](const AddSignal &) {},
      [](const AddCurve &) {}, [](const RenameSignal &) {},
      [](const RenameCurve &) {}, [](const RemoveSignal &) {},
      [](const RemoveCurve &) {}, [](const AddMask &) {},
      [](const RenameMask &) {}, [](const RemoveMask &) {},
      [](const AddSource &) {}, [](const SetSource &) {},
      [](const RenameSource &) {}, [](const RemoveSource &) {},
      [](const AddLight &) {}, [](const SetLightParam &) {},
      [](const SetLightVector &) {}, [](const SetLightShadow &) {},
      [](const SetLightBones &) {}, [](const SetLightReplace &) {},
      [](const SetLightSelector &) {}, [](const ResetLight &) {},
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

  void endSession() {
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

  void remember() { state.maskHistory.Push(mask); }

  void invalidateAssignment(const std::string &a_recipeID) {
    if (state.paint && state.paint->recipeID == a_recipeID &&
        state.paint->assignment) {
      state.paint->assignment.reset();
      state.paint->assignmentInvalid = true;
      state.paint->origin.subject = RecipeSubject{};
      state.paint->origin.property.reset();
      state.paint->origin.layer.reset();
    }
  }

  void pickRecipe(const std::string &a_id) {
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
    pickRecipe(a_i.recipeID);
    selection.document = a_i.document;
  }

  void operator()(const PinRecipe &a_i) {
    pickRecipe(a_i.recipeID);
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
    remember();
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
    remember();
    if (a_i.index < mask.terms.size() && a_i.index > 0) {
      mask.terms[a_i.index].op = a_i.op == TermOp::kSet ? TermOp::kAnd : a_i.op;
      mask.dirty = true;
    }
  }

  void operator()(const SetTermText &a_i) {
    remember();
    if (a_i.index < mask.terms.size()) {
      mask.terms[a_i.index].text = a_i.text;
      mask.terms[a_i.index].label = std::string{kExpressionLabel};
      mask.terms[a_i.index].kind = RawTerm{};
      mask.dirty = true;
    }
  }

  void operator()(const SetTermKind &a_i) {
    remember();
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
    remember();
    if (a_i.index >= mask.terms.size()) {
      return;
    }
    mask.terms.erase(mask.terms.begin() +
                     static_cast<std::ptrdiff_t>(a_i.index));
    if (!mask.terms.empty()) {
      mask.terms.front().op = TermOp::kSet;
    }
    RemapMask(mask, [&](std::size_t a_at) -> std::optional<std::size_t> {
      if (a_at == a_i.index) {
        return std::nullopt;
      }
      return a_at > a_i.index ? a_at - 1 : a_at;
    });
    mask.dirty = true;
  }

  void operator()(const MoveTerm &a_i) {
    remember();
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
      if (a_at == a_i.from) {
        return a_i.to;
      }
      if (a_i.from < a_at && a_at <= a_i.to) {
        return a_at - 1;
      }
      if (a_i.to <= a_at && a_at < a_i.from) {
        return a_at + 1;
      }
      return a_at;
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
    remember();
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
    remember();
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

  void operator()(const EndPaint &) { endSession(); }

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
      invalidateAssignment(a_i.recipeID);
      InvalidateIndexedSubjects(state.navigation, selection, a_i.recipeID);
      InvalidatePreviewPin(state.previewPin, a_i.recipeID);
    }
  }

  void operator()(const RenameRecipe &a_i) {
    state.navigation = Navigation{};
    if (selection.recipeID == a_i.from) {
      selection.recipeID = a_i.to;
    }
    if (state.paint && state.paint->recipeID == a_i.from) {
      state.paint->recipeID = a_i.to;
      state.paint->origin.recipeID = a_i.to;
    }
  }

  void operator()(const CreateRecipe &a_i) {
    state.previewPin.reset();
    if (state.paint) {
      operator()(SetMode{Mode::kCompose});
    }
    state.navigation = Navigation{};
    selection.subject = RecipeSubject{};
    selection.property.reset();
    state.revealedProperty.reset();
    selection.recipeID = a_i.recipeID;
    selection.document = true;
    selection.layer.reset();
    if (!state.paint) {
      mask = MaskStack{};
    }
  }

  void operator()(const SoloRecipe &) {}
  void operator()(const SoloOutput &) {}
  void operator()(const SoloLayer &) {}
  void operator()(const MuteLayer &) {}
  void operator()(const SetFreeze &) {}
  void operator()(const SetScrub &) {}
  void operator()(const SetSpeed &) {}
  void operator()(const StepClock &) {}
  void operator()(const Undo &a_i) {
    invalidateAssignment(a_i.recipeID);
    InvalidateIndexedSubjects(state.navigation, selection, a_i.recipeID);
    InvalidatePreviewPin(state.previewPin, a_i.recipeID);
  }
  void operator()(const Redo &a_i) {
    invalidateAssignment(a_i.recipeID);
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
        [](const ShowSettings &) {}, [](const ShowResource &) {},
        [](const ReadMesh &) {}, [](const SetTermOp &) {},
        [](const SetTermText &) {}, [](const RemoveTerm &) {},
        [](const MoveTerm &) {}, [](const PickTerm &) {},
        [](const SoloTerm &) {}, [](const MuteTerm &) {},
        [](const LoadMask &) {}, [](const ClearMask &) {},
        [](const UndoMask &) {}, [](const RedoMask &) {},
        [](const BeginPaint &) {}, [](const SetPaintSurface &) {},
        [](const KeepPaint &) {}, [](const EndPaint &) {},
        [](const UpdatePaint &) {}, [](const EditRecipe &) {},
        [](const SoloRecipe &) {}, [](const SoloOutput &) {},
        [](const SoloLayer &) {}, [](const MuteLayer &) {},
        [](const SetFreeze &) {}, [](const SetScrub &) {},
        [](const SetSpeed &) {}, [](const StepClock &) {}, [](const Undo &) {},
        [](const Redo &) {}, [](const CreateRecipe &) {},
        [](const RenameRecipe &) {}, [](const FireTrigger &) {});
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
      [](const FireTrigger &) { return false; });
}

[[nodiscard]] bool RequiresSettledEditor(const Intent &a_intent) {
  return Is<EditRecipe>(a_intent) || Is<Undo>(a_intent) || Is<Redo>(a_intent) ||
         Is<CreateRecipe>(a_intent) || Is<RenameRecipe>(a_intent) ||
         Is<BeginPaint>(a_intent) || Is<KeepPaint>(a_intent) ||
         Is<UpdatePaint>(a_intent) || Is<SoloOutput>(a_intent) ||
         Is<SoloLayer>(a_intent) || Is<MuteLayer>(a_intent) ||
         Is<PickCell>(a_intent) || Is<PickLayer>(a_intent) ||
         ChangesPaint(a_intent);
}
}

bool AcceptIntent(const MenuState &a_state, const Intent &a_intent) {
  if ((a_state.pendingIndexedEdit || a_state.pendingRecipeFile) &&
      RequiresSettledEditor(a_intent)) {
    return false;
  }
  if (a_state.paint && a_state.paint->pendingCommit &&
      RequiresSettledEditor(a_intent)) {
    return false;
  }
  return Match(
      a_intent,
      [&](const BeginPaint &a_begin) {
        return a_state.mode == Mode::kPaint && !a_state.paint &&
               a_begin.recipeID != kPaintRecipe &&
               a_begin.resetID == a_state.lastPaintReset;
      },
      [&](const KeepPaint &a_keep) {
        return a_state.paint && a_state.paint->ready &&
               !a_state.paint->pendingCommit &&
               a_state.paint->sessionID == a_keep.request.sessionID &&
               a_state.paint->recipeID == a_keep.request.recipeID;
      },
      [&](const UpdatePaint &a_update) {
        return a_state.paint && a_state.paint->ready &&
               !a_state.paint->pendingRevision &&
               !a_state.paint->pendingCommit &&
               a_state.paint->sessionID == a_update.request.sessionID;
      },
      [&](const EndPaint &) {
        return !a_state.paint || !a_state.paint->pendingCommit;
      },
      [](const SetMode &) { return true; },
      [](const PickPiece &) { return true; },
      [](const PickRecipe &) { return true; },
      [](const PinRecipe &) { return true; },
      [](const PickTarget &) { return true; },
      [](const PickSlot &) { return true; },
      [](const PickCell &) { return true; },
      [](const PickLayer &) { return true; },
      [](const ViewGeometry &) { return true; },
      [](const SetStackSplit &) { return true; },
      [](const ShowSettings &) { return true; },
      [](const ShowResource &) { return true; },
      [](const ReadMesh &) { return true; },
      [](const AddTerm &) { return true; },
      [](const SetTermOp &) { return true; },
      [](const SetTermText &) { return true; },
      [](const SetTermKind &) { return true; },
      [](const RemoveTerm &) { return true; },
      [](const MoveTerm &) { return true; },
      [](const PickTerm &) { return true; },
      [](const SoloTerm &) { return true; },
      [](const MuteTerm &) { return true; },
      [](const LoadMask &) { return true; },
      [](const ClearMask &) { return true; },
      [](const UndoMask &) { return true; },
      [](const RedoMask &) { return true; },
      [](const SetPaintSurface &) { return true; },
      [](const EditRecipe &) { return true; },
      [](const SoloRecipe &) { return true; },
      [](const SoloOutput &) { return true; },
      [](const SoloLayer &) { return true; },
      [](const MuteLayer &) { return true; },
      [](const SetFreeze &) { return true; },
      [](const SetScrub &) { return true; },
      [](const SetSpeed &) { return true; },
      [](const StepClock &) { return true; }, [](const Undo &) { return true; },
      [](const Redo &) { return true; },
      [](const CreateRecipe &) { return true; },
      [](const RenameRecipe &) { return true; },
      [](const FireTrigger &) { return true; });
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

void AcknowledgeEditorOperations(MenuState &a_state,
                                 const Snapshot &a_snapshot) {
  for (const RecipeEditResult &result : a_snapshot.editResults) {
    (void)AcknowledgeIndexedEdit(a_state.pendingIndexedEdit, &result);
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
  const bool exceeds = Match(
      a_intent,
      [&](const AddTerm &a_add) {
        return a_state.mask.terms.size() >= kMaxTerms ||
               (a_state.paint &&
                a_add.sources.size() >
                    kMaxRecipeRows - std::min(kMaxRecipeRows,
                                              a_state.paint->sources.size()));
      },
      [&](const SetTermKind &a_set) {
        return a_state.paint &&
               a_set.sources.size() >
                   kMaxRecipeRows -
                       std::min(kMaxRecipeRows, a_state.paint->sources.size());
      },
      [&](const LoadMask &a_load) { return a_load.terms.size() > kMaxTerms; },
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
      [](const ShowSettings &) { return false; },
      [](const ShowResource &) { return false; },
      [](const ReadMesh &) { return false; },
      [](const SetTermOp &) { return false; },
      [](const SetTermText &) { return false; },
      [](const RemoveTerm &) { return false; },
      [](const MoveTerm &) { return false; },
      [](const PickTerm &) { return false; },
      [](const SoloTerm &) { return false; },
      [](const MuteTerm &) { return false; },
      [](const ClearMask &) { return false; },
      [](const UndoMask &) { return false; },
      [](const RedoMask &) { return false; },
      [](const BeginPaint &) { return false; },
      [](const SetPaintSurface &) { return false; },
      [](const KeepPaint &) { return false; },
      [](const EndPaint &) { return false; },
      [](const UpdatePaint &) { return false; },
      [](const EditRecipe &) { return false; },
      [](const SoloRecipe &) { return false; },
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
      [](const FireTrigger &) { return false; });
  if (exceeds) {
    if (a_state.paint) {
      a_state.paint->problem =
          MakeDiagnostic(Severity::kError, "paint",
                         "the mask term or source limit was reached");
    }
    return;
  }
  Match(a_intent, ReduceVisitor{a_state});
  const bool changed = ChangesPaint(a_intent);
  if (a_state.paint && changed) {
    ++a_state.paint->revision;
    a_state.mask.dirty = true;
    a_state.paint->problem.reset();
  }
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
  return UpdatePaint{PaintUpdateRequest{paint.sessionID, paint.revision,
                                        std::move(expression), paint.sources,
                                        paint.surface}};
}

bool MaskTaskActive(const MenuState &a_state) {
  return a_state.paint.has_value() && a_state.mode == Mode::kPaint;
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
