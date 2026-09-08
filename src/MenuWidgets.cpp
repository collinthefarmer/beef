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

namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImGuiID;
using ImGuiMCP::ImTextureID;
using ImGuiMCP::ImVec2;
using ImGuiMCP::ImVec4;

static_assert(std::is_same_v<WornEnchantmentPBR::Studio::FieldKey, ImGuiID>);

namespace WornEnchantmentPBR::Studio::Widgets
{
	namespace
	{
		constexpr ImVec4 kWarn{ 1.0f, 0.8f, 0.3f, 1.0f };
		constexpr ImVec4 kProblemFrame{ 0.45f, 0.12f, 0.12f, 1.0f };
		constexpr ImVec4 kOk{ 0.5f, 0.9f, 0.5f, 1.0f };
		constexpr ImVec4 kBad{ 1.0f, 0.4f, 0.4f, 1.0f };
		constexpr ImVec4 kDim{ 0.6f, 0.6f, 0.6f, 1.0f };

		constexpr float kFillWidth = -(std::numeric_limits<float>::min)();

		[[nodiscard]] const char* Literal(const char* a_key) noexcept
		{
			return a_key ? a_key : "";
		}

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

		[[nodiscard]] ImTextureID PreviewOf(TextureHandle a_texture, ShaderChannel a_channel, bool a_dynamic)
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

		[[nodiscard]] std::optional<std::string> ReferenceEntries(const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, std::span<const std::string> a_creators)
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
			if (!a_creators.empty()) {
				ImGui::Separator();
				for (const auto& creator : a_creators) {
					if (ImGui::Selectable(creator.c_str(), false)) {
						chosen = creator;
					}
				}
			}
			return chosen;
		}
	}

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
		int flags = 0;
		switch (a_style.borders) {
		case TableStyle::Borders::kAll:
			flags = ImGuiMCP::ImGuiTableFlags_Borders;
			break;
		case TableStyle::Borders::kInnerHorizontal:
			flags = ImGuiMCP::ImGuiTableFlags_BordersInnerH;
			break;
		case TableStyle::Borders::kNone:
			break;
		}
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

	void NextItemWidth(const Width& a_width, float a_scale)
	{
		ImGui::SetNextItemWidth(Resolve(a_width, a_scale));
	}

	float FitWidth(std::string_view a_text)
	{
		const auto text = ImGui::CalcTextSize(a_text.data(), a_text.data() + a_text.size());
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

	float ButtonWidth(std::string_view a_text)
	{
		const auto* style = ImGui::GetStyle();
		const auto  text = ImGui::CalcTextSize(a_text.data(), a_text.data() + a_text.size());
		return text.x + (style ? style->FramePadding.x : 4.0f) * 2.0f;
	}

	float TextWidth(std::string_view a_text)
	{
		return ImGui::CalcTextSize(a_text.data(), a_text.data() + a_text.size()).x;
	}

	float CheckboxWidth(std::string_view a_text)
	{
		const auto* style = ImGui::GetStyle();
		const auto  text = ImGui::CalcTextSize(a_text.data(), a_text.data() + a_text.size());
		return ImGui::GetFrameHeight() + (style ? style->ItemInnerSpacing.x : 4.0f) + text.x;
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

	namespace
	{
		void ProblemLabel(std::string_view a_text)
		{
			const ImVec2 itemMin = ImGui::GetItemRectMin();
			const ImVec2 itemMax = ImGui::GetItemRectMax();
			const ImVec2 windowMin = ImGui::GetWindowPos();
			const ImVec2 windowSize = ImGui::GetWindowSize();
			const ImVec2 windowMax{ windowMin.x + windowSize.x, windowMin.y + windowSize.y };
			const float  pad = ImGui::GetStyle()->FramePadding.x;
			const float  gap = 2.0f;
			const float  wrap = (std::max)(windowMax.x - itemMin.x - 2.0f * pad, 4.0f * ImGui::GetFontSize());
			const ImVec2 text = ImGui::CalcTextSize(a_text.data(), a_text.data() + a_text.size(), false, wrap);
			const float  height = text.y + 2.0f * pad;
			const bool   below = itemMax.y + gap + height <= windowMax.y;
			const ImVec2 boxMin{ itemMin.x, below ? itemMax.y + gap : itemMin.y - gap - height };
			const ImVec2 boxMax{ itemMin.x + text.x + 2.0f * pad, boxMin.y + height };
			auto* const  list = ImGui::GetForegroundDrawList();
			ImGui::ImDrawListManager::PushClipRect(list, windowMin, windowMax, false);
			ImGui::ImDrawListManager::AddRectFilled(list, boxMin, boxMax, ImGui::GetColorU32(ImGuiMCP::ImGuiCol_PopupBg, 0.95f), 0.0f, 0);
			ImGui::ImDrawListManager::AddText(list, ImGui::GetFont(), ImGui::GetFontSize(), ImVec2{ boxMin.x + pad, boxMin.y + pad }, ImGui::GetColorU32(kBad), a_text.data(), a_text.data() + a_text.size(), wrap);
			ImGui::ImDrawListManager::PopClipRect(list);
		}
	}

	std::optional<std::string> TextField(const char* a_key, const std::string& a_model, const Width& a_width, float a_scale, const TextCheck& a_check)
	{
		auto&          state = State();
		const FieldKey key = KeyOf(a_key);
		auto&          buffer = state.textBuffers[key];
		if (state.activeField != key) {
			const auto n = (std::min)(a_model.size(), buffer.size() - 1);
			std::memcpy(buffer.data(), a_model.data(), n);
			buffer[n] = '\0';
		}
		const std::optional<std::string> problem = (a_check && state.activeField == key) ? a_check(std::string{ buffer.data() }) : std::nullopt;
		ImGui::PushID(Literal(a_key));
		if (problem) {
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_FrameBg, kProblemFrame);
		}
		NextItemWidth(a_width, a_scale);
		const bool committed = ImGui::InputText("##text", buffer.data(), buffer.size(), ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue);
		if (problem) {
			ImGui::PopStyleColor();
		}
		TrackActive(key);
		if (problem) {
			ProblemLabel(*problem);
		}
		std::optional<std::string> result;
		if (committed) {
			std::string text{ buffer.data() };
			if (a_check && a_check(text)) {
				state.activeField = key;
				ImGui::SetKeyboardFocusHere(-1);
			} else {
				state.activeField = kNoField;
				result = std::move(text);
			}
		}
		ImGui::PopID();
		return result;
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

	void Thumbnail(TextureHandle a_texture, ShaderChannel a_channel, bool a_dynamic, float a_size)
	{
		const ImVec2 size{ a_size, a_size };
		if (const auto view = PreviewOf(a_texture, a_channel, a_dynamic)) {
			ImGui::Image(view, size);
		} else {
			ImGui::Dummy(size);
		}
	}

	bool ThumbnailButton(const char* a_key, TextureHandle a_texture, ShaderChannel a_channel, bool a_dynamic, float a_size)
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

	std::optional<std::string> ReferenceCombo(const char* a_key, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, const Width& a_width, float a_scale, std::span<const std::string> a_creators)
	{
		std::optional<std::string> chosen;
		ImGui::PushID(Literal(a_key));
		NextItemWidth(a_width, a_scale);
		if (ImGui::BeginCombo("##reference", a_current.empty() ? "(none)" : a_current.c_str())) {
			chosen = ReferenceEntries(a_current, a_names, a_allowEmpty, a_creators);
			ImGui::EndCombo();
		}
		ImGui::PopID();
		return chosen;
	}

	namespace
	{
		struct BadgeStyle
		{
			const char* glyph;
			bool        takesSignal;
			ImVec4      colour;
			const char* rule;
			Swatch      swatch;
		};

		constexpr ImVec4 kBadgeFrame{ 0.20f, 0.20f, 0.24f, 1.0f };

		ImVec4 ColourOf(FieldKind a_kind) noexcept
		{
			const auto* row = RowOf(kFieldKinds, a_kind);
			return row ? ImVec4{ row->colour[0], row->colour[1], row->colour[2], 1.0f } : kDim;
		}

		BadgeStyle StyleOf(FieldKind a_kind) noexcept
		{
			const auto* row = RowOf(kFieldKinds, a_kind);
			if (!row) {
				return { "?", false, kDim, "", Swatch::kNone };
			}
			return { row->glyph, row->takesSignal, ColourOf(a_kind), row->rule, row->swatch };
		}

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
		BadgeFrame(style.glyph, style.colour, style.takesSignal, side, side);
		Tooltip(style.rule);
		ImGui::SameLine(0.0f, 0.0f);
	}

	std::optional<std::string> ValueField(const char* a_key, FieldKind a_kind, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, float a_scale, const TextCheck& a_check, std::span<const std::string> a_creators)
	{
		auto&          state = State();
		const auto     style = StyleOf(a_kind);
		const float    side = ImGui::GetFrameHeight();
		const bool     takesSignal = style.takesSignal && (!a_names.empty() || !a_creators.empty());
		const bool     reference = a_current.starts_with('@');
		const FieldKey key = KeyOf(a_key);
		auto           mode = state.comboMode.find(key);
		if (mode == state.comboMode.end()) {
			mode = state.comboMode.emplace(key, reference).first;
		}
		ImGui::PushID(Literal(a_key));
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
				chosen = ReferenceEntries(a_current, a_names, a_allowEmpty, a_creators);
				ImGui::EndCombo();
			}
		} else {
			if (style.swatch == Swatch::kAlways || (style.swatch == Swatch::kWhenColour && LiteralColor(a_current))) {
				chosen = ColorSwatchPicker(key, a_current);
				ImGui::SameLine(0.0f, 0.0f);
			}
			if (const auto typed = TextField("text", a_current, Width::Fill(), a_scale, a_check)) {
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

	bool ModeBar(Mode& a_mode, Mode& a_drawn)
	{
		const Mode before = a_mode;
		const bool setByState = a_mode != a_drawn;
		if (ImGui::BeginTabBar("modes")) {
			for (const Mode mode : kModes) {
				const std::string name{ ModeName(mode) };
				const auto        flags = setByState && mode == before ? ImGuiMCP::ImGuiTabItemFlags_SetSelected : ImGuiMCP::ImGuiTabItemFlags_None;
				if (ImGui::BeginTabItem(name.c_str(), nullptr, flags)) {
					if (!setByState) {
						a_mode = mode;
					}
					ImGui::EndTabItem();
				}
			}
			ImGui::EndTabBar();
		}
		a_drawn = a_mode;
		return a_mode != before;
	}

	bool Section(const char* a_title, bool a_openByDefault)
	{
		return ImGui::CollapsingHeader(Literal(a_title), a_openByDefault ? ImGuiMCP::ImGuiTreeNodeFlags_DefaultOpen : 0);
	}

	std::optional<float> Split(const char* a_id, float a_ratio, const std::function<void()>& a_left, const std::function<void()>& a_right)
	{
		const float ratio = std::clamp(a_ratio, 0.05f, 0.95f);
		if (!ImGui::BeginTable(Literal(a_id), 2, ImGuiMCP::ImGuiTableFlags_Resizable | ImGuiMCP::ImGuiTableFlags_BordersInnerV | ImGuiMCP::ImGuiTableFlags_SizingStretchProp)) {
			return std::nullopt;
		}
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
		if (left + right <= 0.0f) {
			return std::nullopt;
		}
		const float now = left / (left + right);
		return std::abs(now - ratio) > 0.0005f ? std::optional{ now } : std::nullopt;
	}

	namespace
	{
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
			ImGui::PushID(a_line.text.data(), a_line.text.data() + a_line.text.size());
			a_line.right();
			ImGui::PopID();
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

	void DetailModal(const char* a_title, const std::function<void()>& a_body)
	{
		ImGui::SetNextWindowSizeConstraints(ImVec2{ 480.0f, 0.0f }, ImVec2{ 960.0f, 800.0f });
		if (!ImGui::BeginPopupModal(Literal(a_title), nullptr, ImGuiMCP::ImGuiWindowFlags_AlwaysAutoResize)) {
			return;
		}
		a_body();
		if (ImGui::Button("close")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ChooserPick ChooserRow(Table& a_table, std::span<const std::string_view> a_leading, std::string_view a_name, std::string_view a_detail, std::optional<float> a_share, const std::optional<std::string>& a_unavailable, const char* a_action)
	{
		const bool disabled = a_unavailable.has_value();
		if (disabled) {
			ImGui::BeginDisabled();
		}
		for (const auto text : a_leading) {
			a_table.Cell();
			Dim(text);
		}
		a_table.Cell();
		const std::string name{ a_name };
		const bool        clicked = ImGui::Selectable(name.c_str(), false, ImGuiMCP::ImGuiSelectableFlags_SpanAllColumns);
		if (disabled) {
			ImGui::EndDisabled();
			Tooltip(*a_unavailable);
		}
		a_table.Cell();
		Dim(a_detail);
		a_table.Cell();
		if (a_share) {
			ImGui::Text("%.0f%%", *a_share * 100.0f);
		}
		a_table.Cell();
		if (a_action && ImGui::SmallButton(a_action)) {
			return ChooserPick::kAction;
		}
		return clicked && !disabled ? ChooserPick::kChosen : ChooserPick::kNone;
	}

	void RightAligned(float a_width, const std::function<void()>& a_draw)
	{
		const float here = ImGui::GetCursorPosX();
		const float edge = here + ImGui::GetContentRegionAvail().x - a_width;
		ImGui::SetCursorPosX((std::max)(here, edge));
		a_draw();
	}

	void Disabled(bool a_disabled, const std::function<void()>& a_draw)
	{
		if (a_disabled) {
			ImGui::BeginDisabled();
		}
		a_draw();
		if (a_disabled) {
			ImGui::EndDisabled();
		}
	}

	void HeldLabel(const char* a_text)
	{
		ImGui::BeginDisabled();
		ImGui::Button(Literal(a_text));
		ImGui::EndDisabled();
	}

	bool LitButton(const char* a_label, bool a_lit)
	{
		if (a_lit) {
			const auto* pressed = ImGui::GetStyleColorVec4(ImGuiMCP::ImGuiCol_ButtonActive);
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, pressed ? *pressed : ImVec4{ 0.3f, 0.5f, 0.8f, 1.0f });
		}
		const bool clicked = ImGui::Button(Literal(a_label));
		if (a_lit) {
			ImGui::PopStyleColor();
		}
		return clicked;
	}

	float RowButtonWidth()
	{
		return ImGui::GetFrameHeight();
	}

	namespace
	{
		bool SquareToggle(const char* a_label, bool& a_value, std::string_view a_tooltip)
		{
			const float side = RowButtonWidth();
			if (a_value) {
				const auto* pressed = ImGui::GetStyleColorVec4(ImGuiMCP::ImGuiCol_ButtonActive);
				ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, pressed ? *pressed : ImVec4{ 0.3f, 0.5f, 0.8f, 1.0f });
			}
			const bool clicked = ImGui::Button(Literal(a_label), ImVec2{ side, side });
			if (a_value) {
				ImGui::PopStyleColor();
			}
			Tooltip(a_tooltip);
			if (clicked) {
				a_value = !a_value;
			}
			return clicked;
		}
	}

	bool RemoveButton(std::size_t a_references)
	{
		const float side = RowButtonWidth();
		if (a_references > 0) {
			ImGui::BeginDisabled();
		}
		const bool clicked = ImGui::Button("X", ImVec2{ side, side }) && a_references == 0;
		if (a_references > 0) {
			ImGui::EndDisabled();
		}
		Tooltip(a_references > 0 ? std::format("referenced in {} place(s)", a_references) : "remove this row");
		return clicked;
	}

	bool SoloButton(bool& a_solo)
	{
		return SquareToggle("S", a_solo, "solo: show this alone");
	}

	bool MuteButton(bool& a_mute)
	{
		return SquareToggle("M", a_mute, "mute: hide this");
	}

	SoloMuteChange SoloMute(bool& a_solo, bool& a_mute)
	{
		SoloMuteChange changed = SoloButton(a_solo) ? SoloMuteChange::kSolo : SoloMuteChange::kNone;
		ImGui::SameLine();
		if (MuteButton(a_mute)) {
			changed = SoloMuteChange::kMute;
		}
		return changed;
	}

	bool DragHandle(const char* a_type, std::size_t a_index, const char* a_noun)
	{
		const float side = RowButtonWidth();
		const bool  clicked = ImGui::Button("::", ImVec2{ side, side });
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload(a_type, &a_index, sizeof(a_index));
			ImGui::Text("%s %zu", Literal(a_noun), a_index);
			ImGui::EndDragDropSource();
		}
		return clicked;
	}

	std::optional<RowMove> DropTarget(const char* a_type, std::size_t a_index)
	{
		std::optional<RowMove> move;
		if (ImGui::BeginDragDropTarget()) {
			if (const auto* payload = ImGui::AcceptDragDropPayload(a_type)) {
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
