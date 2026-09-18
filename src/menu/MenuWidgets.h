#pragma once

#include "Core.h"
#include "recipe/Recipe.h"
#include "recipe/Visit.h"
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
#include <variant>

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

struct RuleResult {
  Studio::RuleClick click;
  bool open = true;
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
[[nodiscard]] float WidestOf(std::span<const std::string> a_names);
[[nodiscard]] float ButtonWidth(std::string_view a_text);
[[nodiscard]] float TextWidth(std::string_view a_text);
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

[[nodiscard]] std::optional<std::string>
ChoiceCombo(const char *a_key, const std::string &a_current,
            std::span<const std::string> a_names, const WidgetSize &a_size);
[[nodiscard]] std::optional<std::string>
ReferenceCombo(const char *a_key, const Studio::FormField &a_field,
               const WidgetSize &a_size);

using SearchPick = std::variant<std::size_t, std::string>;
struct SearchComboSpec {
  const char *id = nullptr;
  const char *preview = nullptr;
  const char *hint = nullptr;
  Studio::Width width = Studio::Width::Fill();
  std::string_view customVerb = {};
  std::string_view customTip = {};
  std::string_view emptyHint = {};
};
[[nodiscard]] std::optional<SearchPick>
SearchCombo(const SearchComboSpec &a_spec,
            std::span<const std::string> a_labels,
            std::optional<std::size_t> a_selected = {});

struct CandidateRowsSpec {
  std::string_view filter;
  const Studio::GameObjectCatalog *catalog = nullptr;
  std::string_view current = {};
  std::string_view prefix = {};
  std::size_t cap = 0;
};
[[nodiscard]] const Studio::GameObjectCandidate *
DrawCandidateRows(const CandidateRowsSpec &a_spec, int &a_id,
                  std::size_t &a_shown);

void Badge(Studio::FieldKind a_kind);
[[nodiscard]] const char *BlendGlyph(Blend a_blend);
[[nodiscard]] std::optional<Blend> BlendBadge(Blend a_current, Slot a_slot);
[[nodiscard]] std::optional<std::string>
ValueWidget(const char *a_key, const Studio::FormField &a_field, float a_scale,
            const TextCheck &a_check = {},
            const Studio::Width &a_width = Studio::Width::Fill());
[[nodiscard]] bool DetailButton();

[[nodiscard]] std::string ValueText(const Value &a_value);
void ValueSwatch(const Value &a_value);
[[nodiscard]] std::string SourceValueText(const SourceKind &a_kind);
[[nodiscard]] std::string ResourceValueText(const Studio::RecipeRow &a_recipe,
                                            const ResourceRef &a_ref);
void DimFitted(std::string_view a_text);

struct ResourceCells {
  std::string_view name;
  std::string_view type;
  std::optional<Value> value;
};
void ResourceTable(const char *a_id, std::span<const ResourceCells> a_rows);

[[nodiscard]] std::optional<float> Split(const char *a_id, float a_ratio,
                                         const std::function<void()> &a_left,
                                         const std::function<void()> &a_right);

void Rule();
[[nodiscard]] RuleResult Rule(const Studio::RuleSpec &a_spec,
                              float a_trailingWidth = 0.0f,
                              const std::function<void()> &a_trailing = {},
                              const std::function<void()> &a_leading = {});
[[nodiscard]] RuleFilter RuleWithFilter(const Studio::RuleSpec &a_spec,
                                        const FilterSpec &a_filter);

bool Toggle(const char *a_label, bool &a_value, std::string_view a_tooltip);
void DetailModal(const char *a_title, const std::function<void()> &a_body);
void ConfirmModal(const char *a_title, const std::string &a_message,
                  const char *a_affirm,
                  const std::function<void()> &a_onAffirm);
void RightAligned(float a_width, const std::function<void()> &a_draw);
void Disabled(bool a_disabled, const std::function<void()> &a_draw);
void HeldLabel(const char *a_text);

[[nodiscard]] std::string RecipeLabel(const Studio::RecipeRow &a_recipe);

[[nodiscard]] bool RemoveButton(std::size_t a_references);
[[nodiscard]] bool CloseButton();
bool SquareToggle(const char *a_label, bool &a_value,
                  std::string_view a_tooltip);
bool SoloButton(bool &a_solo);
bool SoloButton(bool &a_solo, std::string_view a_tooltip);
bool MuteButton(bool &a_mute);
bool PeekButton(bool &a_peek);
[[nodiscard]] bool DragHandle(const char *a_type, std::size_t a_index,
                              const char *a_noun);
[[nodiscard]] std::optional<Studio::RowMove> DropTarget(const char *a_type,
                                                        std::size_t a_index);

void Problem(std::string_view a_text);
void Warn(std::string_view a_text);
void Ok(std::string_view a_text);
void Dim(std::string_view a_text);
void ProblemBadge(const char *a_label);
void WarnBadge(const char *a_label);
void DimBadge(const char *a_label);
void Banner(std::string_view a_label, float a_trailingWidth,
            const std::function<void()> &a_trailing);
void PlaceholderText(std::string_view a_text);
void DrawDiagnostics(std::span<const Diagnostic> a_problems, bool a_heldBack);
void HelpMarker(const char *a_text);
void LabelWithHelp(std::string_view a_label, std::string_view a_help);
void Tooltip(std::string_view a_text);
}
