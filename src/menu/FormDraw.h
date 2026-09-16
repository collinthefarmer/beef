#pragma once

#include "menu/Frame.h"
#include "studio/Fields.h"
#include "studio/Forms.h"
#include "studio/Navigation.h"
#include "studio/Rows.h"
#include "studio/SelectorEdit.h"
#include "studio/Snapshot.h"
#include "studio/Widgets.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace BetterEnchantmentEffects::Studio {
struct Inspector;
}

namespace BetterEnchantmentEffects::Menu {
[[nodiscard]] std::optional<std::string>
FieldInput(const Studio::FormField &a_field, float a_scale,
           const Studio::Names &a_names,
           const Studio::Width &a_width = Studio::Width::Fill());
void PostField(const Studio::FormField &a_field, const std::string &a_text,
               const Frame &a_frame);
void DrawRowField(const char *a_key, const Studio::FormField &a_field,
                  const Frame &a_frame);

[[nodiscard]] std::optional<std::size_t>
DrawFieldTable(const char *a_id, std::span<const Studio::FormField> a_fields,
               const Frame &a_frame);
[[nodiscard]] std::optional<std::size_t>
DrawForm(const char *a_id, std::span<const Studio::FormField> a_form,
         const Frame &a_frame, std::size_t a_columns = 1);
void DrawFormWithSignals(const char *a_id,
                         std::span<const Studio::FormField> a_form,
                         const Frame &a_frame, std::size_t a_columns = 1);
void DrawInspectorFields(const Studio::Inspector &a_inspector,
                         const Frame &a_frame);
void NavigateFromInspector(
    const Frame &a_frame, Studio::InspectorSubject a_subject,
    std::optional<PropertyLocation> a_property = std::nullopt);

void FirePopup(const Studio::SignalRow &a_signal, const Frame &a_frame);

void DrawSelector(const Studio::SelectorView &a_selector, std::size_t a_output,
                  bool a_light, const Frame &a_frame);
}
