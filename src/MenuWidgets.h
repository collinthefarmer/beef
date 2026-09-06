#pragma once

// Every ImGui mechanic the studio uses, one place each. A widget draws from
// values and returns what the user chose; it never calls the manager or
// reads the store. Field widths are multiplied by the layout's widget scale
// so Design mode gets large controls from the same code. Thumbnail is the
// one place a TextureHandle is dereferenced.
//
// A widget that keeps state between frames (a text buffer, a drag's value,
// a combo mode) takes a short literal key and is keyed by that key's
// ImGuiID in the scope it is drawn in; the page pushes an ID scope per
// recipe, output, layer and row, so "value" on one row never meets "value"
// on the next.

#include "Core.h"
#include "Forms.h"
#include "Recipe.h"
#include "Snapshot.h"
#include "Studio.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace WornEnchantmentPBR::Studio::Widgets
{
	// ------------------------------------------------------------------ types

	// How wide an item or a column is: the rest of the line or cell, the
	// width that shows a text in a combo (or, for a column, its content), or
	// a number of pixels. The widget scale applies to fit and pixel widths,
	// never to fill.
	struct Width
	{
		enum class Mode
		{
			kFill,
			kFit,
			kPx,
		};
		Mode        mode = Mode::kFill;
		float       pixels = 0.0f;
		std::string text;  // kFit: the text to fit; empty fits the content

		[[nodiscard]] static Width Fill() noexcept;
		[[nodiscard]] static Width Fit();
		[[nodiscard]] static Width Fit(std::string_view a_text);
		[[nodiscard]] static Width Px(float a_pixels) noexcept;
	};

	struct Column
	{
		const char* label = "";
		Width       width = Width::Fill();  // Fill stretches; Fit and Px are fixed
	};

	// A table's look. The defaults are the context tables': full borders,
	// columns that fit their content, a header row.
	struct TableStyle
	{
		enum class Borders
		{
			kAll,
			kInnerHorizontal,
		};
		Borders borders = Borders::kAll;
		bool    stretch = false;  // Fill columns share the width in proportion to their content
		bool    headers = true;
		bool    rowBackground = false;
	};

	// A table drawn cell by cell: Begin sets the columns up (and the header
	// row when the style has one), Cell moves to the next cell and starts a
	// row every `columns` cells, End closes it. When Begin fails, Open is
	// false, nothing may be drawn in it, and End does nothing.
	class Table
	{
	public:
		[[nodiscard]] static Table Begin(const char* a_id, std::span<const Column> a_columns, const TableStyle& a_style = {});
		[[nodiscard]] static Table Begin(const char* a_id, std::initializer_list<Column> a_columns, const TableStyle& a_style = {});
		[[nodiscard]] bool         Open() const noexcept { return open_; }
		void                       Cell();
		void                       End();

	private:
		bool        open_ = false;
		std::size_t columns_ = 0;
		std::size_t cells_ = 0;
	};

	// A row dragged onto another: the dragged row ends at `to`.
	struct RowMove
	{
		std::size_t from = 0;
		std::size_t to = 0;
	};

	// ----------------------------------------------------------------- widths

	// Sets the next item's width from a Width; fit is measured with FitWidth.
	void NextItemWidth(const Width& a_width, float a_scale = 1.0f);
	// The width a combo needs to show a_text whole: the text, the frame
	// padding and the arrow.
	[[nodiscard]] float FitWidth(std::string_view a_text);
	// The width that shows the widest of the allowed blend names.
	[[nodiscard]] float BlendWidth(std::span<const Blend> a_allowed);
	// The width that shows the widest of the names, so a column of combos lines up.
	[[nodiscard]] float WidestOf(std::span<const std::string> a_names);
	// The style's gap between items on a line.
	[[nodiscard]] float ItemSpacingX();

	// ----------------------------------------------------------------- fields

	[[nodiscard]] std::optional<std::string> TextField(const char* a_key, const std::string& a_model, const Width& a_width, float a_scale);
	// Live text that nothing overwrites (a table's name filter, a new row's
	// id), returned as it stands this frame; the hint shows while it is empty.
	[[nodiscard]] std::string_view LiveTextField(const char* a_key, const char* a_hint, const Width& a_width, float a_scale);

	// Channel: 0..3 one channel as grey, 4 rgb, 5 luminance. A dynamic
	// texture is re-rendered every frame; a null handle draws a blank.
	void               Thumbnail(TextureHandle a_texture, std::uint32_t a_channel, bool a_dynamic, float a_size);
	[[nodiscard]] bool ThumbnailButton(const char* a_key, TextureHandle a_texture, std::uint32_t a_channel, bool a_dynamic, float a_size);

	[[nodiscard]] std::optional<Blend> BlendCombo(const char* a_key, std::string_view a_current, std::span<const Blend> a_allowed, const Width& a_width, float a_scale);
	// Lists the names as "@name" and returns the chosen text; the empty
	// entry, when allowed, returns "".
	[[nodiscard]] std::optional<std::string> ReferenceCombo(const char* a_key, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, const Width& a_width, float a_scale);

	// What a field requires, as a badge at its left edge: one glyph for the
	// kind of value, filled with the kind's colour when a signal reference
	// may stand in for the value and outlined when it is literal. The
	// tooltip carries the full rule, including what is coerced (one number
	// into a colour or vector).
	void Badge(FieldKind a_kind);
	// A value that is a literal or a @signal, as one control: the type badge
	// on the left is a button that switches the input between a text field
	// and a combo over the signals of the right type, and gives it the
	// keyboard; the input fills the rest. The mode starts as the text says
	// (a "@name" opens as a combo). With no names to offer the value is
	// literal and the badge is outlined and inert. A colour field's text
	// carries a swatch that opens a picker; a pick commits on release as
	// "r, g, b". Returns the new text whichever way it was entered.
	[[nodiscard]] std::optional<std::string> ValueField(const char* a_key, FieldKind a_kind, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, float a_scale);
	// A button the height of a field, drawn beside a badge, that opens the
	// field's detail modal; returns true when clicked. One per ID scope.
	[[nodiscard]] bool DetailButton();

	[[nodiscard]] std::string ValueText(const Value& a_value);
	// A 16 px swatch when the value is a colour, then the value's text. One per ID scope.
	void ValueSwatch(const Value& a_value);

	// ---------------------------------------------------------------- layout

	// A tab per mode; true when the mode changed this frame.
	bool ModeBar(Mode& a_mode);
	// A collapsible section; true while it is open, so the caller draws its
	// contents only then.
	[[nodiscard]] bool Section(const char* a_title, bool a_openByDefault);
	// Two columns split by a draggable vertical rule. a_ratio is the share
	// the left column takes: it sets the split when the table first appears
	// and follows the rule as the user drags it.
	void Split(const char* a_id, float& a_ratio, const std::function<void()>& a_left, const std::function<void()>& a_right);
	// One line of a rule: a text at its left edge (the section under the
	// rule is named by the line below it), and an item at its right edge
	// drawn by `right`, `rightWidth` wide. Either may be absent; an absent
	// text leaves a gap. A line is a frame high either way.
	struct RuleLine
	{
		std::string_view      text;
		float                 rightWidth = 0.0f;
		std::function<void()> right;

		[[nodiscard]] static RuleLine Text(std::string_view a_text) { return RuleLine{ a_text, 0.0f, {} }; }
	};

	// A horizontal rule with a line above and below it. RuleHeight is what
	// it takes whatever the lines hold, for a pane sized around it.
	void                Rule(const RuleLine& a_above = {}, const RuleLine& a_below = {});
	[[nodiscard]] float RuleHeight();

	// A checkbox with a tooltip; true when it changed this frame. Solo,
	// mute, isolate and freeze are all this.
	bool Toggle(const char* a_label, bool& a_value, std::string_view a_tooltip);
	// A button drawn pressed while a_active, greyed while not a_enabled;
	// true when clicked. Two of them make a section switch.
	[[nodiscard]] bool SwitchButton(const char* a_label, bool a_active, bool a_enabled);
	// A combo over plain names (a choice field); returns the chosen name.
	[[nodiscard]] std::optional<std::string> ChoiceCombo(const char* a_key, const std::string& a_current, std::span<const std::string> a_names, const Width& a_width, float a_scale);
	// Solo and mute as two small toggles, "S" and "M", the same wherever a
	// thing can be soloed or muted; a null mute draws solo alone. Returns
	// true when either changed.
	bool SoloMute(bool& a_solo, bool* a_mute);

	// Reordering: DragHandle draws the grip that starts a drag of row
	// a_index; DropTarget makes the last drawn item accept one. a_type names
	// the payload kind so unrelated lists never accept each other's rows.
	[[nodiscard]] bool                   DragHandle(const char* a_type, std::size_t a_index);  // true when clicked without dragging
	[[nodiscard]] std::optional<RowMove> DropTarget(const char* a_type, std::size_t a_index);

	// ------------------------------------------------------------------- text

	void Problem(std::string_view a_text);
	void Warn(std::string_view a_text);
	void Ok(std::string_view a_text);
	void Dim(std::string_view a_text);
	void HelpMarker(const char* a_text);
	// A tooltip on the last item while it is hovered, disabled items included.
	void Tooltip(std::string_view a_text);
}
