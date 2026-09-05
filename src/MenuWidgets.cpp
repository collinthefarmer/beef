#include "MenuWidgets.h"

#include "MenuState.h"
#include "RuntimeTextures.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <limits>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wmismatched-tags"
#include "extern/SKSEMenuFramework.h"
#pragma clang diagnostic pop

// The SDK header keeps the ImGui wrappers and types in ImGuiMCP.
namespace ImGui = ImGuiMCP;
using ImGuiMCP::ImTextureID;
using ImGuiMCP::ImVec2;
using ImGuiMCP::ImVec4;

namespace WornEnchantmentPBR::Studio::Widgets
{
	namespace
	{
		constexpr ImVec4 kWarn{ 1.0f, 0.8f, 0.3f, 1.0f };
		constexpr ImVec4 kOk{ 0.5f, 0.9f, 0.5f, 1.0f };
		constexpr ImVec4 kBad{ 1.0f, 0.4f, 0.4f, 1.0f };
		constexpr ImVec4 kDim{ 0.6f, 0.6f, 0.6f, 1.0f };

		// ImGui reads a width of -FLT_MIN as "everything left"; a fill request
		// is not scaled.
		[[nodiscard]] float ScaledWidth(float a_width, float a_scale) noexcept
		{
			return a_width < 0.0f ? kFillWidth : a_width * a_scale;
		}

		// The width a combo needs to show a_text whole; scale applies to the text.
		[[nodiscard]] float ComboWidth(float a_width, std::string_view a_text, float a_scale)
		{
			return a_width == kFitWidth ? FitWidth(a_text) * a_scale : ScaledWidth(a_width, a_scale);
		}

		[[nodiscard]] std::string Label(const std::string& a_key)
		{
			return "##" + a_key;
		}

		// A field owns the active-field mark while the user is in it and
		// gives it back when the item deactivates, so the next frame shows
		// the model again.
		void TrackActive(const std::string& a_key)
		{
			auto& state = State();
			if (ImGui::IsItemActive()) {
				state.activeField = a_key;
			} else if (state.activeField == a_key) {
				state.activeField.clear();
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
	}

	// ----------------------------------------------------------------- fields

	std::optional<std::string> TextField(const std::string& a_key, const std::string& a_model, float a_width, float a_scale)
	{
		auto& state = State();
		auto& buffer = state.textBuffers[a_key];
		if (state.activeField != a_key) {
			const auto n = (std::min)(a_model.size(), buffer.size() - 1);
			std::memcpy(buffer.data(), a_model.data(), n);
			buffer[n] = '\0';
		}
		ImGui::SetNextItemWidth(ScaledWidth(a_width, a_scale));
		const bool committed = ImGui::InputText(Label(a_key).c_str(), buffer.data(), buffer.size(), ImGuiMCP::ImGuiInputTextFlags_EnterReturnsTrue);
		TrackActive(a_key);
		if (committed) {
			state.activeField.clear();
			return std::string{ buffer.data() };
		}
		return std::nullopt;
	}

	std::optional<float> DragField(const std::string& a_key, float a_value, float a_width, float a_scale, float a_speed)
	{
		auto& state = State();
		auto& held = state.numberBuffers[a_key];
		if (state.activeField != a_key) {
			held[0] = a_value;
		}
		ImGui::SetNextItemWidth(ScaledWidth(a_width, a_scale));
		ImGui::DragFloat(Label(a_key).c_str(), &held[0], a_speed, 0.0f, 0.0f, "%.3f");
		const bool released = ImGui::IsItemDeactivatedAfterEdit();
		TrackActive(a_key);
		if (released) {
			state.activeField.clear();
			return held[0];
		}
		return std::nullopt;
	}

	std::optional<Vec3> ColorField(const std::string& a_key, const Vec3& a_value, float a_width, float a_scale)
	{
		auto& state = State();
		auto& held = state.numberBuffers[a_key];
		if (state.activeField != a_key) {
			held = { a_value.x, a_value.y, a_value.z };
		}
		ImGui::SetNextItemWidth(ScaledWidth(a_width, a_scale));
		ImGui::ColorEdit3(Label(a_key).c_str(), held.data());
		const bool released = ImGui::IsItemDeactivatedAfterEdit();
		TrackActive(a_key);
		if (released) {
			state.activeField.clear();
			return Vec3{ held[0], held[1], held[2] };
		}
		return std::nullopt;
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

	bool ThumbnailButton(const std::string& a_key, TextureHandle a_texture, std::uint32_t a_channel, bool a_dynamic, float a_size)
	{
		const ImVec2 size{ a_size, a_size };
		if (const auto view = PreviewOf(a_texture, a_channel, a_dynamic)) {
			return ImGui::ImageButton(a_key.c_str(), view, size);
		}
		return ImGui::Button(Label(a_key).c_str(), size);
	}

	// ----------------------------------------------------------------- combos

	float FitWidth(std::string_view a_text)
	{
		const auto text = ImGui::CalcTextSize(a_text.data(), a_text.data() + a_text.size());
		// Frame padding either side, the arrow square, and a little slack.
		return text.x + ImGui::GetFrameHeight() * 2.0f + 8.0f;
	}

	std::optional<Blend> BlendCombo(const std::string& a_key, std::string_view a_current, std::span<const Blend> a_allowed, float a_width, float a_scale)
	{
		std::optional<Blend> chosen;
		const std::string    current{ a_current };
		ImGui::SetNextItemWidth(ComboWidth(a_width, current, a_scale));
		if (ImGui::BeginCombo(Label(a_key).c_str(), current.c_str())) {
			for (const Blend blend : a_allowed) {
				const std::string name{ BlendName(blend) };
				if (ImGui::Selectable(name.c_str(), name == current)) {
					chosen = blend;
				}
			}
			ImGui::EndCombo();
		}
		return chosen;
	}

	float BlendWidth(std::span<const Blend> a_allowed)
	{
		float width = 0.0f;
		for (const Blend blend : a_allowed) {
			width = (std::max)(width, FitWidth(BlendName(blend)));
		}
		return width;
	}

	std::optional<std::string> ReferenceCombo(const std::string& a_key, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, float a_width, float a_scale)
	{
		std::optional<std::string> chosen;
		ImGui::SetNextItemWidth(ComboWidth(a_width, a_current.empty() ? std::string_view{ "(none)" } : std::string_view{ a_current }, a_scale));
		if (ImGui::BeginCombo(Label(a_key).c_str(), a_current.empty() ? "(none)" : a_current.c_str())) {
			if (a_allowEmpty && ImGui::Selectable("(none)", a_current.empty())) {
				chosen = std::string{};
			}
			for (const auto& name : a_names) {
				const auto text = ReferenceText(name);
				if (ImGui::Selectable(text.c_str(), text == a_current)) {
					chosen = text;
				}
			}
			ImGui::EndCombo();
		}
		return chosen;
	}

	float WidestOf(std::span<const std::string> a_names)
	{
		float width = 0.0f;
		for (const auto& name : a_names) {
			width = (std::max)(width, FitWidth(name));
		}
		return width;
	}

	// ------------------------------------------------------------------ modes

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

	// ------------------------------------------------------------- reordering

	bool DragHandle(const char* a_type, std::size_t a_index)
	{
		const bool clicked = ImGui::SmallButton(std::format("::##drag{}", a_index).c_str());
		if (ImGui::BeginDragDropSource()) {
			ImGui::SetDragDropPayload(a_type, &a_index, sizeof(a_index));
			ImGui::Text("layer %zu", a_index);
			ImGui::EndDragDropSource();
		}
		return clicked;
	}

	bool SoloMute(bool& a_solo, bool* a_mute)
	{
		bool changed = ImGui::Checkbox("S##solo", &a_solo);
		Tooltip("solo: show this alone");
		if (a_mute) {
			ImGui::SameLine();
			changed = ImGui::Checkbox("M##mute", a_mute) || changed;
			Tooltip("mute: hide this");
		}
		return changed;
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

	void Rule()
	{
		const float gap = ImGui::GetTextLineHeight();
		ImGui::Dummy(ImVec2{ 0.0f, gap });
		ImGui::Separator();
		ImGui::Dummy(ImVec2{ 0.0f, gap });
	}

	void Tooltip(std::string_view a_text)
	{
		if (a_text.empty() || !ImGui::IsItemHovered(ImGuiMCP::ImGuiHoveredFlags_AllowWhenDisabled)) {
			return;
		}
		ImGui::SetTooltip("%.*s", static_cast<int>(a_text.size()), a_text.data());
	}

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
		BadgeStyle StyleOf(FieldType a_type) noexcept
		{
			switch (a_type) {
			case FieldType::kScalar:
				return { "#", true, kValueBlue, "scalar: a number, or @signal of scalar type" };
			case FieldType::kColor:
				return { "c", true, kColourOrange, "colour: r, g, b in 0..1, or one number for all three, or @signal of colour type" };
			case FieldType::kVector:
				return { "v", true, kVectorTeal, "vector: x, y, z (a position, direction or scale), or one number for all three, or @signal of vector type" };
			case FieldType::kReference:
				return { "@", false, kReferenceGreen, "reference: @name of a row of the recipe" };
			case FieldType::kExpression:
				return { "=", false, kCodeYellow, "expression: numbers, [r, g, b], @signals, + - * /, comparisons, and/or/not, if(c, a, b), abs min max clamp saturate floor ceil frac sqrt pow sin cos step smoothstep lerp, time, pi" };
			case FieldType::kCurve:
				return { "x", false, kCurveCyan, "curve: an expression in x (mean is the source's mean), or @curve" };
			case FieldType::kMask:
				return { "m", false, kMaskViolet, "mask: an expression per texel where @source and @mask names are images and @signals are this tick's values" };
			case FieldType::kChannels:
				return { "ch", false, kChannelGrey, "channels: any of r g b a, in any order" };
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

	void Badge(FieldType a_type)
	{
		const auto  style = StyleOf(a_type);
		const float side = ImGui::GetFrameHeight();
		// One square the height of the field, flush against it: filled when a
		// signal can stand in for the value, outlined otherwise.
		BadgeFrame(style.glyph, style.colour, style.takesSignal, side, side);
		Tooltip(style.rule);
		ImGui::SameLine(0.0f, 0.0f);
	}

	std::optional<std::string> ValueField(const std::string& a_key, FieldType a_type, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, float a_scale)
	{
		auto&       state = State();
		const auto  style = StyleOf(a_type);
		const float side = ImGui::GetFrameHeight();
		const bool  reference = a_current.starts_with('@');
		auto        mode = state.comboMode.find(a_key);
		if (mode == state.comboMode.end()) {
			mode = state.comboMode.emplace(a_key, reference).first;
		}
		// The badge: a button when a signal may stand in, inert otherwise.
		ImGui::PushID(a_key.c_str());
		if (style.takesSignal) {
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Button, style.colour);
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonHovered, ImVec4{ style.colour.x * 0.85f, style.colour.y * 0.85f, style.colour.z * 0.85f, 1.0f });
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_ButtonActive, ImVec4{ style.colour.x * 0.7f, style.colour.y * 0.7f, style.colour.z * 0.7f, 1.0f });
			ImGui::PushStyleColor(ImGuiMCP::ImGuiCol_Text, kBadgeFrame);
			if (ImGui::Button(style.glyph, ImVec2{ side, side })) {
				mode->second = !mode->second;
				state.focusField = a_key;
			}
			ImGui::PopStyleColor(4);
			Tooltip(std::string{ style.rule } + (mode->second ? "\nclick: type a value instead" : "\nclick: choose a signal instead"));
		} else {
			BadgeFrame(style.glyph, style.colour, false, side, side);
			Tooltip(style.rule);
		}
		ImGui::SameLine(0.0f, 0.0f);
		ImGui::PopID();

		std::optional<std::string> chosen;
		const bool                 focus = state.focusField == a_key;
		if (focus) {
			state.focusField.clear();
			ImGui::SetKeyboardFocusHere();
		}
		if (mode->second && style.takesSignal) {
			const std::string preview = reference ? a_current : std::string{ "choose a signal" };
			ImGui::SetNextItemWidth(kFillWidth);
			if (ImGui::BeginCombo(Label(a_key + ":combo").c_str(), preview.c_str())) {
				if (a_allowEmpty && ImGui::Selectable("(none)", a_current.empty())) {
					chosen = std::string{};
				}
				for (const auto& name : a_names) {
					const auto text = ReferenceText(name);
					if (ImGui::Selectable(text.c_str(), text == a_current)) {
						chosen = text;
					}
				}
				ImGui::EndCombo();
			}
			return chosen;
		}
		return TextField(a_key + ":text", a_current, kFillWidth, a_scale);
	}

	bool DetailButton(const std::string& a_key)
	{
		const float side = ImGui::GetFrameHeight();
		const bool  clicked = ImGui::Button(("..." + Label(a_key)).c_str(), ImVec2{ side, side });
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

	void ValueSwatch(const std::string& a_key, const Value& a_value)
	{
		if (const auto* c = Get<Vec3>(a_value)) {
			ImGui::ColorButton(Label(a_key).c_str(), ImVec4{ c->x, c->y, c->z, 1.0f }, 0, ImVec2{ 16.0f, 16.0f });
			ImGui::SameLine();
		}
		ImGui::TextUnformatted(ValueText(a_value).c_str());
	}
}
