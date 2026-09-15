#pragma once

#include "Core.h"
#include "recipe/Recipe.h"
#include "studio/Forms.h"
#include "studio/Snapshot.h"
#include "studio/View.h"
#include "studio/Widgets.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace BetterEnchantmentEffects::Menu {
using TextCheck =
    std::function<std::optional<std::string>(const std::string &)>;

inline constexpr std::string_view kNewInputChoice = "\x01new-input";

class FieldScope {
public:
  explicit FieldScope(std::string_view a_part);
  ~FieldScope();
  FieldScope(const FieldScope &) = delete;
  FieldScope &operator=(const FieldScope &) = delete;

private:
  std::string previous;
};

enum class ChooserPick {
  kNone,
  kChosen,
  kAction,
};

enum class SoloMuteChange {
  kNone,
  kSolo,
  kMute,
};

struct RuleFilter {
  Studio::RuleClick click;
  std::string_view filter;
};

struct WidgetSize {
  Studio::Width width = Studio::Width::Fill();
  float scale = 1.0f;
};

struct FilterSpec {
  const char *key;
  const char *hint;
  float width;
  float scale = 1.0f;
};

struct ChooserRowSpec {
  std::span<const std::string_view> leading;
  std::string_view name;
  std::string_view detail;
  std::optional<float> share;
  std::optional<std::string> unavailable;
  const char *action = nullptr;
};

class Table {
public:
  [[nodiscard]] static Table Begin(const char *a_id,
                                   std::span<const Studio::Column> a_columns,
                                   const Studio::TableStyle &a_style = {});
  [[nodiscard]] static Table
  Begin(const char *a_id, std::initializer_list<Studio::Column> a_columns,
        const Studio::TableStyle &a_style = {});
  [[nodiscard]] bool Open() const noexcept { return open_; }
  void Cell();
  void End();

private:
  bool open_ = false;
  std::size_t columns_ = 0;
  std::size_t cells_ = 0;
};

[[nodiscard]] float ResolveWidth(const Studio::Width &a_width,
                                 float a_scale) noexcept;
void NextItemWidth(const Studio::Width &a_width, float a_scale = 1.0f);
[[nodiscard]] float FitWidth(std::string_view a_text);
[[nodiscard]] float BlendWidth(std::span<const Blend> a_allowed);
[[nodiscard]] float WidestOf(std::span<const std::string> a_names);
[[nodiscard]] float ButtonWidth(std::string_view a_text);
[[nodiscard]] float TextWidth(std::string_view a_text);
[[nodiscard]] float CheckboxWidth(std::string_view a_text);
[[nodiscard]] float ItemSpacingX();
[[nodiscard]] float RowButtonWidth();
[[nodiscard]] float RuleHeight();

[[nodiscard]] std::optional<std::string>
TextField(const char *a_key, const std::string &a_model,
          const WidgetSize &a_size, const TextCheck &a_check = {});
[[nodiscard]] std::string_view LiveTextField(const char *a_key,
                                             const char *a_hint,
                                             const Studio::Width &a_width,
                                             float a_scale);

void Thumbnail(const Studio::ThumbnailSpec &a_spec);
[[nodiscard]] bool ThumbnailButton(const char *a_key,
                                   const Studio::ThumbnailSpec &a_spec);

[[nodiscard]] std::optional<Blend> BlendCombo(const char *a_key,
                                              std::string_view a_current,
                                              std::span<const Blend> a_allowed,
                                              const WidgetSize &a_size);
[[nodiscard]] std::optional<std::string>
ChoiceCombo(const char *a_key, const std::string &a_current,
            std::span<const std::string> a_names, const WidgetSize &a_size);
[[nodiscard]] std::optional<std::string>
ReferenceCombo(const char *a_key, const Studio::FormField &a_field,
               const WidgetSize &a_size);

void Badge(Studio::FieldKind a_kind);
[[nodiscard]] const char *BlendGlyph(Blend a_blend);
[[nodiscard]] std::optional<Blend> BlendBadge(Blend a_current, Slot a_slot);
[[nodiscard]] std::optional<std::string>
ValueWidget(const char *a_key, const Studio::FormField &a_field, float a_scale,
            const TextCheck &a_check = {});
[[nodiscard]] bool DetailButton();

[[nodiscard]] std::string ValueText(const Value &a_value);
void ValueSwatch(const Value &a_value);

bool ModeBar(Studio::Mode &a_mode, Studio::Mode &a_drawn);
[[nodiscard]] bool Section(const char *a_title, bool a_openByDefault);
[[nodiscard]] std::optional<float> Split(const char *a_id, float a_ratio,
                                         const std::function<void()> &a_left,
                                         const std::function<void()> &a_right);

void Rule();
[[nodiscard]] Studio::RuleClick Rule(const Studio::RuleSpec &a_spec);
[[nodiscard]] RuleFilter RuleWithFilter(const Studio::RuleSpec &a_spec,
                                        const FilterSpec &a_filter);

[[nodiscard]] ChooserPick ChooserRow(Table &a_table,
                                     const ChooserRowSpec &a_row);
bool Toggle(const char *a_label, bool &a_value, std::string_view a_tooltip);
void DetailModal(const char *a_title, const std::function<void()> &a_body);
void RightAligned(float a_width, const std::function<void()> &a_draw);
void Disabled(bool a_disabled, const std::function<void()> &a_draw);
void HeldLabel(const char *a_text);
[[nodiscard]] bool LitButton(const char *a_label, bool a_lit);

[[nodiscard]] bool RemoveButton(std::size_t a_references);
bool SoloButton(bool &a_solo);
bool MuteButton(bool &a_mute);
SoloMuteChange SoloMute(bool &a_solo, bool &a_mute);
[[nodiscard]] bool DragHandle(const char *a_type, std::size_t a_index,
                              const char *a_noun);
[[nodiscard]] std::optional<Studio::RowMove> DropTarget(const char *a_type,
                                                        std::size_t a_index);

void Problem(std::string_view a_text);
void Warn(std::string_view a_text);
void Ok(std::string_view a_text);
void Dim(std::string_view a_text);
void HelpMarker(const char *a_text);
void Tooltip(std::string_view a_text);
}
