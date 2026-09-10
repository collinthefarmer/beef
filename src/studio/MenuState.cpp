#include "studio/Intent.h"

#include "studio/View.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <set>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
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
      [](const auto &) {});
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
      selection.recipeID = state.paint->recipeID;
    }
    state.paint.reset();
    mask = MaskStack{};
    state.maskHistory.Clear();
  }

  void remember() { state.maskHistory.Push(mask); }

  void pickRecipe(const std::string &a_id) {
    if (selection.recipeID != a_id) {
      mask = MaskStack{};
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
      } else if (state.paint) {
        endSession();
      }
    }
  }

  void operator()(const PickPiece &a_i) {
    selection = Selection{};
    selection.piece = a_i.piece;
    mask = MaskStack{};
  }

  void operator()(const PickRecipe &a_i) { pickRecipe(a_i.recipeID); }

  void operator()(const PinRecipe &a_i) { pickRecipe(a_i.recipeID); }

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

  void operator()(const ViewGeometry &a_i) { selection.geometry = a_i.name; }

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
    mask = MaskStack{};
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

  void operator()(const ScratchRebuilt &) { mask.dirty = false; }

  void operator()(const BeginPaint &a_i) {
    state.paint = PaintSession{a_i.recipeID, a_i.surface, {}};
    state.maskHistory.Clear();
  }

  void operator()(const SetPaintSurface &a_i) {
    if (state.paint) {
      state.paint->surface = a_i.surface;
    }
  }

  void operator()(const KeepPaint &) { endSession(); }

  void operator()(const EndPaint &) { endSession(); }

  void operator()(const EditRecipe &a_i) {
    for (const RecipeEdit &edit : a_i.edits) {
      ReduceEdit(selection, edit);
    }
  }

  void operator()(const RenameRecipe &a_i) {
    if (selection.recipeID == a_i.from) {
      selection.recipeID = a_i.to;
    }
    if (state.paint && state.paint->recipeID == a_i.from) {
      state.paint->recipeID = a_i.to;
    }
  }

  void operator()(const CreateRecipe &a_i) {
    selection.recipeID = a_i.recipeID;
    selection.layer.reset();
    mask = MaskStack{};
  }

  void operator()(const SoloRecipe &) {}
  void operator()(const SoloOutput &) {}
  void operator()(const SoloLayer &) {}
  void operator()(const MuteLayer &) {}
  void operator()(const SetFreeze &) {}
  void operator()(const SetScrub &) {}
  void operator()(const SetSpeed &) {}
  void operator()(const StepClock &) {}
  void operator()(const Undo &) {}
  void operator()(const Redo &) {}
  void operator()(const FireTrigger &) {}
};
}

void Reduce(MenuState &a_state, const Intent &a_intent) {
  Match(a_intent, ReduceVisitor{a_state});
}
}
