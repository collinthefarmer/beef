#include "MenuState.h"

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
					a_selection.target = e.surface == Surface::kShell ? Target::kShell : Target::kMaterial;
					a_selection.slot = e.slot;
					a_selection.layer.reset();
				},
				[](const auto&) {});
		}
	}

	std::string_view ResourceTabName(ResourceTab a_tab) noexcept
	{
		switch (a_tab) {
		case ResourceTab::kSignals:
			return "Signals";
		case ResourceTab::kCurves:
			return "Curves";
		}
		return "?";
	}

	void Reduce(MenuState& a_state, const Intent& a_intent)
	{
		auto& selection = a_state.selection;
		Match(
			a_intent,
			[&](const SetMode& i) {
				if (a_state.mode != i.mode) {
					a_state.mode = i.mode;
					a_state.layout = LayoutFor(i.mode);
				}
			},
			[&](const PickPiece& i) {
				// A new piece: the recipe, geometry, cell and region start over.
				selection = Selection{};
				selection.actorID = i.actorID;
				selection.armorID = i.armorID;
				selection.firstPerson = i.firstPerson;
			},
			[&](const PickRecipe& i) {
				selection.recipeID = i.id;
				selection.layer.reset();
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
				selection.target = i.surface == Surface::kShell ? Target::kShell : Target::kMaterial;
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
			},
			[](const auto&) {});
	}
}
