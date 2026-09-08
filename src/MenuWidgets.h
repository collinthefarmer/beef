#pragma once

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
		std::string text;

		[[nodiscard]] static Width Fill() noexcept;
		[[nodiscard]] static Width Fit();
		[[nodiscard]] static Width Fit(std::string_view a_text);
		[[nodiscard]] static Width Px(float a_pixels) noexcept;
	};

	struct Column
	{
		const char* label = "";
		Width       width = Width::Fill();
	};

	struct TableStyle
	{
		enum class Borders
		{
			kAll,
			kInnerHorizontal,
			kNone,
		};
		Borders borders = Borders::kAll;
		bool    stretch = false;
		bool    headers = true;
		bool    rowBackground = false;
	};

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

	struct RowMove
	{
		std::size_t from = 0;
		std::size_t to = 0;
	};

	void NextItemWidth(const Width& a_width, float a_scale = 1.0f);
	[[nodiscard]] float FitWidth(std::string_view a_text);
	[[nodiscard]] float BlendWidth(std::span<const Blend> a_allowed);
	[[nodiscard]] float WidestOf(std::span<const std::string> a_names);
	[[nodiscard]] float ButtonWidth(std::string_view a_text);
	[[nodiscard]] float TextWidth(std::string_view a_text);
	[[nodiscard]] float CheckboxWidth(std::string_view a_text);
	[[nodiscard]] float ItemSpacingX();

	using TextCheck = std::function<std::optional<std::string>(const std::string&)>;

	[[nodiscard]] std::optional<std::string> TextField(const char* a_key, const std::string& a_model, const Width& a_width, float a_scale, const TextCheck& a_check = {});
	[[nodiscard]] std::string_view LiveTextField(const char* a_key, const char* a_hint, const Width& a_width, float a_scale);

	void               Thumbnail(TextureHandle a_texture, ShaderChannel a_channel, bool a_dynamic, float a_size);
	[[nodiscard]] bool ThumbnailButton(const char* a_key, TextureHandle a_texture, ShaderChannel a_channel, bool a_dynamic, float a_size);

	[[nodiscard]] std::optional<Blend> BlendCombo(const char* a_key, std::string_view a_current, std::span<const Blend> a_allowed, const Width& a_width, float a_scale);
	[[nodiscard]] std::optional<std::string> ReferenceCombo(const char* a_key, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, const Width& a_width, float a_scale, std::span<const std::string> a_creators = {});

	void Badge(FieldKind a_kind);
	[[nodiscard]] std::optional<std::string> ValueField(const char* a_key, FieldKind a_kind, const std::string& a_current, std::span<const std::string> a_names, bool a_allowEmpty, float a_scale, const TextCheck& a_check = {}, std::span<const std::string> a_creators = {});
	[[nodiscard]] bool DetailButton();

	[[nodiscard]] std::string ValueText(const Value& a_value);
	void ValueSwatch(const Value& a_value);

	bool ModeBar(Mode& a_mode, Mode& a_drawn);
	[[nodiscard]] bool Section(const char* a_title, bool a_openByDefault);
	[[nodiscard]] std::optional<float> Split(const char* a_id, float a_ratio, const std::function<void()>& a_left, const std::function<void()>& a_right);
	struct RuleLine
	{
		std::string_view      text;
		float                 rightWidth = 0.0f;
		std::function<void()> right;

		[[nodiscard]] static RuleLine Text(std::string_view a_text) { return RuleLine{ a_text, 0.0f, {} }; }
	};

	void                Rule(const RuleLine& a_above = {}, const RuleLine& a_below = {});
	[[nodiscard]] float RuleHeight();

	enum class ChooserPick
	{
		kNone,
		kChosen,
		kAction,
	};
	[[nodiscard]] ChooserPick ChooserRow(Table& a_table, std::span<const std::string_view> a_leading, std::string_view a_name, std::string_view a_detail, std::optional<float> a_share, const std::optional<std::string>& a_unavailable, const char* a_action = nullptr);
	bool Toggle(const char* a_label, bool& a_value, std::string_view a_tooltip);
	[[nodiscard]] std::optional<std::string> ChoiceCombo(const char* a_key, const std::string& a_current, std::span<const std::string> a_names, const Width& a_width, float a_scale);
	void DetailModal(const char* a_title, const std::function<void()>& a_body);
	void RightAligned(float a_width, const std::function<void()>& a_draw);
	void Disabled(bool a_disabled, const std::function<void()>& a_draw);
	void HeldLabel(const char* a_text);
	[[nodiscard]] bool LitButton(const char* a_label, bool a_lit);

	[[nodiscard]] float RowButtonWidth();
	[[nodiscard]] bool RemoveButton(std::size_t a_references);
	bool SoloButton(bool& a_solo);
	bool MuteButton(bool& a_mute);
	enum class SoloMuteChange
	{
		kNone,
		kSolo,
		kMute,
	};
	SoloMuteChange SoloMute(bool& a_solo, bool& a_mute);

	[[nodiscard]] bool                   DragHandle(const char* a_type, std::size_t a_index, const char* a_noun);
	[[nodiscard]] std::optional<RowMove> DropTarget(const char* a_type, std::size_t a_index);

	void Problem(std::string_view a_text);
	void Warn(std::string_view a_text);
	void Ok(std::string_view a_text);
	void Dim(std::string_view a_text);
	void HelpMarker(const char* a_text);
	void Tooltip(std::string_view a_text);
}
