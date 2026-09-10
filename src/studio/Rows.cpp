#include "studio/Rows.h"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
SignalRow SignalRowOf(const Signal &a_signal, const RowTypes &a_rows,
                      std::size_t a_references) {
  SignalRow row;
  row.name = a_signal.name;
  row.kind = SignalKindOf(a_signal.kind);
  if (const std::optional<ValueType> type =
          a_rows.graph.TypeOf(a_signal.name)) {
    row.type = *type;
  }
  if (const std::optional<std::size_t> index =
          a_rows.graph.Index(a_signal.name)) {
    row.inert = a_rows.graph.Inert(*index);
  }
  row.definition = a_signal.kind;
  if (const ConstantSignal *constant = Get<ConstantSignal>(a_signal.kind)) {
    row.constant = constant->value;
  }
  if (const ExprSignal *expr = Get<ExprSignal>(a_signal.kind)) {
    row.text = expr->text;
  }
  if (const TriggerSignal *trigger = Get<TriggerSignal>(a_signal.kind)) {
    if (const EventOrigin *event = Get<EventOrigin>(trigger->origin)) {
      row.event = event->event;
    } else if (const PluginOrigin *plugin =
                   Get<PluginOrigin>(trigger->origin)) {
      row.event = plugin->id;
    }
  }
  if (a_signal.curve) {
    row.curve = a_signal.curve->text;
  }
  row.references = a_references;
  return row;
}

TextRow CurveRowOf(const Curve &a_curve, std::size_t a_references) {
  return TextRow{a_curve.name, a_curve.text, a_references};
}

TextRow MaskRowOf(const Mask &a_mask, std::size_t a_references) {
  return TextRow{a_mask.name, a_mask.text, a_references};
}

LayerRow LayerRowOf([[maybe_unused]] const Recipe &a_recipe,
                    const Layer &a_layer, [[maybe_unused]] Slot a_slot) {
  LayerRow row;
  row.source = LayerSourceText(a_layer.source);
  row.mask = a_layer.mask ? "@" + a_layer.mask->name : "";
  row.blend = std::string{BlendName(a_layer.blend)};
  row.opacityText = ParamText(a_layer.opacity);
  row.color = a_layer.color ? Vec3ParamText(*a_layer.color) : "";
  row.curve = a_layer.curve ? a_layer.curve->text : "";
  row.channels = a_layer.channels.ToString();
  return row;
}

std::vector<ScalarRow> ScalarRowsOf([[maybe_unused]] const Recipe &a_recipe,
                                    const SurfaceOutput &a_output) {
  std::vector<ScalarRow> rows;
  for (const ScalarField field : ScalarsOf(a_output.slot)) {
    ScalarRow row;
    row.name = std::string{ScalarFieldName(field)};
    if (field == ScalarField::kColor) {
      if (!a_output.scalars.color) {
        continue;
      }
      row.text = Vec3ParamText(*a_output.scalars.color);
    } else {
      const std::optional<Param> *param = ScalarOf(a_output.scalars, field);
      if (!param || !*param) {
        continue;
      }
      row.text = ParamText(**param);
    }
    rows.push_back(std::move(row));
  }
  return rows;
}

OutputRow OutputRowOf(const Recipe &a_recipe, std::size_t a_index) {
  OutputRow row;
  row.index = a_index;
  if (a_index >= a_recipe.outputs.size()) {
    return row;
  }
  const SurfaceOutput *material = Get<SurfaceOutput>(a_recipe.outputs[a_index]);
  if (!material) {
    row.target = Target::kLight;
    return row;
  }
  row.target = TargetOf(material->surface);
  row.surface = material->surface;
  row.slot = material->slot;
  row.replace = material->replace;
  row.scalars = ScalarRowsOf(a_recipe, *material);
  for (const Layer &layer : material->stack) {
    row.layers.push_back(LayerRowOf(a_recipe, layer, material->slot));
  }
  return row;
}
}
