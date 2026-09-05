#pragma once

// Every ImGui mechanic the studio uses, one place each. A widget draws from
// values and returns what the user chose; it never calls the manager or
// reads the store. Field widths are multiplied by the layout's widget scale
// so Design mode gets large controls from the same code. Thumbnail is the
// one place a TextureHandle is dereferenced.

#include "Core.h"
#include "Recipe.h"
#include "Snapshot.h"
#include "Studio.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace WornEnchantmentPBR::Studio::Widgets
{
	// A row dragged onto another: the dragged row ends at `to`.
	struct RowMove
	{
		std::size_t from = 0;
		std::size_t to = 0;
	};

	// Fields are keyed by a string unique to the row and field
	// ("layer:magicka:2:0:mask"); the key names the buffer the field types
	// into. A width below zero fills the available space.
	// A width below zero fills what is left of the line or the table cell,
	// so an input that shares its cell with nothing else takes all of it.
	// -FLT_MIN is ImGui's exact "everything left"; -1 would leave a pixel,
	// and a stretch table measuring such content shrinks a pixel per frame.
	inline constexpr float kFillWidth = -(std::numeric_limits<float>::min)();
	// A width of zero fits a combo to its preview text: the text, the frame
	// padding and the arrow. FitWidth gives that number for any text.
	inline constexpr float kFitWidth = 0.0f;
	[[nodiscard]] float    FitWidth(std::string_view a_text);

	[[nodiscard]] std::optional<std::string> TextField(const std::string& a_key, const std::string& a_model, float a_width, float a_scale);
	[[nodiscard]] std::optional<float>       DragField(const std::string& a_key, float a_value, float a_width, float a_scale, float a_speed = 0.01f);
	[[nodiscard]] std::optional<Vec3>        ColorField(const std::string& a_key, const Vec3& a_value, float a_width, float a_scale);

	// Channel: 0..3 one channel as grey, 4 rgb, 5 luminance. A dynamic
	// texture is re-rendered every frame; a null handle draws a blank.
	void               Thumbnail(TextureHandle a_texture, std::uint32_t a_channel, bool a_dynamic, float a_size);
	[[nodiscard]] bool ThumbnailButton(const std::string& a_key, TextureHandle a_texture, std::uint32_t a_channel, bool a_dynamic, float a_size);

	[[nodiscard]] std::optional<Blend> BlendCombo(const std::string& a_key, std::string_view a_current, std::span<const Blend> a_allowed, float a_width, float a_scale);
	// The width that shows the widest of the allowed blend names.
	[[nodiscard]] float BlendWidth(std::span<const Blend> a_allowed);
	// Lists the names as "@name" and returns the chosen text; the empty
	// entry, when allowed, returns "".
	[[nodiscard]] std::optional<std::string> ReferenceCombo(const std::string& a_key, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, float a_width, float a_scale);
	// The width that shows the widest of the names, so a column of combos lines up.
	[[nodiscard]] float WidestOf(std::span<const std::string> a_names);

	// A tab per mode; true when the mode changed this frame.
	bool ModeBar(Mode& a_mode);

	// Reordering: DragHandle draws the grip that starts a drag of row
	// a_index; DropTarget makes the last drawn item accept one. a_type names
	// the payload kind so unrelated lists never accept each other's rows.
	[[nodiscard]] bool                   DragHandle(const char* a_type, std::size_t a_index);  // true when clicked without dragging
	[[nodiscard]] std::optional<RowMove> DropTarget(const char* a_type, std::size_t a_index);

	void Problem(std::string_view a_text);
	void Warn(std::string_view a_text);
	void Ok(std::string_view a_text);
	void Dim(std::string_view a_text);
	void HelpMarker(const char* a_text);
	// Solo and mute as two small checkboxes, "S" and "M", the same wherever
	// a thing can be soloed or muted; a null mute draws solo alone. Returns
	// true when either changed.
	bool SoloMute(bool& a_solo, bool* a_mute);
	// A horizontal rule with a line's gap above and below it.
	void Rule();
	// A tooltip on the last item while it is hovered, disabled items included.
	void Tooltip(std::string_view a_text);

	// What a field requires, as a badge at its left edge: one glyph for the
	// kind of value, filled with the kind's colour when a signal reference
	// may stand in for the value and outlined when it is literal. The
	// tooltip carries the full rule, including what is coerced (one number
	// into a colour or vector).
	enum class FieldType
	{
		kScalar,      // a number, or @signal of scalar type
		kColor,       // r, g, b (one number for all three), or @signal of colour type
		kVector,      // x, y, z as a position, direction or scale, or @signal of vector type
		kReference,   // @name of a row
		kExpression,  // the recipe language
		kCurve,       // an expression in x, or @curve
		kMask,        // an expression per texel over sources and masks
		kChannels,    // a subset of rgba
	};
	void Badge(FieldType a_type);
	// A value that is a literal or a @signal, as one control: the type badge
	// on the left is a button that switches the input between a text field
	// and a combo over the signals of the right type, and gives it the
	// keyboard; the input fills the rest. The mode starts as the text says
	// (a "@name" opens as a combo). Returns the new text whichever way it
	// was entered.
	[[nodiscard]] std::optional<std::string> ValueField(const std::string& a_key, FieldType a_type, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, float a_scale);
	// A button the height of a field, drawn beside a badge, that opens the
	// field's detail modal; returns true when clicked.
	[[nodiscard]] bool DetailButton(const std::string& a_key);

	[[nodiscard]] std::string ValueText(const Value& a_value);
	// A 16 px swatch when the value is a colour, then the value's text.
	void ValueSwatch(const std::string& a_key, const Value& a_value);
}
