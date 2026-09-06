#pragma once

// The page state the widgets and the compose page share, and the intents
// that change it. The state is the mode and its layout, the selection, and
// the text a field holds while it is being typed into; one instance, touched
// on the render thread only; nothing here is saved.
//
// Widgets never write the selection. They return intents, the page collects
// them while drawing, and after the frame is drawn each one goes through
// Reduce (the state change, a pure function tested natively) and, where it
// has one, its effect on the manager. Every record derived in a frame is
// therefore consistent with the state the frame began with.

#include "Edits.h"
#include "Studio.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>

namespace WornEnchantmentPBR::Studio
{
	// A field is keyed by the ImGuiID of its literal key in the ID scope it is
	// drawn in (the page pushes a scope per recipe, output and layer), so the
	// same literal names a different field on every row and no string is built
	// per frame. Zero is no field, as ImGui reads it.
	using FieldKey = std::uint32_t;
	inline constexpr FieldKey kNoField = 0;

	// The tables of the resources pane.
	enum class ResourceTab
	{
		kSignals,
		kCurves,
	};
	inline constexpr std::array<ResourceTab, 2> kResourceTabs{ ResourceTab::kSignals, ResourceTab::kCurves };
	[[nodiscard]] std::string_view              ResourceTabName(ResourceTab a_tab) noexcept;

	using TextBuffer = std::array<char, 1024>;
	using NumberBuffer = std::array<float, 3>;  // a drag's value, or a colour's r, g, b

	struct MenuState
	{
		Mode      mode = Mode::kCompose;
		Layout    layout;  // LayoutFor(mode), kept so the split the user drags survives the frame
		Selection selection;
		// The stack pane shows the picked target's settings (the shell's, the
		// light's) or the picked slot's stack, one at a time; the page falls
		// back to whichever the target has.
		bool settings = false;
		// The resources pane's open tab; the tab bar owns the click, the
		// state follows it so the pane's rule knows which table Add and the
		// filter serve.
		ResourceTab resource = ResourceTab::kSignals;
		// A field shows the model until it is active; while it is, it shows its
		// own buffer, so a snapshot taken mid-edit never overwrites typing.
		std::unordered_map<FieldKey, TextBuffer>   textBuffers;
		std::unordered_map<FieldKey, NumberBuffer> numberBuffers;
		FieldKey                                   activeField = kNoField;  // the field being edited
		// A value field is a text input or a combo over signals; its badge
		// toggles which. Keyed like the buffers. focusField names the field to
		// give the keyboard to on the next frame, after a toggle.
		std::unordered_map<FieldKey, bool> comboMode;
		FieldKey                           focusField = kNoField;
	};

	[[nodiscard]] inline MenuState& State()
	{
		static MenuState state;
		return state;
	}

	// ---------------------------------------------------------------- intents
	// What a widget asked for. Picks change the selection; an edit changes
	// the recipe (and the selection where it moves a row); solo, mute,
	// freeze, scrub, speed and step change the view; undo, redo, a new
	// recipe and a fired trigger go to the manager.

	struct SetMode
	{
		Mode mode = Mode::kCompose;
	};
	struct PickPiece
	{
		FormID actorID = 0;
		FormID armorID = 0;
		bool   firstPerson = false;
	};
	struct PickRecipe
	{
		std::string id;
	};
	struct PickTarget
	{
		Target target = Target::kMaterial;
	};
	struct PickSlot
	{
		Slot slot = Slot::kEmissive;
	};
	// A written cell: its surface and slot, and its top layer so the
	// inspector opens on something at once.
	struct PickCell
	{
		Surface                    surface = Surface::kMaterial;
		Slot                       slot = Slot::kEmissive;
		std::optional<std::size_t> topLayer;
	};
	struct PickLayer
	{
		std::size_t index = 0;
	};
	struct PickRegion
	{
		std::string name;  // empty = whole piece
	};
	struct ViewGeometry
	{
		std::string name;
	};
	struct ShowSettings
	{
		bool on = false;
	};
	struct ShowResource
	{
		ResourceTab tab = ResourceTab::kSignals;
	};
	struct EditRecipe
	{
		std::string recipe;
		RecipeEdit  edit;
	};
	struct SoloRecipe
	{
		std::string recipe;
		bool        on = false;
	};
	struct SoloOutput
	{
		std::string recipe;
		std::size_t output = 0;
		bool        on = false;
	};
	struct SoloLayer
	{
		std::string recipe;
		std::size_t output = 0;
		std::size_t layer = 0;
		bool        on = false;
	};
	struct MuteLayer
	{
		std::string recipe;
		std::size_t output = 0;
		std::size_t layer = 0;
		bool        on = false;
	};
	struct SetFreeze
	{
		bool  on = false;
		float at = 0.0f;  // the moment held when freezing
	};
	struct SetScrub
	{
		float seconds = 0.0f;
	};
	struct SetSpeed
	{
		float speed = 1.0f;
	};
	struct StepClock
	{
	};
	struct Undo
	{
		std::string recipe;
	};
	struct Redo
	{
		std::string recipe;
	};
	struct CreateRecipe
	{
		std::string id;
		FormID      armorID = 0;
	};
	struct FireTrigger
	{
		FormID      actorID = 0;
		std::string event;
	};

	using Intent = std::variant<
		SetMode, PickPiece, PickRecipe, PickTarget, PickSlot, PickCell, PickLayer, PickRegion, ViewGeometry, ShowSettings, ShowResource,
		EditRecipe, SoloRecipe, SoloOutput, SoloLayer, MuteLayer,
		SetFreeze, SetScrub, SetSpeed, StepClock, Undo, Redo, CreateRecipe, FireTrigger>;

	// The state change an intent makes; nothing else. An edit moves the
	// layer selection with the row it adds, removes or moves, and a new
	// output or recipe becomes the selected one.
	void Reduce(MenuState& a_state, const Intent& a_intent);
}
