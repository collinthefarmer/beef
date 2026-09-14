#include "studio/Edits.h"
#include "studio/Relationships.h"

#include "Core.h"
#include "recipe/Expression.h"
#include "recipe/Recipe.h"
#include "recipe/Signals.h"
#include "recipe/Words.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
namespace {
using Refusal = std::optional<Diagnostic>;

Diagnostic Refuse(std::string a_where, std::string a_message) {
  return MakeDiagnostic(Severity::kError, std::move(a_where),
                        std::move(a_message));
}

std::string SelectorText(const Selector &a_selector) {
  std::string text;
  for (const auto &clause : a_selector.anyOf) {
    const std::string value = Match(
        clause.operand, [](const FormRef &a_form) { return a_form.text; },
        [](const std::string &a_glob) { return a_glob; });
    text += (text.empty() ? "" : ", ") +
            std::format("{} {}", NameOf(kSelectorKinds, clause.kind), value);
  }
  return text;
}

std::string ValueText(const Value &a_value) {
  return Match(
      a_value, [](float a_f) { return ParamText(Param{a_f}); },
      [](const Vec2 &a_v) {
        return ParamText(Param{a_v.x}) + ", " + ParamText(Param{a_v.y});
      },
      [](const Vec3 &a_v) {
        return Vec3ParamText(
            Vec3Param{std::array<Param, 3>{a_v.x, a_v.y, a_v.z}});
      });
}

std::string CurveText(const std::optional<CurveRef> &a_curve) {
  return a_curve ? a_curve->text : "none";
}

Refusal FirstError(const std::vector<Diagnostic> &a_diagnostics) {
  for (const auto &d : a_diagnostics) {
    if (d.severity == Severity::kError) {
      return d;
    }
  }
  return std::nullopt;
}

bool Reported(const std::vector<Diagnostic> &a_before, const Diagnostic &a_d) {
  return std::ranges::any_of(a_before, [&](const Diagnostic &a_b) {
    return a_b.severity == a_d.severity && a_b.where == a_d.where &&
           a_b.message == a_d.message;
  });
}

Refusal NewError(const std::vector<Diagnostic> &a_before,
                 const std::vector<Diagnostic> &a_after) {
  for (const auto &d : a_after) {
    if (d.severity == Severity::kError && !Reported(a_before, d)) {
      return d;
    }
  }
  return std::nullopt;
}

Refusal CheckText(const std::string &a_where, const std::string &a_text) {
  if (a_text.empty()) {
    return Refuse(a_where, "the expression is empty");
  }
  if (a_text.size() > kMaxExpressionLength) {
    return Refuse(a_where, std::format("longer than {} characters",
                                       kMaxExpressionLength));
  }
  if (const auto program = Program::Parse(a_text); !program) {
    return Refuse(a_where, program.error());
  }
  return std::nullopt;
}

Refusal CheckCurveText(const std::string &a_where, const CurveRef &a_curve) {
  if (a_curve.Named()) {
    return std::nullopt;
  }
  if (const auto program = ParseCurve(a_curve.text); !program) {
    return Refuse(a_where, program.error());
  }
  return std::nullopt;
}

struct CheckCtx {
  const RowTypes &rows;
  std::string_view where;
};

Refusal CheckScalarRef(const CheckCtx &a_ctx, std::string_view a_field,
                       const Param &a_param) {
  const auto *ref = Get<Ref>(a_param);
  if (!ref) {
    return std::nullopt;
  }
  const auto type = SignalTypeOf(a_ctx.rows, ref->name);
  if (!type) {
    return Refuse(
        std::string{a_ctx.where},
        std::format("'{}' reads unknown signal '@{}'", a_field, ref->name));
  }
  if (*type != ValueType::kScalar) {
    return Refuse(std::string{a_ctx.where},
                  std::format("'{}' must be a scalar; '@{}' is a {}", a_field,
                              ref->name, Name(*type)));
  }
  return std::nullopt;
}

Refusal CheckVectorRefSignal(const CheckCtx &a_ctx, std::string_view a_field,
                             const Ref &a_ref) {
  const auto type = SignalTypeOf(a_ctx.rows, a_ref.name);
  if (!type) {
    return Refuse(
        std::string{a_ctx.where},
        std::format("'{}' reads unknown signal '@{}'", a_field, a_ref.name));
  }
  if (*type != ValueType::kVec3) {
    return Refuse(std::string{a_ctx.where},
                  std::format("'{}' must be a vec3; '@{}' is a {}", a_field,
                              a_ref.name, Name(*type)));
  }
  return std::nullopt;
}

Refusal CheckVectorRefParts(const CheckCtx &a_ctx, std::string_view a_field,
                            const std::array<Param, 3> &a_parts, bool a_color) {
  for (const auto &part : a_parts) {
    if (auto problem = CheckScalarRef(a_ctx, a_field, part)) {
      return problem;
    }
    const auto *number = Get<float>(part);
    if (a_color && number && (*number < 0.0f || *number > 1.0f)) {
      return Refuse(std::string{a_ctx.where},
                    std::format("'{}' components are 0..1", a_field));
    }
  }
  return std::nullopt;
}

Refusal CheckVectorRef(const CheckCtx &a_ctx, std::string_view a_field,
                       const Vec3Param &a_param, bool a_color) {
  return Match(
      a_param,
      [&](const Ref &a_ref) -> Refusal {
        return CheckVectorRefSignal(a_ctx, a_field, a_ref);
      },
      [&](const std::array<Param, 3> &a_parts) -> Refusal {
        return CheckVectorRefParts(a_ctx, a_field, a_parts, a_color);
      });
}

struct FoundOutput {
  SurfaceOutput *output = nullptr;
  Refusal problem;
};
struct FoundLayer {
  SurfaceOutput *output = nullptr;
  Layer *layer = nullptr;
  Refusal problem;
};
struct FoundLight {
  LightOutput *light = nullptr;
  Refusal problem;
};

FoundOutput FindSurfaceOutput(Recipe &a_recipe, std::size_t a_index) {
  if (a_index >= a_recipe.outputs.size()) {
    return {nullptr,
            Refuse(OutputWhere(a_index), std::format("there are {} outputs",
                                                     a_recipe.outputs.size()))};
  }
  auto *material = Get<SurfaceOutput>(a_recipe.outputs[a_index]);
  if (!material) {
    return {nullptr, Refuse(OutputWhere(a_index),
                            std::format("output {} is a light", a_index))};
  }
  return {material, std::nullopt};
}

FoundLayer FindLayer(Recipe &a_recipe, std::size_t a_output,
                     std::size_t a_layer) {
  auto found = FindSurfaceOutput(a_recipe, a_output);
  if (found.problem) {
    return {nullptr, nullptr, found.problem};
  }
  if (a_layer >= found.output->stack.size()) {
    return {nullptr, nullptr,
            Refuse(LayerWhere(a_output, a_layer),
                   std::format("the stack has {} layers",
                               found.output->stack.size()))};
  }
  return {found.output, &found.output->stack[a_layer], std::nullopt};
}

FoundLight FindLight(Recipe &a_recipe, std::size_t a_index) {
  if (a_index >= a_recipe.outputs.size()) {
    return {nullptr,
            Refuse(OutputWhere(a_index), std::format("there are {} outputs",
                                                     a_recipe.outputs.size()))};
  }
  auto *light = Get<LightOutput>(a_recipe.outputs[a_index]);
  if (!light) {
    return {nullptr, Refuse(OutputWhere(a_index), "not a light output")};
  }
  return {light, std::nullopt};
}

Signal *FindSignalRow(Recipe &a_recipe, std::string_view a_name) {
  const auto it = std::ranges::find(a_recipe.signals, a_name, &Signal::name);
  return it == a_recipe.signals.end() ? nullptr : &*it;
}
Curve *FindCurveRow(Recipe &a_recipe, std::string_view a_name) {
  const auto it = std::ranges::find(a_recipe.curves, a_name, &Curve::name);
  return it == a_recipe.curves.end() ? nullptr : &*it;
}
Mask *FindMaskRow(Recipe &a_recipe, std::string_view a_name) {
  const auto it = std::ranges::find(a_recipe.masks, a_name, &Mask::name);
  return it == a_recipe.masks.end() ? nullptr : &*it;
}
Source *FindSourceRow(Recipe &a_recipe, std::string_view a_name) {
  const auto it = std::ranges::find(a_recipe.sources, a_name, &Source::name);
  return it == a_recipe.sources.end() ? nullptr : &*it;
}

SignalGraph GraphOf(const Recipe &a_recipe) {
  return SignalGraph::Compile(a_recipe.signals, a_recipe.curves);
}

Refusal Edit(Recipe &a_recipe, const SetLayerSource &a_edit) {
  auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
  if (found.problem)
    return found.problem;
  const auto where = LayerWhere(a_edit.output, a_edit.layer);
  const auto slot = found.output->slot;
  Layer candidate = *found.layer;
  candidate.source = a_edit.source;
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem = NewError(CheckLayer(rows, *found.layer, slot, where),
                              CheckLayer(rows, candidate, slot, where)))
    return problem;
  found.layer->source = a_edit.source;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLayerCurve &a_edit) {
  auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
  if (found.problem)
    return found.problem;
  const auto where = LayerWhere(a_edit.output, a_edit.layer);
  const auto slot = found.output->slot;
  Layer candidate = *found.layer;
  candidate.curve = a_edit.curve;
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem = NewError(CheckLayer(rows, *found.layer, slot, where),
                              CheckLayer(rows, candidate, slot, where)))
    return problem;
  found.layer->curve = a_edit.curve;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLayerBlend &a_edit) {
  auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
  if (found.problem)
    return found.problem;
  const auto where = LayerWhere(a_edit.output, a_edit.layer);
  const auto slot = found.output->slot;
  Layer candidate = *found.layer;
  candidate.blend = a_edit.blend;
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem = NewError(CheckLayer(rows, *found.layer, slot, where),
                              CheckLayer(rows, candidate, slot, where)))
    return problem;
  found.layer->blend = a_edit.blend;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLayerOpacity &a_edit) {
  auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
  if (found.problem)
    return found.problem;
  const auto where = LayerWhere(a_edit.output, a_edit.layer);
  const auto slot = found.output->slot;
  Layer candidate = *found.layer;
  candidate.opacity = a_edit.opacity;
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem = NewError(CheckLayer(rows, *found.layer, slot, where),
                              CheckLayer(rows, candidate, slot, where)))
    return problem;
  found.layer->opacity = a_edit.opacity;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLayerColor &a_edit) {
  auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
  if (found.problem)
    return found.problem;
  const auto where = LayerWhere(a_edit.output, a_edit.layer);
  const auto slot = found.output->slot;
  Layer candidate = *found.layer;
  candidate.color = a_edit.color;
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem = NewError(CheckLayer(rows, *found.layer, slot, where),
                              CheckLayer(rows, candidate, slot, where)))
    return problem;
  found.layer->color = a_edit.color;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLayerMask &a_edit) {
  auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
  if (found.problem)
    return found.problem;
  const auto where = LayerWhere(a_edit.output, a_edit.layer);
  const auto slot = found.output->slot;
  Layer candidate = *found.layer;
  candidate.mask = a_edit.mask;
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem = NewError(CheckLayer(rows, *found.layer, slot, where),
                              CheckLayer(rows, candidate, slot, where)))
    return problem;
  found.layer->mask = a_edit.mask;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLayerChannels &a_edit) {
  auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
  if (found.problem)
    return found.problem;
  found.layer->channels = a_edit.channels;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const AddLayer &a_edit) {
  auto found = FindSurfaceOutput(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  auto &stack = found.output->stack;
  const std::size_t at = a_edit.at.value_or(stack.size());
  if (at > stack.size()) {
    return Refuse(LayerWhere(a_edit.output, at),
                  std::format("the stack has {} layers", stack.size()));
  }
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          FirstError(CheckLayer(rows, a_edit.layer, found.output->slot,
                                LayerWhere(a_edit.output, at))))
    return problem;
  stack.insert(stack.begin() + static_cast<std::ptrdiff_t>(at), a_edit.layer);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const RemoveLayer &a_edit) {
  auto found = FindLayer(a_recipe, a_edit.output, a_edit.layer);
  if (found.problem)
    return found.problem;
  auto &stack = found.output->stack;
  stack.erase(stack.begin() + static_cast<std::ptrdiff_t>(a_edit.layer));
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const MoveLayer &a_edit) {
  auto found = FindLayer(a_recipe, a_edit.output, a_edit.from);
  if (found.problem)
    return found.problem;
  auto &stack = found.output->stack;
  if (a_edit.to >= stack.size()) {
    return Refuse(LayerWhere(a_edit.output, a_edit.from),
                  std::format("cannot move to {}: the stack has {} layers",
                              a_edit.to, stack.size()));
  }
  const auto from = stack.begin() + static_cast<std::ptrdiff_t>(a_edit.from);
  const auto to = stack.begin() + static_cast<std::ptrdiff_t>(a_edit.to);
  if (a_edit.from < a_edit.to) {
    std::rotate(from, from + 1, to + 1);
  } else if (a_edit.to < a_edit.from) {
    std::rotate(to, from, from + 1);
  }
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const ClearLayers &a_edit) {
  auto found = FindSurfaceOutput(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  found.output->stack.clear();
  return std::nullopt;
}

std::optional<std::size_t> ExcludingOutput(const Recipe &a_recipe,
                                           Surface a_surface, Slot a_slot) {
  for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
    const auto *other = Get<SurfaceOutput>(a_recipe.outputs[i]);
    if (other && other->surface == a_surface &&
        SlotsExclude(other->slot, a_slot)) {
      return i;
    }
  }
  return std::nullopt;
}

Selector SharedSelector(const Recipe &a_recipe) {
  std::optional<Selector> shared;
  for (const auto &output : a_recipe.outputs) {
    const auto *surface = Get<SurfaceOutput>(output);
    if (!surface) {
      continue;
    }
    if (shared && !(*shared == surface->selector)) {
      return Selector{};
    }
    shared = surface->selector;
  }
  return shared.value_or(Selector{});
}

Refusal Edit(Recipe &a_recipe, const AddOutput &a_edit) {
  const auto where = OutputWhere(a_recipe.outputs.size());
  if (!SurfaceHasSlot(a_edit.surface, a_recipe.shell.material, a_edit.slot)) {
    return Refuse(where,
                  std::format("a {} shell offers no '{}' slot",
                              a_recipe.shell.material == ShellMaterial::kVanilla
                                  ? "vanilla"
                                  : "PBR-copy",
                              SlotName(a_edit.slot)));
  }
  if (const auto other =
          ExcludingOutput(a_recipe, a_edit.surface, a_edit.slot)) {
    const auto *row = Get<SurfaceOutput>(a_recipe.outputs[*other]);
    return Refuse(
        where, std::format("output {} on '{}' excludes '{}' on the same {}",
                           *other, row ? SlotName(row->slot) : "?",
                           SlotName(a_edit.slot), SurfaceName(a_edit.surface)));
  }
  SurfaceOutput output = DefaultOutput(a_edit.surface, a_edit.slot);
  output.selector =
      a_edit.selector.All() ? SharedSelector(a_recipe) : a_edit.selector;
  a_recipe.outputs.emplace_back(std::move(output));
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const RemoveOutput &a_edit) {
  if (a_edit.output >= a_recipe.outputs.size()) {
    return Refuse(OutputWhere(a_edit.output),
                  std::format("there are {} outputs", a_recipe.outputs.size()));
  }
  a_recipe.outputs.erase(a_recipe.outputs.begin() +
                         static_cast<std::ptrdiff_t>(a_edit.output));
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetScalar &a_edit) {
  auto found = FindSurfaceOutput(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  const auto where = OutputWhere(a_edit.output);
  const auto field = ScalarFieldName(a_edit.field);
  if (!std::ranges::contains(ScalarsOf(found.output->slot), a_edit.field)) {
    return Refuse(where, std::format("slot '{}' has no '{}'",
                                     SlotName(found.output->slot), field));
  }
  auto *scalar = ScalarOf(found.output->scalars, a_edit.field);
  if (!scalar) {
    return Refuse(where, std::format("'{}' is a colour", field));
  }
  SurfaceOutput candidate = *found.output;
  *ScalarOf(candidate.scalars, a_edit.field) = a_edit.value;
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          NewError(CheckOutput(rows, a_recipe.outputs[a_edit.output], where),
                   CheckOutput(rows, Output{candidate}, where)))
    return problem;
  *scalar = a_edit.value;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetColorScalar &a_edit) {
  auto found = FindSurfaceOutput(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  const auto where = OutputWhere(a_edit.output);
  if (!std::ranges::contains(ScalarsOf(found.output->slot),
                             ScalarField::kColor)) {
    return Refuse(where, std::format("slot '{}' has no 'color'",
                                     SlotName(found.output->slot)));
  }
  SurfaceOutput candidate = *found.output;
  candidate.scalars.color = a_edit.color;
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          NewError(CheckOutput(rows, a_recipe.outputs[a_edit.output], where),
                   CheckOutput(rows, Output{candidate}, where)))
    return problem;
  found.output->scalars.color = a_edit.color;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetOutputReplace &a_edit) {
  auto found = FindSurfaceOutput(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  found.output->replace = a_edit.replace;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetOutputSelector &a_edit) {
  auto found = FindSurfaceOutput(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  found.output->selector = a_edit.selector;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetPriority &a_edit) {
  a_recipe.priority = a_edit.priority;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetClockSpeed &a_edit) {
  a_recipe.clock.speed = a_edit.speed;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const AddKey &a_edit) {
  const auto where = KeyWhere(a_edit.key);
  if (std::ranges::find(a_recipe.keys, a_edit.key) != a_recipe.keys.end()) {
    return Refuse(where, "the recipe has that key");
  }
  switch (KeyOperandOf(a_edit.key.kind)) {
  case KeyOperand::kForm:
    if (!a_edit.key.Form() || a_edit.key.Form()->text.empty()) {
      return Refuse(where, "a form key names a form");
    }
    break;
  case KeyOperand::kGlob:
    if (a_edit.key.Glob().empty()) {
      return Refuse(where, "a material key needs a glob");
    }
    break;
  case KeyOperand::kNone:
    break;
  }
  a_recipe.keys.push_back(a_edit.key);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const RemoveKey &a_edit) {
  const auto where = KeyWhere(a_edit.key);
  const auto it = std::ranges::find(a_recipe.keys, a_edit.key);
  if (it == a_recipe.keys.end()) {
    return Refuse(where, "no such key");
  }
  if (a_recipe.keys.size() == 1) {
    return Refuse(where, "a recipe keeps at least one key");
  }
  a_recipe.keys.erase(it);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetConstant &a_edit) {
  auto *signal = FindSignalRow(a_recipe, a_edit.signal);
  if (!signal) {
    return Refuse(SignalWhere(a_edit.signal), "no such signal");
  }
  signal->kind = ConstantSignal{a_edit.value};
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetExpression &a_edit) {
  auto *signal = FindSignalRow(a_recipe, a_edit.signal);
  if (!signal) {
    return Refuse(SignalWhere(a_edit.signal), "no such signal");
  }
  if (auto problem = CheckText(SignalWhere(a_edit.signal), a_edit.text))
    return problem;
  signal->kind = ExprSignal{a_edit.text};
  return std::nullopt;
}

Refusal CheckSignalKind(const Recipe &a_recipe, const std::string &a_where,
                        const SignalKind &a_kind);

Refusal Edit(Recipe &a_recipe, const SetSignal &a_edit) {
  auto *signal = FindSignalRow(a_recipe, a_edit.signal);
  if (!signal) {
    return Refuse(SignalWhere(a_edit.signal), "no such signal");
  }
  if (auto problem =
          CheckSignalKind(a_recipe, SignalWhere(a_edit.signal), a_edit.kind))
    return problem;
  signal->kind = a_edit.kind;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetSignalCurve &a_edit) {
  auto *signal = FindSignalRow(a_recipe, a_edit.signal);
  if (!signal) {
    return Refuse(SignalWhere(a_edit.signal), "no such signal");
  }
  if (a_edit.curve) {
    const auto where = SignalWhere(a_edit.signal);
    if (const auto name = a_edit.curve->Named()) {
      if (!a_recipe.FindCurve(*name)) {
        return Refuse(where,
                      std::format("'curve' names unknown curve '@{}'", *name));
      }
    } else if (auto problem = CheckCurveText(where, *a_edit.curve)) {
      return problem;
    }
  }
  signal->curve = a_edit.curve;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetCurve &a_edit) {
  auto *curve = FindCurveRow(a_recipe, a_edit.curve);
  if (!curve) {
    return Refuse(CurveWhere(a_edit.curve), "no such curve");
  }
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          FirstError(CheckCurve(rows, Curve{a_edit.curve, a_edit.text})))
    return problem;
  curve->text = a_edit.text;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetMask &a_edit) {
  auto *mask = FindMaskRow(a_recipe, a_edit.mask);
  if (!mask) {
    return Refuse(MaskWhere(a_edit.mask), "no such mask");
  }
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          FirstError(CheckMask(rows, Mask{a_edit.mask, a_edit.text})))
    return problem;
  mask->text = a_edit.text;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const AddSignal &a_edit) {
  if (!IsName(a_edit.name)) {
    return Refuse(SignalWhere(a_edit.name),
                  "names are letters, digits and underscores, not starting "
                  "with a digit");
  }
  if (a_recipe.FindSignal(a_edit.name)) {
    return Refuse(SignalWhere(a_edit.name), "a signal has that name");
  }
  a_recipe.signals.push_back(
      Signal{a_edit.name, ConstantSignal{0.0f}, std::nullopt});
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const AddCurve &a_edit) {
  if (!IsName(a_edit.name)) {
    return Refuse(CurveWhere(a_edit.name),
                  "names are letters, digits and underscores, not starting "
                  "with a digit");
  }
  if (a_recipe.FindCurve(a_edit.name)) {
    return Refuse(CurveWhere(a_edit.name), "a curve has that name");
  }
  a_recipe.curves.push_back(Curve{a_edit.name, "x"});
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const AddMask &a_edit) {
  if (!IsName(a_edit.name)) {
    return Refuse(MaskWhere(a_edit.name),
                  "names are letters, digits and underscores, not starting "
                  "with a digit");
  }
  if (a_recipe.FindMask(a_edit.name) || a_recipe.FindSource(a_edit.name)) {
    return Refuse(MaskWhere(a_edit.name), "a mask or source has that name");
  }
  a_recipe.masks.push_back(Mask{a_edit.name, "1"});
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const AddSource &a_edit) {
  if (!IsName(a_edit.name)) {
    return Refuse(SourceWhere(a_edit.name),
                  "names are letters, digits and underscores, not starting "
                  "with a digit");
  }
  if (a_recipe.FindSource(a_edit.name) || a_recipe.FindMask(a_edit.name)) {
    return Refuse(SourceWhere(a_edit.name), "a source or mask has that name");
  }
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          FirstError(CheckSource(rows, Source{a_edit.name, a_edit.kind})))
    return problem;
  a_recipe.sources.push_back(Source{a_edit.name, a_edit.kind});
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetSource &a_edit) {
  auto *source = FindSourceRow(a_recipe, a_edit.name);
  if (!source) {
    return Refuse(SourceWhere(a_edit.name), "no such source");
  }
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          FirstError(CheckSource(rows, Source{a_edit.name, a_edit.kind})))
    return problem;
  source->kind = a_edit.kind;
  return std::nullopt;
}

template <class Fn> void ForEachSignalRef(Recipe &a_recipe, Fn a_visit);
template <class Fn> void ForEachImageRef(Recipe &a_recipe, Fn a_visit);
template <class Fn> void ForEachCurveRef(Recipe &a_recipe, Fn a_visit);
template <class Fn> void ForEachText(Recipe &a_recipe, Fn a_visit);
template <class Fn> void ForEachOverrideName(Recipe &a_recipe, Fn a_visit);
void RenameOverrides(Recipe &a_recipe, const std::string &a_from,
                     const std::string &a_to);
void RenameImageRefs(Recipe &a_recipe, std::string_view a_from,
                     std::string_view a_to);

Refusal Edit(Recipe &a_recipe, const RenameSignal &a_edit) {
  auto *signal = FindSignalRow(a_recipe, a_edit.from);
  if (!signal) {
    return Refuse(SignalWhere(a_edit.from), "no such signal");
  }
  if (!IsName(a_edit.to)) {
    return Refuse(SignalWhere(a_edit.from),
                  "names are letters, digits and underscores, not starting "
                  "with a digit");
  }
  if (a_edit.to == a_edit.from) {
    return std::nullopt;
  }
  if (a_recipe.FindSignal(a_edit.to)) {
    return Refuse(SignalWhere(a_edit.from),
                  std::format("a signal is already named '{}'", a_edit.to));
  }
  const bool imageShares =
      a_recipe.FindSource(a_edit.from) || a_recipe.FindMask(a_edit.from);
  signal->name = a_edit.to;
  ForEachSignalRef(a_recipe, [&](Ref &a_ref, const PropertyLocation &) {
    if (a_ref.name == a_edit.from) {
      a_ref.name = a_edit.to;
    }
  });
  ForEachText(a_recipe, [&](std::string &a_text, bool a_mask,
                            const PropertyLocation &) {
    if (!(a_mask && imageShares)) {
      a_text = RenameInExpression(a_text, a_edit.from, a_edit.to, false);
    }
  });
  RenameOverrides(a_recipe, a_edit.from, a_edit.to);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const RenameCurve &a_edit) {
  auto *curve = FindCurveRow(a_recipe, a_edit.from);
  if (!curve) {
    return Refuse(CurveWhere(a_edit.from), "no such curve");
  }
  if (!IsName(a_edit.to)) {
    return Refuse(CurveWhere(a_edit.from),
                  "names are letters, digits and underscores, not starting "
                  "with a digit");
  }
  if (a_edit.to == a_edit.from) {
    return std::nullopt;
  }
  if (a_recipe.FindCurve(a_edit.to)) {
    return Refuse(CurveWhere(a_edit.from),
                  std::format("a curve is already named '{}'", a_edit.to));
  }
  curve->name = a_edit.to;
  ForEachCurveRef(a_recipe, [&](CurveRef &a_ref, const PropertyLocation &) {
    if (a_ref.Named() == a_edit.from) {
      a_ref.text = "@" + a_edit.to;
    }
  });
  ForEachText(
      a_recipe, [&](std::string &a_text, bool, const PropertyLocation &) {
        a_text = RenameInExpression(a_text, a_edit.from, a_edit.to, true);
      });
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const RenameMask &a_edit) {
  auto *mask = FindMaskRow(a_recipe, a_edit.from);
  if (!mask) {
    return Refuse(MaskWhere(a_edit.from), "no such mask");
  }
  if (!IsName(a_edit.to)) {
    return Refuse(MaskWhere(a_edit.from),
                  "names are letters, digits and underscores, not starting "
                  "with a digit");
  }
  if (a_edit.to == a_edit.from) {
    return std::nullopt;
  }
  if (a_recipe.FindMask(a_edit.to) || a_recipe.FindSource(a_edit.to)) {
    return Refuse(
        MaskWhere(a_edit.from),
        std::format("a mask or source is already named '{}'", a_edit.to));
  }
  mask->name = a_edit.to;
  RenameImageRefs(a_recipe, a_edit.from, a_edit.to);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const RenameSource &a_edit) {
  auto *source = FindSourceRow(a_recipe, a_edit.from);
  if (!source) {
    return Refuse(SourceWhere(a_edit.from), "no such source");
  }
  if (!IsName(a_edit.to)) {
    return Refuse(SourceWhere(a_edit.from),
                  "names are letters, digits and underscores, not starting "
                  "with a digit");
  }
  if (a_edit.to == a_edit.from) {
    return std::nullopt;
  }
  if (a_recipe.FindSource(a_edit.to) || a_recipe.FindMask(a_edit.to)) {
    return Refuse(
        SourceWhere(a_edit.from),
        std::format("a source or mask is already named '{}'", a_edit.to));
  }
  source->name = a_edit.to;
  RenameImageRefs(a_recipe, a_edit.from, a_edit.to);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const RemoveSignal &a_edit) {
  const auto it =
      std::ranges::find(a_recipe.signals, a_edit.name, &Signal::name);
  if (it == a_recipe.signals.end()) {
    return Refuse(SignalWhere(a_edit.name), "no such signal");
  }
  const auto counts = CountReferences(a_recipe);
  if (const auto found = counts.signals.find(a_edit.name);
      found != counts.signals.end() && found->second > 0) {
    return Refuse(SignalWhere(a_edit.name),
                  std::format("referenced in {} place(s)", found->second));
  }
  a_recipe.signals.erase(it);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const RemoveCurve &a_edit) {
  const auto it = std::ranges::find(a_recipe.curves, a_edit.name, &Curve::name);
  if (it == a_recipe.curves.end()) {
    return Refuse(CurveWhere(a_edit.name), "no such curve");
  }
  const auto counts = CountReferences(a_recipe);
  if (const auto found = counts.curves.find(a_edit.name);
      found != counts.curves.end() && found->second > 0) {
    return Refuse(CurveWhere(a_edit.name),
                  std::format("referenced in {} place(s)", found->second));
  }
  a_recipe.curves.erase(it);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const RemoveMask &a_edit) {
  const auto it = std::ranges::find(a_recipe.masks, a_edit.name, &Mask::name);
  if (it == a_recipe.masks.end()) {
    return Refuse(MaskWhere(a_edit.name), "no such mask");
  }
  const auto counts = CountReferences(a_recipe);
  if (const auto found = counts.images.find(a_edit.name);
      found != counts.images.end() && found->second > 0) {
    return Refuse(MaskWhere(a_edit.name),
                  std::format("referenced in {} place(s)", found->second));
  }
  a_recipe.masks.erase(it);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const RemoveSource &a_edit) {
  const auto it =
      std::ranges::find(a_recipe.sources, a_edit.name, &Source::name);
  if (it == a_recipe.sources.end()) {
    return Refuse(SourceWhere(a_edit.name), "no such source");
  }
  const auto counts = CountReferences(a_recipe);
  if (const auto found = counts.images.find(a_edit.name);
      found != counts.images.end() && found->second > 0) {
    return Refuse(SourceWhere(a_edit.name),
                  std::format("referenced in {} place(s)", found->second));
  }
  a_recipe.sources.erase(it);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const AddLight &) {
  for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
    if (Get<LightOutput>(a_recipe.outputs[i])) {
      return Refuse("outputs",
                    std::format("output {} is already the light", i));
    }
  }
  a_recipe.outputs.emplace_back(LightOutput{});
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLightParam &a_edit) {
  auto found = FindLight(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  LightOutput candidate = *found.light;
  switch (a_edit.field) {
  case LightParam::kIntensity:
    candidate.intensity = a_edit.value;
    break;
  case LightParam::kSize:
    candidate.size = a_edit.value;
    break;
  case LightParam::kCutoff:
    candidate.cutoff = a_edit.value;
    break;
  }
  const auto where = OutputWhere(a_edit.output);
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          NewError(CheckOutput(rows, a_recipe.outputs[a_edit.output], where),
                   CheckOutput(rows, Output{candidate}, where)))
    return problem;
  *found.light = candidate;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLightVector &a_edit) {
  auto found = FindLight(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  LightOutput candidate = *found.light;
  if (a_edit.field == LightVector::kColor) {
    candidate.color = a_edit.value;
  } else {
    candidate.offset = a_edit.value;
  }
  const auto where = OutputWhere(a_edit.output);
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          NewError(CheckOutput(rows, a_recipe.outputs[a_edit.output], where),
                   CheckOutput(rows, Output{candidate}, where)))
    return problem;
  *found.light = candidate;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLightShadow &a_edit) {
  auto found = FindLight(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  found.light->shadow = a_edit.shadow;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLightBones &a_edit) {
  auto found = FindLight(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  LightOutput candidate = *found.light;
  candidate.bones = a_edit.bones;
  const auto where = OutputWhere(a_edit.output);
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          NewError(CheckOutput(rows, a_recipe.outputs[a_edit.output], where),
                   CheckOutput(rows, Output{candidate}, where)))
    return problem;
  found.light->bones = a_edit.bones;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLightReplace &a_edit) {
  auto found = FindLight(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  found.light->replace = a_edit.replace;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetLightSelector &a_edit) {
  auto found = FindLight(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  found.light->selector = a_edit.selector;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const ResetLight &a_edit) {
  auto found = FindLight(a_recipe, a_edit.output);
  if (found.problem)
    return found.problem;
  LightOutput reset;
  reset.bulb = found.light->bulb;
  reset.selector = found.light->selector;
  reset.replace = found.light->replace;
  *found.light = reset;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetShellParam &a_edit) {
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem = CheckScalarRef(CheckCtx{rows, "shell"},
                                    ShellParamName(a_edit.field), a_edit.value))
    return problem;
  auto &shell = a_recipe.shell;
  switch (a_edit.field) {
  case ShellParam::kAlpha:
    shell.alpha = a_edit.value;
    break;
  case ShellParam::kRimPower:
    shell.rimPower = a_edit.value;
    break;
  case ShellParam::kEmissive:
    shell.emissive = a_edit.value;
    break;
  case ShellParam::kScale:
    shell.pose.scale = a_edit.value;
    break;
  case ShellParam::kSpin:
    shell.pose.spin = a_edit.value;
    break;
  }
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetShellVector &a_edit) {
  const SignalGraph graph = GraphOf(a_recipe);
  const RowTypes rows{a_recipe, graph};
  if (auto problem =
          CheckVectorRef(CheckCtx{rows, "shell"}, ShellVectorName(a_edit.field),
                         a_edit.value, false))
    return problem;
  if (a_edit.field == ShellVector::kInflate) {
    a_recipe.shell.pose.inflate = a_edit.value;
  } else {
    a_recipe.shell.pose.offset = a_edit.value;
  }
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetShellPoint &a_edit) {
  if (a_edit.field == ShellPoint::kScalePoint) {
    a_recipe.shell.pose.scalePoint = a_edit.value;
  } else {
    if (a_edit.value == Vec3{}) {
      return Refuse("shell", "the spin axis cannot be zero");
    }
    a_recipe.shell.pose.spinAxis = a_edit.value;
  }
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetShellMaterial &a_edit) {
  for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
    const auto *material = Get<SurfaceOutput>(a_recipe.outputs[i]);
    if (material && material->surface == Surface::kShell &&
        !SurfaceHasSlot(Surface::kShell, a_edit.material, material->slot)) {
      return Refuse(
          "shell",
          std::format("output {} writes '{}' on the shell, which a {} shell "
                      "lacks",
                      i, SlotName(material->slot),
                      ShellMaterialName(a_edit.material)));
    }
  }
  a_recipe.shell.material = a_edit.material;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetShellBlend &a_edit) {
  a_recipe.shell.blend = a_edit.blend;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetShellDepthBias &a_edit) {
  a_recipe.shell.depthBias = a_edit.on;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const SetShellAlphaTest &a_edit) {
  if (a_edit.value < 0.0f || a_edit.value > 1.0f) {
    return Refuse("shell", "alphaTest is 0..1");
  }
  a_recipe.shell.alphaTest = a_edit.value;
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const ResetShell &) {
  a_recipe.shell = ShellSettings{};
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const ClearOutputs &) {
  a_recipe.outputs.clear();
  return Edit(a_recipe, ResetShell{});
}

template <class Visitor> void ForEachParam(Recipe &a_recipe, Visitor &a_visit);
struct LiteralVisitor;

Refusal Edit(Recipe &a_recipe, const ClearResources &);
Refusal Edit(Recipe &a_recipe, const ClearRecipe &);

template <class Visitor>
void VisitRef(Ref &a_ref, Visitor &a_visit, std::string_view a_property) {
  a_visit.Property(a_property);
  a_visit.Reference(a_ref);
}
template <class Visitor>
void VisitRef(std::optional<Ref> &a_ref, Visitor &a_visit,
              std::string_view a_property) {
  a_visit.Property(a_property);
  if (a_ref) {
    a_visit.Reference(*a_ref);
  }
}
template <class Visitor>
void VisitParam(Param &a_param, std::optional<float> a_default,
                Visitor &a_visit, std::string_view a_property) {
  a_visit.Property(a_property);
  a_visit.Scalar(a_param, a_default);
}
template <class Visitor>
void VisitParam(std::optional<Param> &a_param, std::optional<float> a_default,
                Visitor &a_visit, std::string_view a_property) {
  a_visit.Property(a_property);
  if (a_param) {
    a_visit.OptionalScalar(a_param, a_default);
  }
}
template <std::size_t N, class Visitor>
void VisitVector(
    std::variant<std::array<Param, N>, Ref> &a_param,
    std::type_identity_t<std::optional<std::array<float, N>>> a_default,
    Visitor &a_visit, std::string_view a_property) {
  a_visit.Property(a_property);
  a_visit.Vector(a_param, a_default);
}
template <std::size_t N, class Visitor>
void VisitVector(
    std::optional<std::variant<std::array<Param, N>, Ref>> &a_param,
    std::type_identity_t<std::optional<std::array<float, N>>> a_default,
    Visitor &a_visit, std::string_view a_property) {
  a_visit.Property(a_property);
  if (a_param) {
    a_visit.OptionalVector(a_param, a_default);
  }
}

std::optional<float> LiteralOf(const Param &a_param) {
  const auto *number = Get<float>(a_param);
  return number ? std::optional{*number} : std::nullopt;
}
template <std::size_t N>
std::optional<std::array<float, N>>
LiteralOf(const std::variant<std::array<Param, N>, Ref> &a_param) {
  const auto *parts = Get<std::array<Param, N>>(a_param);
  if (!parts) {
    return std::nullopt;
  }
  std::array<float, N> out{};
  for (std::size_t i = 0; i < N; ++i) {
    const auto number = LiteralOf((*parts)[i]);
    if (!number) {
      return std::nullopt;
    }
    out[i] = *number;
  }
  return out;
}

std::optional<float> ScalarDefault(Slot a_slot, ScalarField a_field) {
  return ScalarRequired(a_slot, a_field)
             ? std::optional{ScalarFallback(a_field)}
             : std::nullopt;
}

template <class Visitor>
void VisitSignalParams(SignalKind &a_kind, Visitor &a_visit) {
  Match(
      a_kind,
      [&](PulseSignal &s) {
        VisitParam(s.base, std::nullopt, a_visit, "base");
        VisitParam(s.amplitude, std::nullopt, a_visit, "amplitude");
        VisitParam(s.period, std::nullopt, a_visit, "period");
        VisitParam(s.phase, std::nullopt, a_visit, "phase");
      },
      [&](RampSignal &s) {
        VisitParam(s.from, std::nullopt, a_visit, "from");
        VisitParam(s.to, std::nullopt, a_visit, "to");
        VisitParam(s.seconds, std::nullopt, a_visit, "seconds");
      },
      [&](TriggerSignal &s) {
        VisitParam(s.lifetime, std::nullopt, a_visit, "lifetime");
        if (auto *when = Get<WhenOrigin>(s.origin)) {
          VisitRef(when->when, a_visit, "when");
          VisitRef(when->value, a_visit, "value");
        }
      },
      [&](PayloadSignal &s) { VisitRef(s.trigger, a_visit, "trigger"); },
      [&](CounterSignal &s) {
        VisitRef(s.trigger, a_visit, "trigger");
        VisitRef(s.reset, a_visit, "reset");
        VisitParam(s.cap, std::nullopt, a_visit, "cap");
      },
      [&](AccumulateSignal &s) {
        VisitRef(s.trigger, a_visit, "trigger");
        VisitParam(s.decay, std::nullopt, a_visit, "decay");
      },
      [&](NoiseSignal &s) {
        VisitParam(s.frequency, std::nullopt, a_visit, "frequency");
        VisitParam(s.amplitude, std::nullopt, a_visit, "amplitude");
      },
      [&](GradientSignal &s) {
        VisitParam(s.t, std::nullopt, a_visit, "t");
        std::size_t stopIndex = 0;
        for (auto &stop : s.stops) {
          VisitVector(stop.color, std::nullopt, a_visit,
                      std::format("stops[{}].color", stopIndex++));
        }
      },
      [&](DeltaSignal &s) { VisitRef(s.of, a_visit, "of"); },
      [&](SmoothSignal &s) {
        VisitRef(s.of, a_visit, "of");
        VisitParam(s.seconds, std::nullopt, a_visit, "seconds");
      },
      [](auto &) {});
}

template <class Visitor>
void VisitSourceParams(SourceKind &a_kind, Visitor &a_visit) {
  Match(
      a_kind,
      [&](ImageSource &s) {
        VisitVector(s.scroll, std::nullopt, a_visit, "scroll");
        VisitVector(s.tile, std::nullopt, a_visit, "tile");
      },
      [&](RippleSource &s) {
        VisitRef(s.trigger, a_visit, "trigger");
        VisitParam(s.speed, std::nullopt, a_visit, "speed");
        VisitParam(s.width, std::nullopt, a_visit, "width");
        VisitParam(s.decay, std::nullopt, a_visit, "decay");
      },
      [](auto &) {});
}

template <class Visitor>
void VisitSurfaceParams(SurfaceOutput &a_output, Visitor &a_visit) {
  for (const auto &row : kScalarFields) {
    if (auto *scalar = ScalarOf(a_output.scalars, row.value)) {
      VisitParam(*scalar, ScalarDefault(a_output.slot, row.value), a_visit,
                 row.name);
    }
  }
  const auto colour = ScalarDefault(a_output.slot, ScalarField::kColor);
  VisitVector(
      a_output.scalars.color,
      colour ? std::optional{std::array<float, 3>{*colour, *colour, *colour}}
             : std::nullopt,
      a_visit, "color");
  const Layer layerDefaults = DefaultLayer();
  std::size_t layerIndex = 0;
  const RelationshipOwner outputOwner = a_visit.location.owner;
  for (auto &layer : a_output.stack) {
    const auto *owner = Get<OutputOwner>(outputOwner);
    a_visit.Owner(LayerOwner{owner ? owner->output : 0, layerIndex++});
    VisitParam(layer.opacity, LiteralOf(layerDefaults.opacity), a_visit,
               "opacity");
    VisitVector(layer.color, std::nullopt, a_visit, "color");
  }
  a_visit.Owner(outputOwner);
}

template <class Visitor>
void VisitLightParams(LightOutput &a_output, Visitor &a_visit) {
  const LightOutput lightDefaults{};
  VisitVector(a_output.offset, LiteralOf(lightDefaults.offset), a_visit,
              "offset");
  VisitVector(a_output.color, LiteralOf(lightDefaults.color), a_visit, "color");
  VisitParam(a_output.intensity, LiteralOf(lightDefaults.intensity), a_visit,
             "intensity");
  VisitParam(a_output.size, LiteralOf(lightDefaults.size), a_visit, "size");
  VisitParam(a_output.cutoff, LiteralOf(lightDefaults.cutoff), a_visit,
             "cutoff");
}

template <class Visitor>
void VisitOutputParams(Output &a_output, Visitor &a_visit) {
  Match(
      a_output, [&](SurfaceOutput &o) { VisitSurfaceParams(o, a_visit); },
      [&](LightOutput &o) { VisitLightParams(o, a_visit); });
}

template <class Visitor>
void VisitShellParams(ShellSettings &a_shell, Visitor &a_visit) {
  const ShellSettings shellDefaults{};
  VisitParam(a_shell.alpha, LiteralOf(shellDefaults.alpha), a_visit, "alpha");
  VisitParam(a_shell.rimPower, LiteralOf(shellDefaults.rimPower), a_visit,
             "rimPower");
  VisitParam(a_shell.emissive, LiteralOf(shellDefaults.emissive), a_visit,
             "emissive");
  VisitVector(a_shell.pose.inflate, LiteralOf(shellDefaults.pose.inflate),
              a_visit, "inflate");
  VisitVector(a_shell.pose.offset, LiteralOf(shellDefaults.pose.offset),
              a_visit, "offset");
  VisitParam(a_shell.pose.scale, LiteralOf(shellDefaults.pose.scale), a_visit,
             "scale");
  VisitParam(a_shell.pose.spin, LiteralOf(shellDefaults.pose.spin), a_visit,
             "spin");
}

template <class Visitor> void ForEachParam(Recipe &a_recipe, Visitor &a_visit) {
  for (auto &signal : a_recipe.signals) {
    a_visit.Owner(ResourceRef{ResourceKind::kSignal, signal.name});
    VisitSignalParams(signal.kind, a_visit);
  }
  for (auto &source : a_recipe.sources) {
    a_visit.Owner(ResourceRef{ResourceKind::kSource, source.name});
    VisitSourceParams(source.kind, a_visit);
  }
  std::size_t outputIndex = 0;
  for (auto &output : a_recipe.outputs) {
    a_visit.Owner(OutputOwner{outputIndex++});
    VisitOutputParams(output, a_visit);
  }
  a_visit.Owner(ShellOwner{});
  VisitShellParams(a_recipe.shell, a_visit);
}

struct LocatedVisitor {
  PropertyLocation location;
  void Owner(RelationshipOwner a_owner) { location.owner = std::move(a_owner); }
  void Property(std::string_view a_property) {
    location.property = a_property;
    location.component.reset();
  }
};

template <class Fn> struct RefVisitor : LocatedVisitor {
  Fn &visit;
  explicit RefVisitor(Fn &a_visit) : visit(a_visit) {}
  void Reference(Ref &a_ref) { visit(a_ref, location); }
  void Scalar(Param &a_param, std::optional<float>) {
    if (auto *ref = Get<Ref>(a_param)) {
      Reference(*ref);
    }
  }
  void OptionalScalar(std::optional<Param> &a_param,
                      std::optional<float> a_default) {
    Scalar(*a_param, a_default);
  }
  template <std::size_t N>
  void Vector(std::variant<std::array<Param, N>, Ref> &a_param,
              std::optional<std::array<float, N>>) {
    if (auto *ref = Get<Ref>(a_param)) {
      Reference(*ref);
    } else if (auto *parts = Get<std::array<Param, N>>(a_param)) {
      std::size_t component = 0;
      for (auto &part : *parts) {
        location.component = component++;
        Scalar(part, std::nullopt);
      }
      location.component.reset();
    }
  }
  template <std::size_t N>
  void OptionalVector(
      std::optional<std::variant<std::array<Param, N>, Ref>> &a_param,
      std::optional<std::array<float, N>> a_default) {
    Vector(*a_param, a_default);
  }
};

template <class Fn> void ForEachSignalRef(Recipe &a_recipe, Fn a_visit) {
  RefVisitor<Fn> visitor{a_visit};
  ForEachParam(a_recipe, visitor);
}

struct LiteralVisitor : LocatedVisitor {
  void Reference(Ref &) {}
  void Scalar(Param &a_param, std::optional<float> a_default) {
    if (Is<Ref>(a_param)) {
      a_param = a_default.value_or(0.0f);
    }
  }
  void OptionalScalar(std::optional<Param> &a_param,
                      std::optional<float> a_default) {
    if (!Is<Ref>(*a_param)) {
      return;
    }
    if (a_default) {
      *a_param = *a_default;
    } else {
      a_param.reset();
    }
  }
  template <std::size_t N>
  void Vector(std::variant<std::array<Param, N>, Ref> &a_param,
              std::optional<std::array<float, N>> a_default) {
    std::array<Param, N> literal{};
    const auto fallback = a_default.value_or(std::array<float, N>{});
    for (std::size_t i = 0; i < N; ++i) {
      literal[i] = fallback[i];
    }
    if (Is<Ref>(a_param)) {
      a_param = literal;
      return;
    }
    auto &parts = *Get<std::array<Param, N>>(a_param);
    for (std::size_t i = 0; i < N; ++i) {
      if (Is<Ref>(parts[i])) {
        parts[i] = fallback[i];
      }
    }
  }
  template <std::size_t N>
  void OptionalVector(
      std::optional<std::variant<std::array<Param, N>, Ref>> &a_param,
      std::optional<std::array<float, N>> a_default) {
    const auto *parts = Get<std::array<Param, N>>(*a_param);
    const bool names =
        Is<Ref>(*a_param) ||
        (parts && std::ranges::any_of(
                      *parts, [](const Param &p) { return Is<Ref>(p); }));
    if (!names) {
      return;
    }
    if (a_default) {
      Vector(*a_param, a_default);
    } else {
      a_param.reset();
    }
  }
};

template <class Fn> void ForEachOverrideName(Recipe &a_recipe, Fn a_visit) {
  for (std::size_t index = 0; index < a_recipe.variants.size(); ++index) {
    for (const auto &[name, value] : a_recipe.variants[index].overrides) {
      a_visit(name, PropertyLocation{VariantOwner{index}, "override", {}});
    }
  }
}

void RenameOverrides(Recipe &a_recipe, const std::string &a_from,
                     const std::string &a_to) {
  for (auto &variant : a_recipe.variants) {
    const auto it = variant.overrides.find(a_from);
    if (it != variant.overrides.end()) {
      auto value = it->second;
      variant.overrides.erase(it);
      variant.overrides.emplace(a_to, value);
    }
  }
}

template <class Fn> void ForEachMaterialLayer(Recipe &a_recipe, Fn a_visit) {
  for (std::size_t output = 0; output < a_recipe.outputs.size(); ++output) {
    auto *material = Get<SurfaceOutput>(a_recipe.outputs[output]);
    if (!material) {
      continue;
    }
    for (std::size_t layer = 0; layer < material->stack.size(); ++layer) {
      a_visit(material->stack[layer], LayerOwner{output, layer});
    }
  }
}

template <class Fn> void ForEachText(Recipe &a_recipe, Fn a_visit) {
  for (auto &signal : a_recipe.signals) {
    const ResourceRef owner{ResourceKind::kSignal, signal.name};
    if (auto *expr = Get<ExprSignal>(signal.kind)) {
      a_visit(expr->text, false, PropertyLocation{owner, "expression", {}});
    }
    if (signal.curve && !signal.curve->Named()) {
      a_visit(signal.curve->text, false, PropertyLocation{owner, "curve", {}});
    }
  }
  for (auto &curve : a_recipe.curves) {
    a_visit(curve.text, false,
            PropertyLocation{ResourceRef{ResourceKind::kCurve, curve.name},
                             "expression",
                             {}});
  }
  for (auto &mask : a_recipe.masks) {
    a_visit(mask.text, true,
            PropertyLocation{
                ResourceRef{ResourceKind::kMask, mask.name}, "expression", {}});
  }
  ForEachMaterialLayer(a_recipe, [&](Layer &a_layer, LayerOwner a_owner) {
    if (a_layer.curve && !a_layer.curve->Named()) {
      a_visit(a_layer.curve->text, false,
              PropertyLocation{a_owner, "curve", {}});
    }
  });
}

template <class Fn> void ForEachImageRef(Recipe &a_recipe, Fn a_visit) {
  ForEachMaterialLayer(a_recipe, [&](Layer &a_layer, LayerOwner a_owner) {
    if (auto *ref = Get<Ref>(a_layer.source)) {
      a_visit(*ref, PropertyLocation{a_owner, "source", {}});
    }
    if (a_layer.mask) {
      a_visit(*a_layer.mask, PropertyLocation{a_owner, "mask", {}});
    }
  });
}

void RenameImageRefs(Recipe &a_recipe, std::string_view a_from,
                     std::string_view a_to) {
  ForEachImageRef(a_recipe, [&](Ref &a_ref, const PropertyLocation &) {
    if (a_ref.name == a_from) {
      a_ref.name = std::string{a_to};
    }
  });
  for (auto &mask : a_recipe.masks) {
    mask.text = RenameInExpression(mask.text, a_from, a_to, false);
  }
}

template <class Fn> void ForEachCurveRef(Recipe &a_recipe, Fn a_visit) {
  for (auto &signal : a_recipe.signals) {
    if (signal.curve && signal.curve->Named()) {
      a_visit(*signal.curve,
              PropertyLocation{ResourceRef{ResourceKind::kSignal, signal.name},
                               "curve",
                               {}});
    }
  }
  ForEachMaterialLayer(a_recipe, [&](Layer &a_layer, LayerOwner a_owner) {
    if (a_layer.curve && a_layer.curve->Named()) {
      a_visit(*a_layer.curve, PropertyLocation{a_owner, "curve", {}});
    }
  });
}

Refusal CheckSignalKind(const Recipe &a_recipe, const std::string &a_where,
                        const SignalKind &a_kind) {
  SignalKind copy = a_kind;
  std::vector<std::string> reads;
  auto collect = [&](Ref &a_ref, const PropertyLocation &) {
    reads.push_back(a_ref.name);
  };
  RefVisitor<decltype(collect)> visitor{collect};
  VisitSignalParams(copy, visitor);
  for (const auto &name : reads) {
    if (!a_recipe.FindSignal(name)) {
      return Refuse(a_where, std::format("reads unknown signal '@{}'", name));
    }
  }
  if (const auto *expr = Get<ExprSignal>(a_kind)) {
    return CheckText(a_where, expr->text);
  }
  if (const auto *efsh = Get<EfshSignal>(a_kind);
      efsh && efsh->record.text.empty()) {
    return Refuse(a_where, "an efsh signal names its effect shader");
  }
  if (const auto *av = Get<ActorValueSignal>(a_kind);
      av && av->actorValue.empty()) {
    return Refuse(a_where, "an av signal names an actor value");
  }
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const ClearResources &) {
  a_recipe.signals.clear();
  a_recipe.curves.clear();
  a_recipe.sources.clear();
  a_recipe.masks.clear();
  a_recipe.variants.clear();
  for (auto &output : a_recipe.outputs) {
    if (auto *material = Get<SurfaceOutput>(output)) {
      std::erase_if(material->stack,
                    [](const Layer &l) { return Is<Ref>(l.source); });
      for (auto &layer : material->stack) {
        layer.mask.reset();
        layer.curve.reset();
      }
    }
  }
  LiteralVisitor literal;
  ForEachParam(a_recipe, literal);
  return std::nullopt;
}

Refusal Edit(Recipe &a_recipe, const ClearRecipe &) {
  if (auto problem = Edit(a_recipe, ClearOutputs{}))
    return problem;
  return Edit(a_recipe, ClearResources{});
}

struct DescribeVisitor {
  std::string operator()(const SetLayerSource &e) const {
    return std::format("{}: source {}", LayerWhere(e.output, e.layer),
                       LayerSourceText(e.source));
  }
  std::string operator()(const SetLayerCurve &e) const {
    return std::format("{}: curve {}", LayerWhere(e.output, e.layer),
                       CurveText(e.curve));
  }
  std::string operator()(const SetLayerBlend &e) const {
    return std::format("{}: blend {}", LayerWhere(e.output, e.layer),
                       BlendName(e.blend));
  }
  std::string operator()(const SetLayerOpacity &e) const {
    return std::format("{}: opacity {}", LayerWhere(e.output, e.layer),
                       ParamText(e.opacity));
  }
  std::string operator()(const SetLayerColor &e) const {
    return std::format("{}: color {}", LayerWhere(e.output, e.layer),
                       e.color ? Vec3ParamText(*e.color) : "none");
  }
  std::string operator()(const SetLayerMask &e) const {
    return std::format("{}: mask {}", LayerWhere(e.output, e.layer),
                       e.mask ? "@" + e.mask->name : "none");
  }
  std::string operator()(const SetLayerChannels &e) const {
    return std::format("{}: channels {}", LayerWhere(e.output, e.layer),
                       e.channels.ToString());
  }
  std::string operator()(const AddLayer &e) const {
    return e.at ? std::format("{}: add layer", LayerWhere(e.output, *e.at))
                : std::format("{}: add layer on top", OutputWhere(e.output));
  }
  std::string operator()(const RemoveLayer &e) const {
    return std::format("{}: remove", LayerWhere(e.output, e.layer));
  }
  std::string operator()(const MoveLayer &e) const {
    return std::format("{}: move to {}", LayerWhere(e.output, e.from), e.to);
  }
  std::string operator()(const ClearLayers &e) const {
    return std::format("{}: clear layers", OutputWhere(e.output));
  }
  std::string operator()(const AddOutput &e) const {
    return e.selector.All()
               ? std::format("outputs: add {} {}", SurfaceName(e.surface),
                             SlotName(e.slot))
               : std::format("outputs: add {} {} on {}", SurfaceName(e.surface),
                             SlotName(e.slot), SelectorText(e.selector));
  }
  std::string operator()(const RemoveOutput &e) const {
    return std::format("{}: remove", OutputWhere(e.output));
  }
  std::string operator()(const SetScalar &e) const {
    return std::format("{}: {} {}", OutputWhere(e.output),
                       ScalarFieldName(e.field), ParamText(e.value));
  }
  std::string operator()(const SetColorScalar &e) const {
    return std::format("{}: color {}", OutputWhere(e.output),
                       Vec3ParamText(e.color));
  }
  std::string operator()(const SetOutputReplace &e) const {
    return std::format("{}: replace {}", OutputWhere(e.output),
                       e.replace ? "on" : "off");
  }
  std::string operator()(const SetOutputSelector &e) const {
    return e.selector.All() ? std::format("{}: all", OutputWhere(e.output))
                            : std::format("{}: on {}", OutputWhere(e.output),
                                          SelectorText(e.selector));
  }
  std::string operator()(const AddKey &e) const {
    return std::format("keys: add {}", e.key.ToString());
  }
  std::string operator()(const RemoveKey &e) const {
    return std::format("keys: remove {}", e.key.ToString());
  }
  std::string operator()(const ClearOutputs &) const {
    return std::string{"outputs: clear, shell reset"};
  }
  std::string operator()(const ClearResources &) const {
    return std::string{"resources: clear"};
  }
  std::string operator()(const ClearRecipe &) const {
    return std::string{"recipe: clear"};
  }
  std::string operator()(const SetPriority &e) const {
    return e.priority ? std::format("priority: {}", *e.priority)
                      : std::string{"priority: none"};
  }
  std::string operator()(const SetClockSpeed &e) const {
    return std::format("clock: speed {}", e.speed);
  }
  std::string operator()(const SetConstant &e) const {
    return std::format("{}: constant {}", SignalWhere(e.signal),
                       ValueText(e.value));
  }
  std::string operator()(const SetExpression &e) const {
    return std::format("{}: expr {}", SignalWhere(e.signal), e.text);
  }
  std::string operator()(const SetSignal &e) const {
    return std::format("{}: {}", SignalWhere(e.signal),
                       SignalKindName(SignalKindOf(e.kind)));
  }
  std::string operator()(const SetSignalCurve &e) const {
    return std::format("{}: curve {}", SignalWhere(e.signal),
                       CurveText(e.curve));
  }
  std::string operator()(const SetCurve &e) const {
    return std::format("curve {}: {}", e.curve, e.text);
  }
  std::string operator()(const SetMask &e) const {
    return std::format("mask {}: {}", e.mask, e.text);
  }
  std::string operator()(const AddSignal &e) const {
    return std::format("signals: add {}", e.name);
  }
  std::string operator()(const AddCurve &e) const {
    return std::format("curves: add {}", e.name);
  }
  std::string operator()(const RenameSignal &e) const {
    return std::format("{}: rename to {}", SignalWhere(e.from), e.to);
  }
  std::string operator()(const RenameCurve &e) const {
    return std::format("{}: rename to {}", CurveWhere(e.from), e.to);
  }
  std::string operator()(const RemoveSignal &e) const {
    return std::format("{}: remove", SignalWhere(e.name));
  }
  std::string operator()(const RemoveCurve &e) const {
    return std::format("{}: remove", CurveWhere(e.name));
  }
  std::string operator()(const AddMask &e) const {
    return std::format("masks: add {}", e.name);
  }
  std::string operator()(const RenameMask &e) const {
    return std::format("{}: rename to {}", MaskWhere(e.from), e.to);
  }
  std::string operator()(const RemoveMask &e) const {
    return std::format("{}: remove", MaskWhere(e.name));
  }
  std::string operator()(const AddSource &e) const {
    return std::format("sources: add {} ({})", e.name, SourceKindName(e.kind));
  }
  std::string operator()(const SetSource &e) const {
    return std::format("{}: {}", SourceWhere(e.name), DescribeSource(e.kind));
  }
  std::string operator()(const RenameSource &e) const {
    return std::format("{}: rename to {}", SourceWhere(e.from), e.to);
  }
  std::string operator()(const RemoveSource &e) const {
    return std::format("{}: remove", SourceWhere(e.name));
  }
  std::string operator()(const AddLight &) const {
    return std::string{"outputs: add light"};
  }
  std::string operator()(const SetLightParam &e) const {
    return std::format("{}: {} {}", OutputWhere(e.output),
                       LightParamName(e.field), ParamText(e.value));
  }
  std::string operator()(const SetLightVector &e) const {
    return std::format("{}: {} {}", OutputWhere(e.output),
                       LightVectorName(e.field), Vec3ParamText(e.value));
  }
  std::string operator()(const SetLightShadow &e) const {
    return std::format("{}: shadow {}", OutputWhere(e.output),
                       e.shadow ? "on" : "off");
  }
  std::string operator()(const SetLightBones &e) const {
    return std::format("{}: bones {}", OutputWhere(e.output),
                       Is<NamedBones>(e.bones) ? "named" : "skinned");
  }
  std::string operator()(const SetLightReplace &e) const {
    return std::format("{}: replace {}", OutputWhere(e.output),
                       e.replace ? "on" : "off");
  }
  std::string operator()(const SetLightSelector &e) const {
    return e.selector.All() ? std::format("{}: all", OutputWhere(e.output))
                            : std::format("{}: on {}", OutputWhere(e.output),
                                          SelectorText(e.selector));
  }
  std::string operator()(const ResetLight &e) const {
    return std::format("{}: reset light", OutputWhere(e.output));
  }
  std::string operator()(const SetShellParam &e) const {
    return std::format("shell: {} {}", ShellParamName(e.field),
                       ParamText(e.value));
  }
  std::string operator()(const SetShellVector &e) const {
    return std::format("shell: {} {}", ShellVectorName(e.field),
                       Vec3ParamText(e.value));
  }
  std::string operator()(const SetShellPoint &e) const {
    return std::format("shell: {} {}, {}, {}", ShellPointName(e.field),
                       e.value.x, e.value.y, e.value.z);
  }
  std::string operator()(const SetShellMaterial &e) const {
    return std::format("shell: material {}", ShellMaterialName(e.material));
  }
  std::string operator()(const SetShellBlend &e) const {
    return std::format("shell: blend {}", ShellBlendName(e.blend));
  }
  std::string operator()(const SetShellDepthBias &e) const {
    return std::format("shell: depthBias {}", e.on ? "on" : "off");
  }
  std::string operator()(const SetShellAlphaTest &e) const {
    return std::format("shell: alphaTest {}", e.value);
  }
  std::string operator()(const ResetShell &) const {
    return std::string{"shell: reset"};
  }
};
}

std::optional<Diagnostic> Apply(Recipe &a_recipe, const RecipeEdit &a_edit) {
  return Match(a_edit, [&](const auto &a_specific) {
    return Edit(a_recipe, a_specific);
  });
}

std::expected<Recipe, Diagnostic> PrepareEdits(const Recipe &a_recipe,
                                               const EditBatch &a_batch) {
  Recipe copy = a_recipe;
  for (const auto &edit : a_batch.edits) {
    if (auto problem = Apply(copy, edit)) {
      return std::unexpected(*problem);
    }
  }
  return copy;
}

std::optional<Diagnostic> Apply(Recipe &a_recipe, const EditBatch &a_batch) {
  std::expected<Recipe, Diagnostic> prepared = PrepareEdits(a_recipe, a_batch);
  if (!prepared) {
    return prepared.error();
  }
  a_recipe = std::move(*prepared);
  return std::nullopt;
}

std::string Describe(const EditBatch &a_batch) {
  std::string text;
  for (const auto &edit : a_batch.edits) {
    text += text.empty() ? Describe(edit) : "; " + Describe(edit);
  }
  return text;
}

std::string Describe(const RecipeEdit &a_edit) {
  return Match(a_edit, DescribeVisitor{});
}

std::string_view LightParamName(LightParam a_field) noexcept {
  switch (a_field) {
  case LightParam::kIntensity:
    return "intensity";
  case LightParam::kSize:
    return "size";
  case LightParam::kCutoff:
    return "cutoff";
  }
  return "?";
}

std::string_view LightVectorName(LightVector a_field) noexcept {
  return a_field == LightVector::kColor ? "color" : "offset";
}

std::string_view ShellParamName(ShellParam a_field) noexcept {
  switch (a_field) {
  case ShellParam::kAlpha:
    return "alpha";
  case ShellParam::kRimPower:
    return "rimPower";
  case ShellParam::kEmissive:
    return "emissive";
  case ShellParam::kScale:
    return "scale";
  case ShellParam::kSpin:
    return "spin";
  }
  return "?";
}

std::string_view ShellVectorName(ShellVector a_field) noexcept {
  return a_field == ShellVector::kInflate ? "inflate" : "offset";
}

std::string_view ShellPointName(ShellPoint a_field) noexcept {
  return a_field == ShellPoint::kScalePoint ? "scalePoint" : "spinAxis";
}

std::vector<Relationship> RelationshipsOf(const Recipe &a_recipe) {
  Recipe copy = a_recipe;
  std::vector<Relationship> relationships;
  const auto imageKind = [&](const std::string &name) {
    return a_recipe.FindMask(name) ? ResourceKind::kMask
                                   : ResourceKind::kSource;
  };
  ForEachSignalRef(copy, [&](Ref &ref, const PropertyLocation &location) {
    relationships.push_back({location, {ResourceKind::kSignal, ref.name}});
  });
  ForEachCurveRef(copy, [&](CurveRef &ref, const PropertyLocation &location) {
    if (const auto name = ref.Named()) {
      relationships.push_back({location, {ResourceKind::kCurve, *name}});
    }
  });
  ForEachText(copy, [&](std::string &text, bool mask,
                        const PropertyLocation &location) {
    const auto program = Program::Parse(text);
    if (!program) {
      return;
    }
    for (const std::string &name : program->References()) {
      const bool image =
          mask && (a_recipe.FindMask(name) || a_recipe.FindSource(name));
      relationships.push_back(
          {location, {image ? imageKind(name) : ResourceKind::kSignal, name}});
    }
    for (const std::string &name : program->Curves()) {
      relationships.push_back({location, {ResourceKind::kCurve, name}});
    }
  });
  ForEachImageRef(copy, [&](Ref &ref, const PropertyLocation &location) {
    relationships.push_back({location, {imageKind(ref.name), ref.name}});
  });
  ForEachOverrideName(
      copy, [&](const std::string &name, const PropertyLocation &location) {
        relationships.push_back({location, {ResourceKind::kSignal, name}});
      });
  return relationships;
}

ReferenceCounts CountReferences(const Recipe &a_recipe) {
  ReferenceCounts counts;
  for (const Relationship &relationship : RelationshipsOf(a_recipe)) {
    const ResourceRef &driver = relationship.driver;
    if (driver.kind == ResourceKind::kCurve) {
      ++counts.curves[driver.name];
      continue;
    }
    const auto *owner = Get<ResourceRef>(relationship.consumer.owner);
    const bool maskExpression = owner && owner->kind == ResourceKind::kMask;
    if (driver.kind == ResourceKind::kSignal || maskExpression) {
      ++counts.signals[driver.name];
    }
    if (driver.kind == ResourceKind::kSource ||
        driver.kind == ResourceKind::kMask || maskExpression) {
      ++counts.images[driver.name];
    }
  }
  return counts;
}

std::string RenameInExpression(std::string_view a_text,
                               std::span<const ExpressionRename> a_renames,
                               bool a_curve) {
  const auto nameChar = [](char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
  };
  std::string out;
  out.reserve(a_text.size());
  std::size_t at = 0;
  while (at < a_text.size()) {
    if (a_text[at] != '@') {
      out += a_text[at++];
      continue;
    }
    std::size_t end = at + 1;
    while (end < a_text.size() && nameChar(a_text[end])) {
      ++end;
    }
    const std::string_view name = a_text.substr(at + 1, end - at - 1);
    std::size_t next = end;
    while (next < a_text.size() &&
           std::isspace(static_cast<unsigned char>(a_text[next]))) {
      ++next;
    }
    const bool call = next < a_text.size() && a_text[next] == '(';
    if (!name.empty() && call == a_curve) {
      const auto rename =
          std::ranges::find(a_renames, name, &ExpressionRename::from);
      if (rename != a_renames.end()) {
        out += '@';
        out += rename->to;
        at = end;
        continue;
      }
    }
    out += a_text[at];
    ++at;
  }
  return out;
}

std::string RenameInExpression(std::string_view a_text, std::string_view a_from,
                               std::string_view a_to, bool a_curve) {
  const ExpressionRename rename{std::string{a_from}, std::string{a_to}};
  return RenameInExpression(a_text, std::span{&rename, 1}, a_curve);
}

Layer DefaultLayer() {
  Layer layer;
  layer.source = Vec3{1.0f, 1.0f, 1.0f};
  layer.blend = Blend::kReplace;
  layer.opacity = 1.0f;
  return layer;
}

SurfaceOutput DefaultOutput(Surface a_surface, Slot a_slot) {
  SurfaceOutput output;
  output.surface = a_surface;
  output.slot = a_slot;
  for (const auto field : ScalarsOf(a_slot)) {
    if (!ScalarRequired(a_slot, field)) {
      continue;
    }
    const float fallback = ScalarFallback(field);
    if (field == ScalarField::kColor) {
      output.scalars.color = std::array<Param, 3>{fallback, fallback, fallback};
    } else if (auto *scalar = ScalarOf(output.scalars, field)) {
      *scalar = fallback;
    }
  }
  return output;
}
}
