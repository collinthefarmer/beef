#include "MenuWidgets.h"

#include "MenuState.h"
#include "RuntimeTextures.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>
#include <type_traits>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include "extern/SKSEMenuFramework.h"
#pragma clang diagnostic pop

// The SDK header keeps the ImGui wrappers and types in ImGuiMCP.
namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImGuiID;
using ImGuiMCP::ImTextureID;
using ImGuiMCP::ImVec2;
using ImGuiMCP::ImVec4;

// A field key is the ImGuiID of its literal in the scope it is drawn in.
static_assert(std::is_same_v<WornEnchantmentPBR::Studio::FieldKey, ImGuiID>);

namespace WornEnchantmentPBR::Studio::Widgets
{
	namespace
	{
		constexpr ImVec4 kWarn{ 1.0f, 0.8f, 0.3f, 1.0f };
		constexpr ImVec4 kOk{ 0.5f, 0.9f, 0.5f, 1.0f };
		constexpr ImVec4 kBad{ 1.0f, 0.4f, 0.4f, 1.0f };
		constexpr ImVec4 kDim{ 0.6f, 0.6f, 0.6f, 1.0f };

		// ImGui's exact "everything left". -1 would leave a pixel, and a
		// stretch table measuring such content shrinks a pixel per frame.
		constexpr float kFillWidth = -(std::numeric_limits<float>::min)();

		[[nodiscard]] const char* Literal(const char* a_key) noexcept
		{
			return a_key ? a_key : "";
		}

		// The key of a field drawn under a_key in the current ID scope.
		[[nodiscard]] FieldKey KeyOf(const char* a_key)
		{
			return ImGui::GetID(Literal(a_key));
		}

		[[nodiscard]] float Resolve(const Width& a_width, float a_scale)
		{
			switch (a_width.mode) {
			case Width::Mode::kFill:
				return kFillWidth;
			case Width::Mode::kFit:
				return FitWidth(a_width.text) * a_scale;
			case Width::Mode::kPx:
				return a_width.pixels * a_scale;
			}
			return kFillWidth;
		}

		// A field owns the active-field mark while the user is in it and
		// gives it back when the item deactivates, so the next frame shows
		// the model again.
		void TrackActive(FieldKey a_key)
		{
			auto& state = State();
			if (ImGui::IsItemActive()) {
				state.activeField = a_key;
			} else if (state.activeField == a_key) {
				state.activeField = kNoField;
			}
		}

		void Colored(const ImVec4& a_color, std::string_view a_text)
		{
			ImGui::TextColored(a_color, "%.*s", static_cast<int>(a_text.size()), a_text.data());
		}

		// The preview's shader view, when the lab can make one; null otherwise.
		[[nodiscard]] ImTextureID PreviewOf(TextureHandle a_texture, std::uint32_t a_channel, bool a_dynamic)
		{
			if (!a_texture) {
				return nullptr;
			}
			auto* lab = TextureLab::GetSingleton();
			if (!lab) {
				return nullptr;
			}
			const auto preview = lab->Preview(a_texture, a_channel, a_dynamic);
			return preview && preview->srv ? reinterpret_cast<ImTextureID>(preview->srv) : nullptr;
		}

		// The names as "@name" entries of an open combo; returns the chosen text.
		[[nodiscard]] std::optional<std::string> ReferenceEntries(const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty)
		{
			std::optional<std::string> chosen;
			if (a_allowEmpty && ImGui::Selectable("(none)", a_current.empty())) {
				chosen = std::string{};
			}
			for (const auto& name : a_names) {
				const auto text = ReferenceText(name);
				if (ImGui::Selectable(text.c_str(), text == a_current)) {
					chosen = text;
				}
			}
			return chosen;
		}
	}

	// ------------------------------------------------------------------ types

	Width Width::Fill() noexcept
	{
		return Width{};
	}

	Width Width::Fit()
	{
		return Width{ Mode::kFit, 0.0f, {} };
	}

	Width Width::Fit(std::string_view a_text)
	{
		return Width{ Mode::kFit, 0.0f, std::string{ a_text } };
	}

	Width Width::Px(float a_pixels) noexcept
	{
		return Width{ Mode::kPx, a_pixels, {} };
	}

	Table Table::Begin(const char* a_id, std::initializer_list<Column> a_columns, const TableStyle& a_style)
	{
		return Begin(a_id, std::span<const Column>{ a_columns.begin(), a_columns.size() }, a_style);
	}

	Table Table::Begin(const char* a_id, std::span<const Column> a_columns, const TableStyle& a_style)
	{
		Table table;
		if (a_columns.empty()) {
			return table;
		}
		int flags = a_style.borders == TableStyle::Borders::kAll ? ImGuiMCP::ImGuiTableFlags_Borders : ImGuiMCP::ImGuiTableFlags_BordersInnerH;
		flags |= a_style.stretch ? ImGuiMCP::ImGuiTableFlags_SizingStretchProp : ImGuiMCP::ImGuiTableFlags_SizingFixedFit;
		if (a_style.rowBackground) {
			flags |= ImGuiMCP::ImGuiTableFlags_RowBg;
		}
		if (!ImGui::BeginTable(Literal(a_id), static_cast<int>(a_columns.size()), flags)) {
			return table;
		}
		for (const auto& column : a_columns) {
			const auto& width = column.width;
			switch (width.mode) {
			case Width::Mode::kFill:
				ImGui::TableSetupColumn(column.label, ImGuiMCP::ImGuiTableColumnFlags_WidthStretch);
				break;
			case Width::Mode::kFit:
				// A fixed column with no width sizes to its widest content.
				ImGui::TableSetupColumn(column.label, ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, width.text.empty() ? 0.0f : FitWidth(width.text));
				break;
			case Width::Mode::kPx:
				ImGui::TableSetupColumn(column.label, ImGuiMCP::ImGuiTableColumnFlags_WidthFixed, width.pixels);
				break;
			}
		}
		if (a_style.headers) {
			ImGui::TableHeadersRow();
		}
		table.open_ = true;
		table.columns_ = a_columns.size();
		return table;
	}

	void Table::Cell()
	{
		if (!open_ || columns_ == 0) {
			return;
		}
		if (cells_ % columns_ == 0) {
			ImGui::TableNextRow();
		}
		ImGui::TableNextColumn();
		++cells_;
	}

	void Table::End()
	{
		if (open_) {
			ImGui::EndTable();
		}
		open_ = false;
	}

	// ----------------------------------------------------------------- widths

	void NextItemWidth(const Width& a_width, float a_scale)
	{
		ImGui::SetNextItemWidth(Resolve(a_width, a_scale));
	}

	float FitWidth(std::string_view a_text)
	{
		const auto text = ImGui::CalcTextSize(a_text.data(), a_text.data() + a_text.size());
		// Frame padding either side, the arrow square, and a little slack.
		return text.x + ImGui::GetFrameHeight() * 2.0f + 8.0f;
	}

	float BlendWidth(std::span<const Blend> a_allowed)
	{
		float width = 0.0f;
		for (const Blend blend : a_allowed) {
			width = (std::max)(width, FitWidth(BlendName(blend)));
		}
		return width;
	}

	float ItemSpacingX()
	{
		const auto* style = ImGui::GetStyle();
		return style ? style->ItemSpacing.x : 8.0f;
	}

	float WidestOf(std::span<const std::string> a_names)
	{
		float width = 0.0f;
		for (const auto& name : a_names) {
			width = (std::max)(width, FitWidth(name));
		}
		return width;
	}

	// ----------------------------------------------------------------- fields

	std::optional<std::string> TextField(const char* a_key, const std::string& a_model, const Width& a_width, float a_scale)
	{
		auto&          state = State();
		const FieldKey key = KeyOf(a_key);
		auto&          buffer = state.textBuffers[key];
		if (state.activeField != key) {
			const auto n = (std::min)(a_model.size(), buffer.size() - 1);
			std::memcpy(buffer.data(), a_model.data(), n);
			buffer[n] = '\0';
		}
		ImGui::PushID(Literal(a_key));
		NextItemWidth(a_width, a_scale);
		const bool committed = ImGui::InputText("##text", buffer.data(), buffer.size(), ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue);
		TrackActive(key);
		ImGui::PopID();
		if (committed) {
			state.activeField = kNoField;
			return std::string{ buffer.data() };
		}
		return std::nullopt;
	}

	std::string_view LiveTextField(const char* a_key, const char* a_hint, const Width& a_width, float a_scale)
	{
		auto& buffer = State().textBuffers[KeyOf(a_key)];
		ImGui::PushID(Literal(a_key));
		NextItemWidth(a_width, a_scale);
		ImGui::InputTextWithHint("##live", a_hint, buffer.data(), buffer.size());
		ImGui::PopID();
		return std::string_view{ buffer.data() };
	}

	namespace
	{
		// A swatch the height of a field showing the colour its text names
		// (grey when it names none), opening a picker in a popup. The held
		// colour follows the text while the popup is closed and the picker
		// while it is open; a pick returns its text on release.
		[[nodiscard]] std::optional<std::string> ColorSwatchPicker(FieldKey a_key, const std::string& a_current)
		{
			auto& held = State().numberBuffers[a_key];
			if (!ImGui::IsPopupOpen("picker")) {
				const Vec3 colour = LiteralColor(a_current).value_or(Vec3{ 0.5f, 0.5f, 0.5f });
				held = { colour.x, colour.y, colour.z };
			}
			const float side = ImGui::GetFrameHeight();
			if (ImGui::ColorButton("##swatch", ImVec4{ held[0], held[1], held[2], 1.0f }, ImGuiMCP::ImGuiColorEditFlags_NoTooltip, ImVec2{ side, side })) {
				ImGui::OpenPopup("picker");
			}
			Tooltip("pick a colour; the field takes it as r, g, b");
			std::optional<std::string> picked;
			if (ImGui::BeginPopup("picker")) {
				ImGui::ColorPicker3("##picker", held.data(), ImGuiMCP::ImGuiColorEditFlags_NoSidePreview);
				if (ImGui::IsItemDeactivatedAfterEdit()) {
					picked = LiteralColorText(Vec3{ held[0], held[1], held[2] });
				}
				ImGui::EndPopup();
			}
			return picked;
		}
	}

	// ------------------------------------------------------------- thumbnails

	void Thumbnail(TextureHandle a_texture, std::uint32_t a_channel, bool a_dynamic, float a_size)
	{
		const ImVec2 size{ a_size, a_size };
		if (const auto view = PreviewOf(a_texture, a_channel, a_dynamic)) {
			ImGui::Image(view, size);
		} else {
			ImGui::Dummy(size);
		}
	}

	bool ThumbnailButton(const char* a_key, TextureHandle a_texture, std::uint32_t a_channel, bool a_dynamic, float a_size)
	{
		const ImVec2 size{ a_size, a_size };
		ImGui::PushID(Literal(a_key));
		bool clicked = false;
		if (const auto view = PreviewOf(a_texture, a_channel, a_dynamic)) {
			clicked = ImGui::ImageButton("image", view, size);
		} else {
			clicked = ImGui::Button("##blank", size);
		}
		ImGui::PopID();
		return clicked;
	}

	// ----------------------------------------------------------------- combos

	std::optional<Blend> BlendCombo(const char* a_key, std::string_view a_current, std::span<const Blend> a_allowed, const Width& a_width, float a_scale)
	{
		std::optional<Blend> chosen;
		const std::string    current{ a_current };
		ImGui::PushID(Literal(a_key));
		NextItemWidth(a_width, a_scale);
		if (ImGui::BeginCombo("##blend", current.c_str())) {
			for (const Blend blend : a_allowed) {
				const std::string name{ BlendName(blend) };
				if (ImGui::Selectable(name.c_str(), name == current)) {
					chosen = blend;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopID();
		return chosen;
	}

	bool SwitchButton(const char* a_label, bool a_active, bool a_enabled)
	{
		if (!a_enabled) {
			ImGui::BeginDisabled();
		}
		if (a_active) {
			const auto* pressed = ImGui::GetStyleColorVec4(ImGuiMCP::ImGuiCol_ButtonActive);
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, pressed ? *pressed : ImVec4{ 0.3f, 0.5f, 0.8f, 1.0f });
		}
		const bool clicked = ImGui::Button(Literal(a_label));
		if (a_active) {
			ImGui::PopStyleColor();
		}
		if (!a_enabled) {
			ImGui::EndDisabled();
		}
		return clicked && a_enabled;
	}

	std::optional<std::string> ChoiceCombo(const char* a_key, const std::string& a_current, std::span<const std::string> a_names, const Width& a_width, float a_scale)
	{
		std::optional<std::string> chosen;
		ImGui::PushID(Literal(a_key));
		NextItemWidth(a_width, a_scale);
		if (ImGui::BeginCombo("##choice", a_current.c_str())) {
			for (const auto& name : a_names) {
				if (ImGui::Selectable(name.c_str(), name == a_current)) {
					chosen = name;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::PopID();
		return chosen;
	}

	std::optional<std::string> ReferenceCombo(const char* a_key, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, const Width& a_width, float a_scale)
	{
		std::optional<std::string> chosen;
		ImGui::PushID(Literal(a_key));
		NextItemWidth(a_width, a_scale);
		if (ImGui::BeginCombo("##reference", a_current.empty() ? "(none)" : a_current.c_str())) {
			chosen = ReferenceEntries(a_current, a_names, a_allowEmpty);
			ImGui::EndCombo();
		}
		ImGui::PopID();
		return chosen;
	}

	// ----------------------------------------------------------------- badges

	namespace
	{
		struct BadgeStyle
		{
			const char* glyph;
			bool        takesSignal;  // an "@" mark follows: a @signal stands in for the value
			ImVec4      colour;
			const char* rule;
		};

		constexpr ImVec4 kValueBlue{ 0.55f, 0.80f, 1.00f, 1.0f };
		constexpr ImVec4 kColourOrange{ 1.00f, 0.70f, 0.45f, 1.0f };
		constexpr ImVec4 kVectorTeal{ 0.55f, 0.95f, 0.80f, 1.0f };
		constexpr ImVec4 kReferenceGreen{ 0.60f, 0.95f, 0.60f, 1.0f };
		constexpr ImVec4 kCodeYellow{ 1.00f, 0.90f, 0.45f, 1.0f };
		constexpr ImVec4 kCurveCyan{ 0.55f, 0.95f, 0.95f, 1.0f };
		constexpr ImVec4 kMaskViolet{ 0.85f, 0.65f, 1.00f, 1.0f };
		constexpr ImVec4 kChannelGrey{ 0.85f, 0.85f, 0.85f, 1.0f };
		constexpr ImVec4 kBadgeFrame{ 0.20f, 0.20f, 0.24f, 1.0f };

		// Values (blue, orange, teal) take a @signal in their place and say
		// so with the mark; the rest are what they are.
		BadgeStyle StyleOf(FieldKind a_kind) noexcept
		{
			switch (a_kind) {
			case FieldKind::kScalar:
				return { "#", true, kValueBlue, "scalar: a number, or @signal of scalar type" };
			case FieldKind::kColor:
				return { "c", true, kColourOrange, "colour: r, g, b in 0..1, or one number for all three, or @signal of colour type" };
			case FieldKind::kVector:
				return { "v", true, kVectorTeal, "vector: x, y, z (a position, direction or scale), or one number for all three, or @signal of vector type" };
			case FieldKind::kReference:
				return { "@", false, kReferenceGreen, "reference: @name of a row of the recipe" };
			case FieldKind::kExpression:
				return { "=", false, kCodeYellow, "expression: numbers, [r, g, b], @signals, + - * /, comparisons, and/or/not, if(c, a, b), abs min max clamp saturate floor ceil frac sqrt pow sin cos step smoothstep lerp, time, pi" };
			case FieldKind::kCurve:
				return { "x", false, kCurveCyan, "curve: an expression in x (mean is the source's mean), or @curve" };
			case FieldKind::kMask:
				return { "m", false, kMaskViolet, "mask: an expression per texel where @source and @mask names are images and @signals are this tick's values" };
			case FieldKind::kChannels:
				return { "ch", false, kChannelGrey, "channels: any of r g b a, in any order" };
			case FieldKind::kToggle:
				return { "?", false, kChannelGrey, "on or off" };
			case FieldKind::kChoice:
				return { "o", false, kChannelGrey, "one of the listed values" };
			case FieldKind::kText:
				return { "\"", false, kChannelGrey, "text" };
			}
			return { "?", false, kDim, "" };
		}

		// Filled (the kind's colour behind a dark glyph) when a signal can
		// drive the field; outlined (a dark square with the glyph in the kind's
		// colour) when the value is literal. One character either way.
		void BadgeFrame(const char* a_label, const ImVec4& a_colour, bool a_filled, float a_width, float a_height)
		{
			const ImVec4 back = a_filled ? a_colour : kBadgeFrame;
			const ImVec4 text = a_filled ? kBadgeFrame : a_colour;
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, back);
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, back);
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, back);
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, text);
			ImGui::Button(a_label, ImVec2{ a_width, a_height });
			ImGui::PopStyleColor(4);
		}
	}

	void Badge(FieldKind a_kind)
	{
		const auto  style = StyleOf(a_kind);
		const float side = ImGui::GetFrameHeight();
		// One square the height of the field, flush against it: filled when a
		// signal can stand in for the value, outlined otherwise.
		BadgeFrame(style.glyph, style.colour, style.takesSignal, side, side);
		Tooltip(style.rule);
		ImGui::SameLine(0.0f, 0.0f);
	}

	std::optional<std::string> ValueField(const char* a_key, FieldKind a_kind, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, float a_scale)
	{
		auto&          state = State();
		const auto     style = StyleOf(a_kind);
		const float    side = ImGui::GetFrameHeight();
		const bool     takesSignal = style.takesSignal && !a_names.empty();
		const bool     reference = a_current.starts_with('@');
		const FieldKey key = KeyOf(a_key);
		auto           mode = state.comboMode.find(key);
		if (mode == state.comboMode.end()) {
			mode = state.comboMode.emplace(key, reference).first;
		}
		ImGui::PushID(Literal(a_key));
		// The badge: a button when a signal may stand in, inert otherwise.
		if (takesSignal) {
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, style.colour);
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, ImVec4{ style.colour.x * 0.85f, style.colour.y * 0.85f, style.colour.z * 0.85f, 1.0f });
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, ImVec4{ style.colour.x * 0.7f, style.colour.y * 0.7f, style.colour.z * 0.7f, 1.0f });
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kBadgeFrame);
			if (ImGui::Button(style.glyph, ImVec2{ side, side })) {
				mode->second = !mode->second;
				state.focusField = key;
			}
			ImGui::PopStyleColor(4);
			Tooltip(std::string{ style.rule } + (mode->second ? "\nclick: type a value instead" : "\nclick: choose a signal instead"));
		} else {
			BadgeFrame(style.glyph, style.colour, false, side, side);
			Tooltip(style.rule);
		}
		ImGui::SameLine(0.0f, 0.0f);

		std::optional<std::string> chosen;
		if (state.focusField == key) {
			state.focusField = kNoField;
			ImGui::SetKeyboardFocusHere();
		}
		if (mode->second && takesSignal) {
			const std::string preview = reference ? a_current : std::string{ "choose a signal" };
			NextItemWidth(Width::Fill());
			if (ImGui::BeginCombo("##combo", preview.c_str())) {
				chosen = ReferenceEntries(a_current, a_names, a_allowEmpty);
				ImGui::EndCombo();
			}
		} else {
			if (a_kind == FieldKind::kColor) {
				chosen = ColorSwatchPicker(key, a_current);
				ImGui::SameLine(0.0f, 0.0f);
			}
			if (const auto typed = TextField("text", a_current, Width::Fill(), a_scale)) {
				chosen = typed;
			}
		}
		ImGui::PopID();
		return chosen;
	}

	bool DetailButton()
	{
		const float side = ImGui::GetFrameHeight();
		const bool  clicked = ImGui::Button("...", ImVec2{ side, side });
		Tooltip("open this field's details");
		ImGui::SameLine(0.0f, 0.0f);
		return clicked;
	}

	std::string ValueText(const Value& a_value)
	{
		return Match(
			a_value,
			[](float f) { return std::format("{:.3f}", f); },
			[](const Vec2& v) { return std::format("({:.3f}, {:.3f})", v.x, v.y); },
			[](const Vec3& v) { return std::format("({:.3f}, {:.3f}, {:.3f})", v.x, v.y, v.z); });
	}

	void ValueSwatch(const Value& a_value)
	{
		if (const auto* c = Get<Vec3>(a_value)) {
			ImGui::ColorButton("##swatch", ImVec4{ c->x, c->y, c->z, 1.0f }, 0, ImVec2{ 16.0f, 16.0f });
			ImGui::SameLine();
		}
		ImGui::TextUnformatted(ValueText(a_value).c_str());
	}

	// ----------------------------------------------------------------- layout

	bool ModeBar(Mode& a_mode)
	{
		const Mode before = a_mode;
		if (ImGui::BeginTabBar("modes")) {
			for (const Mode mode : kModes) {
				const std::string name{ ModeName(mode) };
				if (ImGui::BeginTabItem(name.c_str())) {
					a_mode = mode;
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
		return a_mode != before;
	}

	bool Section(const char* a_title, bool a_openByDefault)
	{
		return ImGui::CollapsingHeader(Literal(a_title), a_openByDefault ? ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen : 0);
	}

	void Split(const char* a_id, float& a_ratio, const std::function<void()>& a_left, const std::function<void()>& a_right)
	{
		const float ratio = std::clamp(a_ratio, 0.05f, 0.95f);
		if (!ImGui::BeginTable(Literal(a_id), 2, ImGuiMCP::ImGuiTableFlags_Resizable | ImGuiMCP::ImGuiTableFlags_BordersInnerV | ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
			return;
		}
		// The weights set the split when the table first appears; after that
		// ImGui keeps the widths the user drags, and the cells' widths read
		// them back into the ratio.
		ImGui::TableSetupColumn("left", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, ratio);
		ImGui::TableSetupColumn("right", ImGuiMCP::ImGuiTableColumnFlags_WidthStretch, 1.0f - ratio);
		ImGui::TableNextRow();
		ImGui::TableNextColumn();
		const float left = ImGui::GetContentRegionAvail().x;
		if (a_left) {
			a_left();
		}
		ImGui::TableNextColumn();
		const float right = ImGui::GetContentRegionAvail().x;
		if (a_right) {
			a_right();
		}
		ImGui::EndTable();
		if (left + right > 0.0f) {
			a_ratio = left / (left + right);
		}
	}

	namespace
	{
		// The text at the left, or a frame's height of nothing; then the
		// item, moved to the right edge of the line.
		void DrawRuleLine(const RuleLine& a_line)
		{
			if (a_line.text.empty()) {
				ImGui::Dummy(ImVec2{ 0.0f, ImGui::GetFrameHeight() });
			} else {
				ImGui::AlignTextToFramePadding();
				ImGui::TextUnformatted(a_line.text.data(), a_line.text.data() + a_line.text.size());
			}
			if (!a_line.right || a_line.rightWidth <= 0.0f) {
				return;
			}
			ImGui::SameLine();
			const float here = ImGui::GetCursorPosX();
			const float edge = here + ImGui::GetContentRegionAvail().x - a_line.rightWidth;
			ImGui::SetCursorPosX((std::max)(here, edge));
			a_line.right();
		}
	}

	void Rule(const RuleLine& a_above, const RuleLine& a_below)
	{
		DrawRuleLine(a_above);
		ImGui::Separator();
		DrawRuleLine(a_below);
	}

	float RuleHeight()
	{
		// Two frame-high lines and the separator's line, each followed by the
		// item spacing.
		const auto* style = ImGui::GetStyle();
		const float spacing = style ? style->ItemSpacing.y : 4.0f;
		return ImGui::GetFrameHeight() * 2.0f + 1.0f + spacing * 3.0f;
	}

	bool Toggle(const char* a_label, bool& a_value, std::string_view a_tooltip)
	{
		const bool changed = ImGui::Checkbox(Literal(a_label), &a_value);
		Tooltip(a_tooltip);
		return changed;
	}

	bool SoloMute(bool& a_solo, bool* a_mute)
	{
		bool changed = Toggle("S##solo", a_solo, "solo: show this alone");
		if (a_mute) {
			ImGui::SameLine();
			changed = Toggle("M##mute", *a_mute, "mute: hide this") || changed;
		}
		return changed;
	}

	// ------------------------------------------------------------- reordering

	bool DragHandle(const char* a_type, std::size_t a_index)
	{
		const bool clicked = ImGui::SmallButton("::");
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload(a_type, &a_index, sizeof(a_index));
			ImGui::Text("layer %zu", a_index);
			ImGui::EndDragDropSource();
		}
		return clicked;
	}

	std::optional<RowMove> DropTarget(const char* a_type, std::size_t a_index)
	{
		std::optional<RowMove> move;
		if (ImGui::BeginDragDropTarget()) {
			if (const auto* payload = ImGui::AcceptDragDropPayload(a_type)) {
				// The payload is bytes ImGui copied; only a whole index is a row.
				if (payload->Data && payload->DataSize == static_cast<int>(sizeof(std::size_t))) {
					std::size_t from = 0;
					std::memcpy(&from, payload->Data, sizeof(from));
					if (from != a_index) {
						move = RowMove{ from, a_index };
					}
				}
			}
			ImGui::EndDragDropTarget();
		}
		return move;
	}

	// ------------------------------------------------------------------- text

	void Problem(std::string_view a_text)
	{
		Colored(kBad, a_text);
	}

	void Warn(std::string_view a_text)
	{
		Colored(kWarn, a_text);
	}

	void Ok(std::string_view a_text)
	{
		Colored(kOk, a_text);
	}

	void Dim(std::string_view a_text)
	{
		Colored(kDim, a_text);
	}

	void HelpMarker(const char* a_text)
	{
		if (!a_text || !*a_text) {
			return;
		}
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("%s", a_text);
		}
	}

	void Tooltip(std::string_view a_text)
	{
		if (a_text.empty() || !ImGui::IsItemHovered(ImGuiMCP::ImGuiHoveredFlags_AllowWhenDisabled)) {
			return;
		}
		ImGui::SetTooltip("%.*s", static_cast<int>(a_text.size()), a_text.data());
	}
}
