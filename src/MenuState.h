#pragma once

// The page state the widgets and the compose page share: the mode, the
// selection, and the text a field holds while it is being typed into. One
// instance, touched on the render thread only; nothing here is saved.

#include "Studio.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>

namespace WornEnchantmentPBR::Studio
{
	// What the studio's target picker points at: a surface, or the light.
	enum class PickedTarget
	{
		kMaterial,
		kShell,
		kLight,
	};

	using TextBuffer = std::array<char, 1024>;
	using NumberBuffer = std::array<float, 3>;  // a drag's value, or a colour's r, g, b

	struct MenuState
	{
		Mode      mode = Mode::kCompose;
		Selection selection;
		// The studio's target and slot pickers. The selected output follows
		// them when the cell is written; Add output shows when it is empty.
		PickedTarget        target = PickedTarget::kMaterial;
		std::optional<Slot> slot;
		// A field shows the model until it is active; while it is, it shows its
		// own buffer, so a snapshot taken mid-edit never overwrites typing.
		std::unordered_map<std::string, TextBuffer>   textBuffers;
		std::unordered_map<std::string, NumberBuffer> numberBuffers;
		std::string                                   activeField;  // key of the field being edited, empty when none
		// A value field is a text input or a combo over signals; its badge
		// toggles which. Keyed like the buffers. focusField names the field to
		// give the keyboard to on the next frame, after a toggle.
		std::unordered_map<std::string, bool> comboMode;
		std::string                           focusField;
	};

	[[nodiscard]] inline MenuState& State()
	{
		static MenuState state;
		return state;
	}
}
