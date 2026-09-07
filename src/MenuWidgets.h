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
			kNone,
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
	// The width a button with this label takes, and a checkbox with it, as
	// ImGui draws them, so a group placed at the right edge ends on it.
	[[nodiscard]] float ButtonWidth(std::string_view a_text);
	[[nodiscard]] float CheckboxWidth(std::string_view a_text);
	// The style's gap between items on a line.
	[[nodiscard]] float ItemSpacingX();

	// ----------------------------------------------------------------- fields

	// Why a text would be refused, or nothing; a field with one draws red
	// with the message under it while its text fails, and does not commit
	// failing text.
	using TextCheck = std::function<std::optional<std::string>(const std::string&)>;

	[[nodiscard]] std::optional<std::string> TextField(const char* a_key, const std::string& a_model, const Width& a_width, float a_scale, const TextCheck& a_check = {});
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
	// The creators, when given, follow the names past a separator and come
	// back as written ("new image").
	[[nodiscard]] std::optional<std::string> ReferenceCombo(const char* a_key, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, const Width& a_width, float a_scale, std::span<const std::string> a_creators = {});

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
	[[nodiscard]] std::optional<std::string> ValueField(const char* a_key, FieldKind a_kind, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, float a_scale, const TextCheck& a_check = {}, std::span<const std::string> a_creators = {});
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
	// A combo over plain names (a choice field); returns the chosen name.
	[[nodiscard]] std::optional<std::string> ChoiceCombo(const char* a_key, const std::string& a_current, std::span<const std::string> a_names, const Width& a_width, float a_scale);
	// A detail's modal: opened by name (ImGui::OpenPopup with the same
	// title), wide enough that a definition wraps once, the body, a close
	// button; drawn only while open.
	void DetailModal(const char* a_title, const std::function<void()>& a_body);
	// The items a_draw draws, placed so that a_width of them end on the
	// right edge of the line (they follow SameLine).
	void RightAligned(float a_width, const std::function<void()>& a_draw);
	// The items a_draw draws, greyed and inert while a_disabled; a greyed
	// button never reports a click.
	void Disabled(bool a_disabled, const std::function<void()>& a_draw);
	// A value shown as a field that cannot be edited (the recipe held
	// while painting).
	void HeldLabel(const char* a_text);
	// A button drawn in the pressed colour while a_lit, to call for a step
	// that has not been taken yet (read the mesh); true when clicked.
	[[nodiscard]] bool LitButton(const char* a_label, bool a_lit);

	// The row buttons of a stack: each a square the height of a field, the
	// badge's size, so a table column of RowButtonWidth is filled by one.
	[[nodiscard]] float RowButtonWidth();
	// "X": greyed while the row is referenced, with the count in its
	// tooltip; true when clicked and unreferenced.
	[[nodiscard]] bool RemoveButton(std::size_t a_references);
	// Solo and mute as "S" and "M", drawn filled while on, the same
	// wherever a thing can be soloed or muted: each alone for a table cell
	// of its own (true when it changed), or the pair side by side, which
	// returns which one changed this frame.
	bool SoloButton(bool& a_solo);
	bool MuteButton(bool& a_mute);
	enum class SoloMuteChange
	{
		kNone,
		kSolo,
		kMute,
	};
	SoloMuteChange SoloMute(bool& a_solo, bool& a_mute);

	// Reordering: DragHandle draws the grip (the same square) that starts a
	// drag of row a_index; DropTarget makes the last drawn item accept one.
	// a_type names the payload kind so unrelated lists never accept each
	// other's rows.
	[[nodiscard]] bool                   DragHandle(const char* a_type, std::size_t a_index, const char* a_noun);  // true when clicked without dragging; the noun names the row in the drag preview
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
