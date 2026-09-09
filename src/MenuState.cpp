#include "MenuState.h"

#include <algorithm>
#include <functional>

namespace BetterEnchantmentEffects::Studio
{
	namespace
	{
		void ReduceEdit(Selection& a_selection, const RecipeEdit& a_edit)
		{
			Match(
				a_edit,
				[&](const AddLayer& e) {
					if (e.at) {
						a_selection.layer = *e.at;
					}
				},
				[&](const RemoveLayer& e) {
					if (!a_selection.layer) {
						return;
					}
					if (*a_selection.layer == e.layer) {
						a_selection.layer.reset();
					} else if (*a_selection.layer > e.layer) {
						--*a_selection.layer;
					}
				},
				[&](const MoveLayer& e) {
					if (!a_selection.layer) {
						return;
					}
					const std::size_t at = *a_selection.layer;
					if (at == e.from) {
						a_selection.layer = e.to;
					} else if (e.from < at && at <= e.to) {
						--*a_selection.layer;
					} else if (e.to <= at && at < e.from) {
						++*a_selection.layer;
					}
				},
				[&](const ClearLayers&) { a_selection.layer.reset(); },
				[&](const RemoveOutput&) { a_selection.layer.reset(); },
				[&](const AddOutput& e) {
					a_selection.target = TargetOf(e.surface);
					a_selection.slot = e.slot;
					a_selection.layer.reset();
				},
				[](const auto&) {});
		}
	}

	namespace
	{
		void RemapRegion(RegionStack& a_region, const std::function<std::optional<std::size_t>(std::size_t)>& a_map)
		{
			const auto remap = [&](std::optional<std::size_t>& a_index) {
				if (a_index) {
					a_index = a_map(*a_index);
				}
			};
			remap(a_region.selected);
			remap(a_region.solo);
			std::set<std::size_t> muted;
			for (const auto index : a_region.muted) {
				if (const auto to = a_map(index)) {
					muted.insert(*to);
				}
			}
			a_region.muted = std::move(muted);
		}
	}

	std::string_view ResourceTabName(ResourceTab a_tab) noexcept
	{
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

	void Reduce(MenuState& a_state, const Intent& a_intent)
	{
		auto& selection = a_state.selection;
		auto& region = a_state.region;
		const auto endSession = [&]() {
			if (a_state.paint) {
				selection.recipeID = a_state.paint->recipeID;
			}
			a_state.paint.reset();
			region = RegionStack{};
			a_state.regionHistory.Clear();
		};
		const auto remember = [&]() { a_state.regionHistory.Push(region); };
		const auto pickRecipe = [&](const std::string& a_id) {
			if (selection.recipeID != a_id) {
				region = RegionStack{};
			}
			selection.recipeID = a_id;
			selection.layer.reset();
		};
		Match(
			a_intent,
			[&](const SetMode& i) {
				if (a_state.mode != i.mode) {
					const float split = a_state.layout.stackSplit;
					a_state.mode = i.mode;
					a_state.layout = LayoutFor(i.mode);
					a_state.layout.stackSplit = split;
					if (i.mode == Mode::kPaint) {
						a_state.resource = ResourceTab::kMasks;
					} else if (a_state.paint) {
						endSession();
					}
				}
			},
			[&](const PickPiece& i) {
				selection = Selection{};
				selection.piece = i.piece;
				region = RegionStack{};
			},
			[&](const PickRecipe& i) { pickRecipe(i.recipeID); },
			[&](const PinRecipe& i) { pickRecipe(i.recipeID); },
			[&](const AddTerm& i) {
				if (region.terms.size() >= kMaxTerms) {
					return;
				}
				remember();
				Term term = i.term;
				if (region.terms.empty()) {
					term.op = TermOp::kSet;
				} else if (term.op == TermOp::kSet) {
					term.op = TermOp::kAnd;
				}
				region.terms.push_back(std::move(term));
				region.selected = region.terms.size() - 1;
				region.dirty = true;
			},
			[&](const SetTermOp& i) {
				remember();
				if (i.index < region.terms.size() && i.index > 0) {
					region.terms[i.index].op = i.op == TermOp::kSet ? TermOp::kAnd : i.op;
					region.dirty = true;
				}
			},
			[&](const SetTermText& i) {
				remember();
				if (i.index < region.terms.size()) {
					region.terms[i.index].text = i.text;
					region.terms[i.index].label = std::string{ kExpressionLabel };
					region.terms[i.index].kind = RawTerm{};
					region.dirty = true;
				}
			},
			[&](const SetTermKind& i) {
				remember();
				if (i.index < region.terms.size()) {
					region.terms[i.index].kind = i.kind;
					region.terms[i.index].text = i.text;
					region.terms[i.index].label = i.label;
					region.dirty = true;
				}
			},
			[&](const RemoveTerm& i) {
				remember();
				if (i.index >= region.terms.size()) {
					return;
				}
				region.terms.erase(region.terms.begin() + static_cast<std::ptrdiff_t>(i.index));
				if (!region.terms.empty()) {
					region.terms.front().op = TermOp::kSet;
				}
				RemapRegion(region, [&](std::size_t a_at) -> std::optional<std::size_t> {
					if (a_at == i.index) {
						return std::nullopt;
					}
					return a_at > i.index ? a_at - 1 : a_at;
				});
				region.dirty = true;
			},
			[&](const MoveTerm& i) {
				remember();
				const std::size_t count = region.terms.size();
				if (i.from >= count || i.to >= count || i.from == i.to) {
					return;
				}
				Term moved = region.terms[i.from];
				region.terms.erase(region.terms.begin() + static_cast<std::ptrdiff_t>(i.from));
				region.terms.insert(region.terms.begin() + static_cast<std::ptrdiff_t>(i.to), std::move(moved));
				for (std::size_t k = 1; k < region.terms.size(); ++k) {
					if (region.terms[k].op == TermOp::kSet) {
						region.terms[k].op = TermOp::kAnd;
					}
				}
				region.terms.front().op = TermOp::kSet;
				RemapRegion(region, [&](std::size_t a_at) -> std::optional<std::size_t> {
					if (a_at == i.from) {
						return i.to;
					}
					if (i.from < a_at && a_at <= i.to) {
						return a_at - 1;
					}
					if (i.to <= a_at && a_at < i.from) {
						return a_at + 1;
					}
					return a_at;
				});
				region.dirty = true;
			},
			[&](const PickTerm& i) {
				if (i.index < region.terms.size()) {
					region.selected = i.index;
				}
			},
			[&](const SoloTerm& i) {
				if (i.index >= region.terms.size()) {
					return;
				}
				if (i.on) {
					region.solo = i.index;
				} else if (region.solo == i.index) {
					region.solo.reset();
				}
				region.dirty = true;
			},
			[&](const MuteTerm& i) {
				if (i.index >= region.terms.size()) {
					return;
				}
				if (i.on) {
					region.muted.insert(i.index);
				} else {
					region.muted.erase(i.index);
				}
				region.dirty = true;
			},
			[&](const LoadRegion& i) {
				remember();
				region = RegionStack{};
				region.terms.assign(i.terms.begin(), i.terms.begin() + static_cast<std::ptrdiff_t>((std::min)(i.terms.size(), kMaxTerms)));
				if (!region.terms.empty()) {
					region.terms.front().op = TermOp::kSet;
					region.selected = 0;
				}
				region.editing = i.editing;
				region.dirty = !region.terms.empty();
			},
			[&](const ClearRegion&) {
				remember();
				region = RegionStack{};
			},
			[&](const UndoRegion&) {
				if (auto past = a_state.regionHistory.Undo(region)) {
					region = std::move(*past);
					region.dirty = true;
				}
			},
			[&](const RedoRegion&) {
				if (auto next = a_state.regionHistory.Redo(region)) {
					region = std::move(*next);
					region.dirty = true;
				}
			},
			[&](const BeginPaint& i) {
				a_state.paint = PaintSession{ i.recipeID, i.surface, {} };
				a_state.regionHistory.Clear();
			},
			[&](const ReadMesh& i) {
				if (a_state.paint) {
					a_state.paint->readGeometries.insert(i.geometry);
				}
			},
			[&](const SetPaintSurface& i) {
				if (a_state.paint) {
					a_state.paint->surface = i.surface;
				}
			},
			[&](const KeepPaint&) { endSession(); },
			[&](const EndPaint&) { endSession(); },
			[&](const PickTarget& i) {
				if (selection.target != i.target) {
					selection.target = i.target;
					selection.slot.reset();
					selection.layer.reset();
				}
			},
			[&](const PickSlot& i) {
				selection.slot = i.slot;
				selection.layer.reset();
			},
			[&](const PickCell& i) {
				selection.target = TargetOf(i.surface);
				selection.slot = i.slot;
				selection.layer = i.topLayer;
			},
			[&](const PickLayer& i) { selection.layer = i.index; },
			[&](const ViewGeometry& i) { selection.geometry = i.name; },
			[&](const SetStackSplit& i) { a_state.layout.stackSplit = std::clamp(i.ratio, 0.05f, 0.95f); },
			[&](const ScratchRebuilt&) { region.dirty = false; },
			[&](const ShowSettings& i) { a_state.settings = i.on; },
			[&](const ShowResource& i) { a_state.resource = i.tab; },
			[&](const EditRecipe& i) {
				for (const auto& edit : i.edits) {
					ReduceEdit(selection, edit);
				}
			},
			[&](const RenameRecipe& i) {
				if (selection.recipeID == i.from) {
					selection.recipeID = i.to;
				}
				if (a_state.paint && a_state.paint->recipeID == i.from) {
					a_state.paint->recipeID = i.to;
				}
			},
			[&](const CreateRecipe& i) {
				selection.recipeID = i.recipeID;
				selection.layer.reset();
				region = RegionStack{};
			},
			[](const SoloRecipe&) {},
			[](const SoloOutput&) {},
			[](const SoloLayer&) {},
			[](const MuteLayer&) {},
			[](const SetFreeze&) {},
			[](const SetScrub&) {},
			[](const SetSpeed&) {},
			[](const StepClock&) {},
			[](const Undo&) {},
			[](const Redo&) {},
			[](const FireTrigger&) {});
	}
}
