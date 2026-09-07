#include "MenuState.h"

#include <algorithm>
#include <functional>

namespace WornEnchantmentPBR::Studio
{
	namespace
	{
		// The layer selection after an edit of the selected recipe's rows.
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
		// The indices the stack's solo, mute and selection hold, remapped
		// after a row moves or goes; a mapping to nothing drops the index.
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
		Match(
			a_intent,
			[&](const SetMode& i) {
				if (a_state.mode != i.mode) {
					a_state.mode = i.mode;
					a_state.layout = LayoutFor(i.mode);
					if (i.mode == Mode::kPaint) {
						a_state.resource = ResourceTab::kMasks;
					}
				}
			},
			[&](const PickPiece& i) {
				// A new piece: the recipe, geometry, cell and region start over.
				selection = Selection{};
				selection.actorID = i.actorID;
				selection.armorID = i.armorID;
				selection.firstPerson = i.firstPerson;
				region = RegionStack{};
			},
			[&](const PickRecipe& i) {
				if (selection.recipeID != i.id) {
					region = RegionStack{};
				}
				selection.recipeID = i.id;
				selection.layer.reset();
			},
			[&](const AddTerm& i) {
				if (region.terms.size() >= kMaxTerms) {
					return;
				}
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
				if (i.index < region.terms.size() && i.index > 0) {
					region.terms[i.index].op = i.op == TermOp::kSet ? TermOp::kAnd : i.op;
					region.dirty = true;
				}
			},
			[&](const SetTermText& i) {
				if (i.index < region.terms.size()) {
					region.terms[i.index].text = i.text;
					region.terms[i.index].label = std::string{ kExpressionLabel };
					region.terms[i.index].recipe = RawTerm{};
					region.dirty = true;
				}
			},
			[&](const SetTermRecipe& i) {
				if (i.index < region.terms.size()) {
					region.terms[i.index].recipe = i.recipe;
					region.terms[i.index].text = i.text;
					region.terms[i.index].label = i.label;
					region.dirty = true;
				}
			},
			[&](const RemoveTerm& i) {
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
				const std::size_t count = region.terms.size();
				if (i.from >= count || i.to >= count || i.from == i.to) {
					return;
				}
				Term moved = region.terms[i.from];
				region.terms.erase(region.terms.begin() + static_cast<std::ptrdiff_t>(i.from));
				region.terms.insert(region.terms.begin() + static_cast<std::ptrdiff_t>(i.to), std::move(moved));
				// The first term leads whatever op it carried; a displaced
				// leader takes `and`.
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
				region = RegionStack{};
				region.terms.assign(i.terms.begin(), i.terms.begin() + static_cast<std::ptrdiff_t>((std::min)(i.terms.size(), kMaxTerms)));
				if (!region.terms.empty()) {
					region.terms.front().op = TermOp::kSet;
					region.selected = 0;
				}
				region.editing = i.editing;
				region.dirty = !region.terms.empty();
			},
			[&](const ClearRegion&) { region = RegionStack{}; },
			[&](const BeginPaint& i) { a_state.paint = PaintSession{ i.recipe, i.surface }; },
			[&](const ReadMesh&) {
				if (a_state.paint) {
					a_state.paint->readPosted = true;
				}
			},
			[&](const SetPaintSurface& i) {
				if (a_state.paint) {
					a_state.paint->surface = i.surface;
				}
			},
			[&](const KeepPaint&) {
				a_state.paint.reset();
				region = RegionStack{};
			},
			[&](const EndPaint&) {
				a_state.paint.reset();
				region = RegionStack{};
			},
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
			[&](const PickRegion& i) { selection.region = i.name; },
			[&](const ViewGeometry& i) { selection.geometry = i.name; },
			[&](const ShowSettings& i) { a_state.settings = i.on; },
			[&](const ShowResource& i) { a_state.resource = i.tab; },
			[&](const EditRecipe& i) { ReduceEdit(selection, i.edit); },
			[&](const CreateRecipe& i) {
				selection.recipeID = i.id;
				selection.layer.reset();
				region = RegionStack{};
			},
			[](const auto&) {});
	}
}
