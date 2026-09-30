// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Signals.h"

#include "recipe/Efsh.h"
#include "recipe/Expression.h"
#include "recipe/Words.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <numbers>
#include <unordered_set>

namespace BetterEnchantmentEffects {
namespace {
constexpr float kEpsilon = 1e-6f;

float Wave(Waveform a_waveform, float a_phase) noexcept {
  const float p = a_phase - std::floor(a_phase);
  switch (a_waveform) {
  case Waveform::kSine:
    return 0.5f - 0.5f * std::cos(2.0f * std::numbers::pi_v<float> * p);
  case Waveform::kTriangle:
    return p < 0.5f ? p * 2.0f : 2.0f - p * 2.0f;
  case Waveform::kSquare:
    return p < 0.5f ? 1.0f : 0.0f;
  case Waveform::kSaw:
    return p;
  }
  return 0.0f;
}

float Lattice(std::int64_t a_i, std::uint32_t a_seed) noexcept {
  std::uint32_t h =
      static_cast<std::uint32_t>(a_i) * 0x9E3779B1u ^ (a_seed + 0x7F4A7C15u);
  h ^= h >> 15;
  h *= 0x2C1B3C6Du;
  h ^= h >> 12;
  h *= 0x297A2D39u;
  h ^= h >> 15;
  return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x7FFFFFu) -
         1.0f;
}

float ValueNoise(float a_t, std::uint32_t a_seed) noexcept {
  const float f = std::floor(a_t);
  const std::int64_t i = static_cast<std::int64_t>(f);
  const float frac = a_t - f;
  const float s = frac * frac * (3.0f - 2.0f * frac);
  return Lattice(i, a_seed) + (Lattice(i + 1, a_seed) - Lattice(i, a_seed)) * s;
}

Vec3 Lerp(const Vec3 &a, const Vec3 &b, float t) noexcept {
  return Vec3{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
              a.z + (b.z - a.z) * t};
}

Value ZeroOf(ValueType a_type) noexcept {
  switch (a_type) {
  case ValueType::kVec2:
    return Vec2{};
  case ValueType::kVec3:
    return Vec3{};
  default:
    return 0.0f;
  }
}

bool MatchesFilter(const EventFilter &a_filter,
                   const TriggerPayload &a_payload) noexcept {
  if (!a_filter.node.empty() && !GlobMatch(a_filter.node, a_payload.node)) {
    return false;
  }
  if (!a_filter.arg.empty() && !GlobMatch(a_filter.arg, a_payload.arg)) {
    return false;
  }
  const float value = AsScalar(a_payload.value);
  if (a_filter.value.min && value < *a_filter.value.min) {
    return false;
  }
  if (a_filter.value.max && value > *a_filter.value.max) {
    return false;
  }
  return true;
}

bool SlotWritable(Surface a_surface, ShellMaterial a_shell,
                  Slot a_slot) noexcept {
  if (a_surface == Surface::kShell && a_shell == ShellMaterial::kVanilla) {
    return a_slot == Slot::kEmissive;
  }
  return true;
}

void CheckScalar(const RowTypes &a_rows, const Reporter &a_report,
                 const Param &a_param, std::string_view a_field) {
  const auto *ref = Get<Ref>(a_param);
  if (!ref) {
    return;
  }
  const auto type = SignalTypeOf(a_rows, ref->name);
  if (!type) {
    a_report.Error(
        std::format("'{}' reads unknown signal '@{}'", a_field, ref->name));
  } else if (*type != ValueType::kScalar) {
    a_report.Error(std::format("'{}' must be a scalar; '@{}' is a {}", a_field,
                               ref->name, Name(*type)));
  }
}

template <std::size_t N>
void CheckVector(const RowTypes &a_rows, const Reporter &a_report,
                 const std::variant<std::array<Param, N>, Ref> &a_param,
                 std::string_view a_field, bool a_color) {
  const ValueType want = N == 2 ? ValueType::kVec2 : ValueType::kVec3;
  Match(
      a_param,
      [&](const Ref &r) {
        const auto type = SignalTypeOf(a_rows, r.name);
        if (!type) {
          a_report.Error(
              std::format("'{}' reads unknown signal '@{}'", a_field, r.name));
        } else if (*type != want) {
          a_report.Error(std::format("'{}' must be a {}; '@{}' is a {}",
                                     a_field, Name(want), r.name, Name(*type)));
        }
      },
      [&](const std::array<Param, N> &parts) {
        for (const auto &p : parts) {
          CheckScalar(a_rows, a_report, p, a_field);
          if (const auto *number = Get<float>(p);
              a_color && number && (*number < 0.0f || *number > 1.0f)) {
            a_report.Error(std::format("'{}' components are 0..1", a_field));
          }
        }
      });
}

void CheckTrigger(const RowTypes &a_rows, const Reporter &a_report,
                  const Ref &a_ref, std::string_view a_field) {
  const auto index = a_rows.graph.FindSignalIndex(a_ref.name);
  const auto *signal = index ? a_rows.graph.SignalAt(*index) : nullptr;
  if (!signal) {
    a_report.Error(
        std::format("'{}' names unknown signal '@{}'", a_field, a_ref.name));
  } else if (!Is<TriggerSignal>(signal->kind)) {
    a_report.Error(std::format("'{}' must name a trigger; '@{}' is not one",
                               a_field, a_ref.name));
  }
}

}

std::optional<ValueType> SignalTypeOf(const RowTypes &a_rows,
                                      std::string_view a_name) noexcept {
  const auto index = a_rows.graph.FindSignalIndex(a_name);
  return index ? a_rows.graph.TypeOf(*index) : std::nullopt;
}

std::optional<ValueType> TexelTypeOf(const RowTypes &a_rows,
                                     std::string_view a_name) {
  const auto index = a_rows.graph.FindNodeIndex(a_name);
  if (!index || a_rows.graph.IsDisabled(*index))
    return std::nullopt;
  return a_rows.graph.SignalAt(*index) || a_rows.graph.SourceAt(*index) ||
                 a_rows.graph.IsMask(*index)
             ? a_rows.graph.TypeOf(*index)
             : std::nullopt;
}

std::optional<ValueType> MaskTypeOf(const RowTypes &a_rows,
                                    const Mask &a_mask) {
  const auto index = a_rows.graph.FindNodeIndex(a_mask.name);
  return index && !a_rows.graph.IsDisabled(*index) &&
                 a_rows.graph.IsMask(*index)
             ? a_rows.graph.TypeOf(*index)
             : std::nullopt;
}

bool NamesTrigger(const RowTypes &a_rows, std::string_view a_name) noexcept {
  const auto index = a_rows.graph.FindSignalIndex(a_name);
  const auto *signal = index ? a_rows.graph.SignalAt(*index) : nullptr;
  return signal && Is<TriggerSignal>(signal->kind);
}

namespace {
std::vector<Diagnostic> RowDiagnostics(const RowTypes &a_rows,
                                       std::string_view a_where) {
  std::vector<Diagnostic> out;
  for (const auto &diagnostic : a_rows.graph.Diagnostics()) {
    if (diagnostic.where == a_where)
      out.push_back(diagnostic);
  }
  return out;
}

template <class Row>
std::vector<Diagnostic> CandidateDiagnostics(const RowTypes &a_rows,
                                             const Row &a_candidate,
                                             std::vector<Row> Recipe::*a_member,
                                             std::string_view a_where) {
  const auto &original = a_rows.recipe.*a_member;
  const auto found = std::ranges::find(original, a_candidate.name, &Row::name);
  if (found != original.end() && *found == a_candidate) {
    return RowDiagnostics(a_rows, a_where);
  }
  Recipe candidate = a_rows.recipe;
  auto &rows = candidate.*a_member;
  if (found == original.end()) {
    rows.push_back(a_candidate);
  } else {
    rows[static_cast<std::size_t>(found - original.begin())] = a_candidate;
  }
  const auto graph = RecipeGraph::Compile(candidate);
  return RowDiagnostics(RowTypes{candidate, graph}, a_where);
}

std::vector<Diagnostic> LayerDiagnostics(const RowTypes &a_rows,
                                         const Layer &a_candidate,
                                         std::string_view a_where) {
  for (std::size_t i = 0; i < a_rows.recipe.outputs.size(); ++i) {
    const auto *original = Get<SurfaceOutput>(a_rows.recipe.outputs[i]);
    if (!original)
      continue;
    for (std::size_t j = 0; j <= original->stack.size(); ++j) {
      if (LayerWhere(i, j) != a_where)
        continue;
      if (j < original->stack.size() && original->stack[j] == a_candidate) {
        return RowDiagnostics(a_rows, a_where);
      }
      Recipe candidate = a_rows.recipe;
      auto &stack = Get<SurfaceOutput>(candidate.outputs[i])->stack;
      if (j < stack.size())
        stack[j] = a_candidate;
      else
        stack.push_back(a_candidate);
      const auto graph = RecipeGraph::Compile(candidate);
      return RowDiagnostics(RowTypes{candidate, graph}, a_where);
    }
  }
  Recipe candidate = a_rows.recipe;
  const auto where = LayerWhere(candidate.outputs.size(), 0);
  SurfaceOutput surface;
  surface.stack.push_back(a_candidate);
  candidate.outputs.emplace_back(std::move(surface));
  const auto graph = RecipeGraph::Compile(candidate);
  auto diagnostics = RowDiagnostics(RowTypes{candidate, graph}, where);
  for (auto &diagnostic : diagnostics)
    diagnostic.where = a_where;
  return diagnostics;
}
}

std::vector<Diagnostic> CheckCurve(const RowTypes &a_rows,
                                   const Curve &a_curve) {
  return CandidateDiagnostics(a_rows, a_curve, &Recipe::curves,
                              CurveWhere(a_curve.name));
}

std::vector<Diagnostic> CheckSource(const RowTypes &a_rows,
                                    const Source &a_source) {
  auto out = CandidateDiagnostics(a_rows, a_source, &Recipe::sources,
                                  SourceWhere(a_source.name));
  for (auto &diagnostic : CheckSourceInputs(a_rows, a_source)) {
    const bool present = std::ranges::any_of(out, [&](const Diagnostic &prior) {
      return prior.severity == diagnostic.severity &&
             prior.where == diagnostic.where &&
             prior.message == diagnostic.message;
    });
    if (!present)
      out.push_back(std::move(diagnostic));
  }
  return out;
}

std::vector<Diagnostic> CheckSourceInputs(const RowTypes &a_rows,
                                          const Source &a_source) {
  const auto where = SourceWhere(a_source.name);
  std::vector<Diagnostic> out;
  const Reporter report{out, where};
  Match(
      a_source.kind,
      [&](const ImageSource &s) {
        if (s.path.empty()) {
          report.Error("'path' is empty");
        }
        if (s.scroll) {
          CheckVector<2>(a_rows, report, *s.scroll, "scroll", false);
        }
        if (s.tile) {
          CheckVector<2>(a_rows, report, *s.tile, "tile", false);
        }
      },
      [&](const BakeSource &s) {
        if (const auto *bw = Get<BoneWeightBake>(s.bake);
            bw && bw->bones.empty()) {
          report.Error("boneWeight needs at least one bone");
        }
      },
      [&](const DistanceSource &s) {
        if (s.from.empty()) {
          report.Error("'distance' names a skeleton node");
        }
      },
      [&](const RippleSource &s) {
        CheckTrigger(a_rows, report, s.trigger, "trigger");
        CheckScalar(a_rows, report, s.speed, "speed");
        CheckScalar(a_rows, report, s.width, "width");
        CheckScalar(a_rows, report, s.decay, "decay");
        CheckVector<3>(a_rows, report, s.direction, "direction", false);
      },
      [&](const MaterialClustersSource &s) {
        if (s.settings.clusters < 1 ||
            s.settings.clusters > kMaxMaterialClusters) {
          report.Error(
              std::format("'clusters' is 1..{}", kMaxMaterialClusters));
        }
        if (s.settings.iterations < 1 ||
            s.settings.iterations > kMaxClusterIterations) {
          report.Error(
              std::format("'iterations' is 1..{}", kMaxClusterIterations));
        }
        for (const ClusterWeightField &field : kClusterWeightFields) {
          const float weight = s.settings.weights.*field.member;
          if (weight < 0.0f || weight > kMaxChannelWeight) {
            report.Error(std::format("'weights.{}' is 0..{}", field.name,
                                     kMaxChannelWeight));
          }
        }
      },
      [](const MaterialSource &) {});
  return out;
}

std::vector<Diagnostic> CheckMask(const RowTypes &a_rows, const Mask &a_mask) {
  return CandidateDiagnostics(a_rows, a_mask, &Recipe::masks,
                              MaskWhere(a_mask.name));
}

std::vector<Diagnostic> CheckLayer(const RowTypes &a_rows, const Layer &a_layer,
                                   Slot a_slot, std::string_view a_where) {
  std::vector<Diagnostic> out = LayerDiagnostics(a_rows, a_layer, a_where);
  const Reporter report{out, a_where};
  if (const auto *ref = Get<Ref>(a_layer.source)) {
    if (!a_rows.recipe.FindSource(ref->name) &&
        !a_rows.recipe.FindMask(ref->name)) {
      report.Error(std::format("'source' names unknown source or mask '@{}'",
                               ref->name));
    }
  }
  CheckScalar(a_rows, report, a_layer.opacity, "opacity");
  if (a_layer.color) {
    CheckVector<3>(a_rows, report, *a_layer.color, "color", true);
  }
  if (a_layer.mask && !a_rows.recipe.FindMask(a_layer.mask->name)) {
    report.Error(
        std::format("'mask' names unknown mask '@{}'", a_layer.mask->name));
  }
  if (!BlendAllowed(a_slot, a_layer.blend)) {
    report.Error(std::format("blend '{}' is valid only on the normal stack",
                             NameOf(kBlends, a_layer.blend)));
  }
  return out;
}

namespace {
void CheckSurfaceScalars(const RowTypes &a_rows, const SurfaceOutput &a_m,
                         const Reporter &a_report) {
  for (const auto field : ScalarsOf(a_m.slot)) {
    const auto name = NameOf(kScalarFields, field);
    if (field == ScalarField::kColor) {
      if (a_m.scalars.color) {
        CheckVector<3>(a_rows, a_report, *a_m.scalars.color, "color", true);
      } else if (ScalarRequired(a_m.slot, field)) {
        a_report.Error(std::format("slot '{}' needs '{}'",
                                   NameOf(kSlots, a_m.slot), name));
      }
      continue;
    }
    const std::optional<Param> *param = ScalarOf(a_m.scalars, field);
    if (!param) {
      continue;
    }
    if (*param) {
      CheckScalar(a_rows, a_report, **param, name);
    } else if (ScalarRequired(a_m.slot, field)) {
      a_report.Error(
          std::format("slot '{}' needs '{}'", NameOf(kSlots, a_m.slot), name));
    }
  }
}

std::vector<Diagnostic> CheckSurfaceOutput(const RowTypes &a_rows,
                                           const SurfaceOutput &a_m,
                                           std::string_view a_where) {
  std::vector<Diagnostic> out;
  const Reporter report{out, a_where};
  CheckSurfaceScalars(a_rows, a_m, report);
  if (!SlotWritable(a_m.surface, a_rows.recipe.shell.material, a_m.slot)) {
    report.Error(std::format(
        "a vanilla shell has only the emissive slot; '{}' is not one",
        NameOf(kSlots, a_m.slot)));
  }
  std::size_t li = 0;
  for (const auto &l : a_m.stack) {
    const auto layerWhere = std::format("{} layer {}", a_where, li++);
    for (auto &d : CheckLayer(a_rows, l, a_m.slot, layerWhere)) {
      out.push_back(std::move(d));
    }
  }
  return out;
}

std::vector<Diagnostic> CheckLightOutput(const RowTypes &a_rows,
                                         const LightOutput &a_l,
                                         std::string_view a_where) {
  std::vector<Diagnostic> out;
  const Reporter report{out, a_where};
  CheckVector<3>(a_rows, report, a_l.offset, "offset", false);
  CheckVector<3>(a_rows, report, a_l.color, "color", true);
  CheckScalar(a_rows, report, a_l.intensity, "intensity");
  CheckScalar(a_rows, report, a_l.size, "size");
  CheckScalar(a_rows, report, a_l.cutoff, "cutoff");
  if (const auto *named = Get<NamedBones>(a_l.bones);
      named && named->bones.empty()) {
    report.Error("'named' needs at least one bone");
  }
  if (const auto *skinned = Get<SkinnedBones>(a_l.bones);
      skinned && skinned->max == 0) {
    report.Error("'skinned.max' must be at least 1");
  }
  return out;
}
}

std::vector<Diagnostic> CheckOutput(const RowTypes &a_rows,
                                    const Output &a_output,
                                    std::string_view a_where) {
  return Match(
      a_output,
      [&](const SurfaceOutput &m) {
        return CheckSurfaceOutput(a_rows, m, a_where);
      },
      [&](const LightOutput &l) {
        return CheckLightOutput(a_rows, l, a_where);
      });
}

namespace {
void CheckSlotExclusions(std::string_view a_where, Slot a_slot,
                         const std::vector<Slot> &a_held,
                         std::vector<Diagnostic> &a_out) {
  const Reporter report{a_out, a_where};
  for (const auto other : a_held) {
    if (SlotsExclude(other, a_slot)) {
      report.Warn(std::format("'{}' and '{}' on the same material exclude "
                              "each other; this output is dropped",
                              SlotName(other), SlotName(a_slot)));
    }
  }
}

void CheckOutputs(const RowTypes &a_rows, std::vector<Diagnostic> &a_out) {
  std::map<Surface, std::vector<Slot>> bound;
  std::size_t index = 0;
  for (const auto &o : a_rows.recipe.outputs) {
    const auto where = OutputWhere(index++);
    for (auto &d : CheckOutput(a_rows, o, where)) {
      a_out.push_back(std::move(d));
    }
    const auto *m = Get<SurfaceOutput>(o);
    if (!m) {
      continue;
    }
    CheckSlotExclusions(where, m->slot, bound[m->surface], a_out);
    bound[m->surface].push_back(m->slot);
  }
}

void CheckShell(const RowTypes &a_rows, std::vector<Diagnostic> &a_out) {
  const ShellSettings &s = a_rows.recipe.shell;
  const Reporter shell{a_out, "shell"};
  CheckScalar(a_rows, shell, s.opacity, "alpha");
  CheckScalar(a_rows, shell, s.rimPower, "rimPower");
  CheckScalar(a_rows, shell, s.emissive, "emissive");
  const Reporter pose{a_out, "shell pose"};
  CheckVector<3>(a_rows, pose, s.pose.inflate, "inflate", false);
  CheckVector<3>(a_rows, pose, s.pose.offset, "offset", false);
  CheckScalar(a_rows, pose, s.pose.scale, "scale");
  CheckScalar(a_rows, pose, s.pose.spin, "spin");
  if (s.pose.spinAxis == Vec3{0.0f, 0.0f, 0.0f}) {
    pose.Error("spinAxis must not be zero");
  }
}

void CheckVariants(const RowTypes &a_rows, std::vector<Diagnostic> &a_out) {
  for (const auto &v : a_rows.recipe.variants) {
    const Reporter report{a_out, VariantWhere(v.name)};
    for (const auto &[name, value] : v.overrides) {
      const auto type = SignalTypeOf(a_rows, name);
      if (!type) {
        report.Error(std::format("overrides unknown signal '{}'", name));
      } else if (*type != TypeOf(value)) {
        report.Error(std::format("override of '{}' is a {}; the signal is a {}",
                                 name, Name(TypeOf(value)), Name(*type)));
      }
    }
  }
}
}

std::vector<Diagnostic>
Validate(const Recipe &a_recipe,
         std::span<const Diagnostic> a_inputDiagnostics) {
  const RecipeGraph graph = RecipeGraph::Compile(a_recipe);
  const RowTypes rows{a_recipe, graph};

  std::vector<Diagnostic> out = CheckRecipeFields(a_recipe);
  out.insert(out.begin(), a_inputDiagnostics.begin(), a_inputDiagnostics.end());
  if (!a_inputDiagnostics.empty()) {
    Reporter{out, "file"}.Warn(
        "Loaded-file diagnostics remain until a successful save or reload.");
  }
  const auto append = [&](std::vector<Diagnostic> a_more) {
    for (auto &d : a_more) {
      const bool reported =
          std::ranges::any_of(out, [&](const Diagnostic &prior) {
            return prior.severity == d.severity && prior.where == d.where &&
                   prior.message == d.message;
          });
      if (!reported) {
        out.push_back(std::move(d));
      }
    }
  };

  for (const auto &d : graph.Diagnostics()) {
    out.push_back(d);
  }
  std::unordered_set<std::string> variants;
  for (const auto &variant : a_recipe.variants) {
    if (variant.name.empty()) {
      Reporter{out, "variant"}.Error("has no name");
    } else if (!variants.insert(variant.name).second) {
      Reporter{out, VariantWhere(variant.name)}.Error("duplicate name");
    }
  }
  for (const auto &c : a_recipe.curves) {
    append(CheckCurve(rows, c));
  }
  for (const auto &s : a_recipe.sources) {
    append(CheckSource(rows, s));
  }
  for (const auto &m : a_recipe.masks) {
    append(CheckMask(rows, m));
  }
  std::vector<Diagnostic> outputDiagnostics;
  CheckOutputs(rows, outputDiagnostics);
  append(std::move(outputDiagnostics));
  CheckShell(rows, out);
  CheckVariants(rows, out);
  return out;
}

template <class State> State *SignalState::Memory(NodeId a_node) noexcept {
  if (a_node >= stateSlots_.size() || !stateSlots_[a_node])
    return nullptr;
  const auto slot = *stateSlots_[a_node];
  return slot < states_.size() ? Get<State>(states_[slot]) : nullptr;
}

template <class State>
const State *SignalState::Memory(NodeId a_node) const noexcept {
  if (a_node >= stateSlots_.size() || !stateSlots_[a_node])
    return nullptr;
  const auto slot = *stateSlots_[a_node];
  return slot < states_.size() ? Get<State>(states_[slot]) : nullptr;
}

SignalState::SignalState(const RecipeGraph &a_graph)
    : graph_(a_graph), stateSlots_(a_graph.Size()) {
  outputOffsets_.reserve(a_graph.Size());
  for (NodeId id = 0; id < graph_.Size(); ++id) {
    const auto *node = graph_.NodeAt(id);
    outputOffsets_.push_back(values_.size());
    if (!node)
      continue;
    for (const auto &output : node->outputs) {
      const auto *type = Get<ValueType>(output.type);
      values_.push_back(type ? ZeroOf(*type) : Value{0.0f});
    }
    if (graph_.IsDisabled(id))
      continue;
    const auto add = [&](OperationState state) {
      stateSlots_[id] = states_.size();
      states_.push_back(std::move(state));
    };
    Match(
        node->kind, [&](const WaveOperation &) { add(WaveState{}); },
        [&](const TriggerOperation &) {
          add(TriggerState{});
          triggers_.push_back(id);
        },
        [&](const HoldOperation &) { add(HoldState{DefaultValue(id)}); },
        [&](const CounterOperation &) { add(CounterState{}); },
        [&](const AccumulateOperation &) { add(AccumulateState{}); },
        [&](const RateOperation &) { add(RateState{}); },
        [&](const SmoothOperation &) { add(SmoothState{}); },
        [&](const ConstantOperation &constant) {
          Store({id, 0}, constant.value);
        },
        [](const auto &) {});
  }
}

Value SignalState::DefaultValue(NodeId a_node) const noexcept {
  const auto type = graph_.OutputType({a_node, 0});
  const auto *numeric = type ? Get<ValueType>(*type) : nullptr;
  return numeric ? ZeroOf(*numeric) : Value{0.0f};
}

Value SignalState::ValueOf(OutputRef a_output) const noexcept {
  const auto *node = graph_.NodeAt(a_output.node);
  if (!node || a_output.node >= outputOffsets_.size() ||
      a_output.output >= node->outputs.size() ||
      !Is<ValueType>(node->outputs[a_output.output].type))
    return 0.0f;
  const auto slot = outputOffsets_[a_output.node] + a_output.output;
  return slot < values_.size() ? values_[slot] : Value{0.0f};
}

void SignalState::Store(OutputRef a_output, Value a_value) {
  const auto *node = graph_.NodeAt(a_output.node);
  if (!node || a_output.node >= outputOffsets_.size() ||
      a_output.output >= node->outputs.size())
    return;
  const auto *type = Get<ValueType>(node->outputs[a_output.output].type);
  if (!type)
    return;
  const auto slot = outputOffsets_[a_output.node] + a_output.output;
  if (slot < values_.size())
    values_[slot] = TypeOf(a_value) == *type ? a_value : ZeroOf(*type);
}

Value SignalState::ValueOf(std::size_t a_index) const noexcept {
  return ValueOf(OutputRef{a_index, 0});
}

Value SignalState::ValueOf(std::string_view a_name) const noexcept {
  const auto output = graph_.FindSignalOutput(a_name);
  return output ? ValueOf(*output) : Value{0.0f};
}

float SignalState::Scalar(std::string_view a_name) const noexcept {
  return AsScalar(ValueOf(a_name));
}

Vec3 SignalState::Vector(std::string_view a_name) const noexcept {
  return AsVec3(ValueOf(a_name));
}

float SignalState::Resolve(const Param &a_param) const noexcept {
  return Match(
      a_param, [](float value) { return value; },
      [&](const Ref &reference) { return Scalar(reference.name); });
}

Vec2 SignalState::Resolve(const Vec2Param &a_param) const noexcept {
  return Match(
      a_param,
      [&](const Ref &reference) { return AsVec2(ValueOf(reference.name)); },
      [&](const std::array<Param, 2> &parts) {
        return Vec2{Resolve(parts[0]), Resolve(parts[1])};
      });
}

Vec3 SignalState::Resolve(const Vec3Param &a_param) const noexcept {
  return Match(
      a_param, [&](const Ref &reference) { return Vector(reference.name); },
      [&](const std::array<Param, 3> &parts) {
        return Vec3{Resolve(parts[0]), Resolve(parts[1]), Resolve(parts[2])};
      });
}

std::span<const TriggerFiring>
SignalState::Firings(OutputRef a_output) const noexcept {
  const auto type = graph_.OutputType(a_output);
  const auto *resource = type ? Get<ResourceType>(*type) : nullptr;
  const auto *memory = resource && *resource == ResourceType::kFirings
                           ? Memory<TriggerState>(a_output.node)
                           : nullptr;
  return memory ? std::span<const TriggerFiring>{memory->firings}
                : std::span<const TriggerFiring>{};
}

std::uint64_t SignalState::AcceptedCount(OutputRef a_output) const noexcept {
  const auto type = graph_.OutputType(a_output);
  const auto *resource = type ? Get<ResourceType>(*type) : nullptr;
  const auto *memory = resource && *resource == ResourceType::kCount
                           ? Memory<TriggerState>(a_output.node)
                           : nullptr;
  return memory ? memory->accepted : 0;
}

std::span<const TriggerFiring>
SignalState::Firings(std::string_view a_trigger) const noexcept {
  const auto id = graph_.FindTrigger(a_trigger);
  const auto *memory = id ? Memory<TriggerState>(*id) : nullptr;
  return memory ? std::span<const TriggerFiring>{memory->firings}
                : std::span<const TriggerFiring>{};
}

std::uint64_t
SignalState::Mismatched(std::string_view a_trigger) const noexcept {
  const auto id = graph_.FindTrigger(a_trigger);
  const auto *memory = id ? Memory<TriggerState>(*id) : nullptr;
  return memory ? memory->mismatched : 0;
}

namespace {
FiringAnchor ResolveAnchor(const TriggerAnchor &a_anchor,
                           const TriggerPayload &a_payload) noexcept {
  return Match(
      a_anchor, [](const std::monostate &) { return FiringAnchor{}; },
      [&](const WorldAnchor &) {
        const auto *position = Get<Vec3>(a_payload.value);
        return position ? FiringAnchor{CarriedPoint{*position}}
                        : FiringAnchor{};
      },
      [&](const NodeAnchor &anchor) {
        const std::string_view node =
            a_payload.node.empty() ? anchor.node : a_payload.node;
        return node.empty() ? FiringAnchor{} : FiringAnchor{AnchorNode{node}};
      });
}
}

FiringAnchor AnchorOf(const TriggerSignal &a_trigger,
                      const TriggerPayload &a_payload) noexcept {
  return ResolveAnchor(a_trigger.anchor, a_payload);
}

FiringAnchor
SignalState::AnchorOf(std::string_view a_trigger,
                      const TriggerFiring &a_firing) const noexcept {
  const auto id = graph_.FindTrigger(a_trigger);
  const auto *node = id ? graph_.NodeAt(*id) : nullptr;
  const auto *trigger = node ? Get<TriggerOperation>(node->kind) : nullptr;
  return trigger ? ResolveAnchor(trigger->anchor, a_firing.payload)
                 : FiringAnchor{};
}

void SignalState::TriggerState::RecordFiring(TriggerFiring a_firing,
                                             std::uint32_t a_limit) {
  firings.push_back(std::move(a_firing));
  ++accepted;
  const std::size_t keep = std::max<std::uint32_t>(1, a_limit);
  if (firings.size() > keep)
    firings.erase(firings.begin(), firings.end() - keep);
}

void SignalState::Accept(NodeId a_node, const EventRecord &a_event,
                         float a_time) {
  const auto *node = graph_.NodeAt(a_node);
  const auto *trigger = node ? Get<TriggerOperation>(node->kind) : nullptr;
  auto *memory = Memory<TriggerState>(a_node);
  if (!trigger || !memory || graph_.IsDisabled(a_node))
    return;
  const auto *input = Get<EventTriggerInput>(trigger->origin);
  const auto *eventNode = input ? graph_.NodeAt(input->events.node) : nullptr;
  const auto *external =
      eventNode ? Get<ExternalInput>(eventNode->kind) : nullptr;
  const auto *events = external ? Get<EventInput>(external->source) : nullptr;
  if (!events || graph_.IsDisabled(input->events.node))
    return;
  const bool accepted = Match(
      events->origin,
      [&](const EventOrigin &origin) {
        return !a_event.plugin && GlobMatch(origin.event, a_event.id) &&
               MatchesFilter(origin.filter, a_event.payload);
      },
      [&](const PluginOrigin &origin) {
        return a_event.plugin && GlobMatch(origin.id, a_event.id);
      });
  if (!accepted)
    return;
  if (TypeOf(a_event.payload.value) != trigger->payloadType) {
    ++memory->mismatched;
    return;
  }
  memory->RecordFiring({a_time, a_event.payload}, trigger->max);
}

void SignalState::Fire(const EventRecord &a_event, float a_time) {
  for (const auto trigger : triggers_)
    Accept(trigger, a_event, a_time);
}

struct SignalState::Evaluator {
  SignalState &state;
  NodeId index;
  const SignalEnvironment &environment;
  const TickInputs &inputs;

  [[nodiscard]] Value Read(OutputRef output) const {
    return state.ValueOf(output);
  }
  [[nodiscard]] float Scalar(OutputRef output) const {
    return AsScalar(Read(output));
  }
  [[nodiscard]] Value Default() const { return state.DefaultValue(index); }

  Value operator()(const ConstantOperation &operation) const {
    return operation.value;
  }
  Value operator()(const ExternalInput &operation) const {
    return Match(
        operation.source,
        [&](const TimeInput &) -> Value { return inputs.time; },
        [&](const DeltaTimeInput &) -> Value {
          return std::max(0.0f, inputs.delta);
        },
        [&](const ActorValueSignal &input) -> Value {
          return environment.ActorValue(input.actorValue, input.measure);
        },
        [&](const ActorStateSignal &input) -> Value {
          return VectorValued(input.kind)
                     ? Value{environment.ActorVector(input.kind)}
                     : Value{environment.ActorState(input.kind)};
        },
        [&](const EnchantmentSignal &input) -> Value {
          return environment.Enchantment(input.field);
        },
        [&](const auto &) { return Default(); });
  }
  Value operator()(const VectorOperation &operation) const {
    if (operation.components.size() == 2)
      return Vec2{Scalar(operation.components[0]),
                  Scalar(operation.components[1])};
    if (operation.components.size() == 3)
      return Vec3{Scalar(operation.components[0]),
                  Scalar(operation.components[1]),
                  Scalar(operation.components[2])};
    return Default();
  }
  Value operator()(const ExpressionOperation &operation) const {
    const auto &expression = operation.expression;
    if (expression.valueBindings.size() > kMaxExpressionOps)
      return Default();
    std::array<Value, kMaxExpressionOps> values;
    for (std::size_t i = 0; i < expression.valueBindings.size(); ++i)
      values[i] = Read(expression.valueBindings[i]);
    Program::Inputs arguments;
    arguments.refs = std::span{values.data(), expression.valueBindings.size()};
    arguments.callFunction = [&](std::size_t binding, float x, float) {
      if (binding >= expression.functionBindings.size())
        return 0.0f;
      const auto &call = expression.functionBindings[binding];
      const auto *function = state.graph_.FunctionAt(call.function);
      if (!function || function->parameters.size() > kMaxExpressionOps ||
          call.sampledParameter >= function->parameters.size())
        return 0.0f;
      std::vector<Value> parameters(function->parameters.size(), Value{0.0f});
      parameters[call.sampledParameter] = x;
      for (const auto &argument : call.arguments) {
        if (argument.parameter >= parameters.size())
          return 0.0f;
        parameters[argument.parameter] = Read(argument.value);
      }
      return AsScalar(
          EvaluateFunction(state.graph_, call.function, parameters));
    };
    return expression.program.Evaluate(arguments);
  }
  Value operator()(const CallOperation &operation) const {
    if (operation.arguments.size() > kMaxExpressionOps)
      return Default();
    std::array<Value, kMaxExpressionOps> arguments;
    for (std::size_t i = 0; i < operation.arguments.size(); ++i)
      arguments[i] = Read(operation.arguments[i]);
    return EvaluateFunction(
        state.graph_, operation.function,
        std::span{arguments.data(), operation.arguments.size()});
  }
  Value operator()(const MapFunctionOperation &operation) const {
    return (*this)(CallOperation{operation.function, operation.arguments});
  }

  Value operator()(const WaveOperation &operation) const {
    auto *memory = state.Memory<WaveState>(index);
    if (!memory)
      return Default();
    const float period = Scalar(operation.period);
    if (period > kEpsilon) {
      memory->phase += Scalar(operation.deltaTime) / period;
      memory->phase -= std::floor(memory->phase);
    }
    return Scalar(operation.base) +
           Scalar(operation.amplitude) *
               Wave(operation.waveform,
                    memory->phase + Scalar(operation.phase));
  }
  Value operator()(const RampOperation &operation) const {
    const float seconds = Scalar(operation.seconds);
    const float progress =
        seconds <= kEpsilon ? 1.0f : Clamp01(Scalar(operation.time) / seconds);
    const float from = Scalar(operation.from);
    return from + (Scalar(operation.to) - from) * progress;
  }
  Value operator()(const EffectShaderOperation &operation) const {
    const auto *node = state.graph_.NodeAt(operation.record.node);
    const auto *external = node ? Get<ExternalInput>(node->kind) : nullptr;
    const auto *record =
        external ? Get<EffectShaderInput>(external->source) : nullptr;
    const auto params =
        record ? environment.EffectShader(record->record) : std::nullopt;
    if (!params)
      return Default();
    const auto fill =
        Efsh::Evaluate(*params, Scalar(operation.time), 1.0f, 1.0f);
    switch (operation.field) {
    case EfshField::kFillAlpha:
      return fill.alpha;
    case EfshField::kFillColor:
      return Vec3{fill.color.x * fill.scale, fill.color.y * fill.scale,
                  fill.color.z * fill.scale};
    case EfshField::kEdgeAlpha:
      return fill.edgeAlpha;
    case EfshField::kEdgeColor:
      return fill.edgeColor;
    case EfshField::kScroll:
      return Vec2{fill.uOffset, fill.vOffset};
    }
    return Default();
  }
  Value operator()(const NoiseOperation &operation) const {
    return Scalar(operation.amplitude) *
           ValueNoise(Scalar(operation.time) * Scalar(operation.frequency),
                      operation.seed);
  }
  Value operator()(const GradientOperation &operation) const {
    if (operation.stops.empty())
      return Vec3{};
    const float position = Scalar(operation.position);
    const auto *lo = &operation.stops.front();
    const auto *hi = &operation.stops.back();
    for (const auto &stop : operation.stops) {
      if (stop.at <= position && stop.at >= lo->at)
        lo = &stop;
      if (stop.at >= position && stop.at <= hi->at)
        hi = &stop;
    }
    if (position <= operation.stops.front().at)
      return Read(operation.stops.front().color);
    if (position >= operation.stops.back().at)
      return Read(operation.stops.back().color);
    const float span = hi->at - lo->at;
    return Lerp(AsVec3(Read(lo->color)), AsVec3(Read(hi->color)),
                span <= kEpsilon ? 0.0f : (position - lo->at) / span);
  }
  Value operator()(const ToRootOperation &operation) const {
    const auto type = state.graph_.OutputType(operation.transform);
    const auto *resource = type ? Get<ResourceType>(*type) : nullptr;
    return resource && *resource == ResourceType::kTransform
               ? Value{environment.WorldToRoot(AsVec3(Read(operation.value)))}
               : Default();
  }
  Value operator()(const TriggerOperation &operation) const {
    auto *memory = state.Memory<TriggerState>(index);
    if (!memory)
      return Default();
    const float time = Scalar(operation.time);
    if (const auto *condition = Get<ConditionTriggerInput>(operation.origin)) {
      const float now = Scalar(condition->condition);
      const float before = memory->previousCondition;
      memory->previousCondition = now;
      if (before <= 0.0f && now > 0.0f) {
        TriggerFiring firing{time, {}};
        firing.payload.value = condition->payload
                                   ? Read(*condition->payload)
                                   : ZeroOf(operation.payloadType);
        if (TypeOf(firing.payload.value) == operation.payloadType)
          memory->RecordFiring(std::move(firing), operation.max);
        else
          ++memory->mismatched;
      }
    }
    const float lifetime = std::max(kEpsilon, Scalar(operation.lifetime));
    std::erase_if(memory->firings, [&](const TriggerFiring &firing) {
      return time - firing.startTime >= lifetime;
    });
    return memory->firings.empty()
               ? 1.0f
               : Clamp01((time - memory->firings.back().startTime) / lifetime);
  }
  Value operator()(const HoldOperation &operation) const {
    auto *memory = state.Memory<HoldState>(index);
    if (!memory)
      return Default();
    const auto firings = state.Firings(operation.firings);
    if (!firings.empty())
      memory->held = firings.back().payload.value;
    return memory->held;
  }
  Value operator()(const CounterOperation &operation) const {
    auto *memory = state.Memory<CounterState>(index);
    if (!memory)
      return Default();
    const auto reset =
        operation.reset ? state.AcceptedCount(*operation.reset) : 0;
    if (reset > memory->seenReset) {
      memory->seenReset = reset;
      memory->value = 0.0f;
    }
    const auto count = state.AcceptedCount(operation.count);
    if (count >= memory->seen)
      memory->value += static_cast<float>(count - memory->seen);
    memory->seen = count;
    const float cap = operation.cap ? Scalar(*operation.cap) : 0.0f;
    if (cap > 0.0f)
      memory->value = std::min(memory->value, cap);
    return memory->value;
  }
  Value operator()(const AccumulateOperation &operation) const {
    auto *memory = state.Memory<AccumulateState>(index);
    if (!memory)
      return Default();
    memory->value =
        std::max(0.0f, memory->value - Scalar(operation.decay) *
                                           Scalar(operation.deltaTime));
    const auto count = state.AcceptedCount(operation.count);
    if (count >= memory->seen)
      memory->value += static_cast<float>(count - memory->seen);
    memory->seen = count;
    return memory->value;
  }
  Value operator()(const RateOperation &operation) const {
    auto *memory = state.Memory<RateState>(index);
    if (!memory)
      return Default();
    const Value now = Read(operation.value);
    const Value before = memory->previous.value_or(now);
    memory->previous = now;
    const float delta = Scalar(operation.deltaTime);
    if (delta <= kEpsilon)
      return Default();
    return Match(
        now,
        [&](float value) -> Value {
          return (value - AsScalar(before)) / delta;
        },
        [&](const Vec2 &value) -> Value {
          const auto prior = AsVec2(before);
          return Vec2{(value.x - prior.x) / delta, (value.y - prior.y) / delta};
        },
        [&](const Vec3 &value) -> Value {
          const auto prior = AsVec3(before);
          return Vec3{(value.x - prior.x) / delta, (value.y - prior.y) / delta,
                      (value.z - prior.z) / delta};
        });
  }
  Value operator()(const SmoothOperation &operation) const {
    auto *memory = state.Memory<SmoothState>(index);
    if (!memory)
      return Default();
    const Value target = Read(operation.value);
    if (!memory->previous) {
      memory->previous = target;
      return target;
    }
    const float seconds = Scalar(operation.seconds);
    const float factor =
        seconds <= kEpsilon
            ? 1.0f
            : 1.0f - std::exp(-Scalar(operation.deltaTime) / seconds);
    const Value next = Match(
        target,
        [&](float value) -> Value {
          return AsScalar(*memory->previous) +
                 (value - AsScalar(*memory->previous)) * factor;
        },
        [&](const Vec2 &value) -> Value {
          const auto prior = AsVec2(*memory->previous);
          return Vec2{prior.x + (value.x - prior.x) * factor,
                      prior.y + (value.y - prior.y) * factor};
        },
        [&](const Vec3 &value) -> Value {
          return Lerp(AsVec3(*memory->previous), value, factor);
        });
    memory->previous = next;
    return next;
  }
  template <class Operation> Value operator()(const Operation &) const {
    return Default();
  }
};

Value SignalState::Evaluate(NodeId a_node,
                            const SignalEnvironment &a_environment,
                            const TickInputs &a_inputs) {
  const auto *node = graph_.NodeAt(a_node);
  return node ? Match(node->kind,
                      Evaluator{*this, a_node, a_environment, a_inputs})
              : Value{0.0f};
}

void SignalState::Tick(const SignalEnvironment &a_environment,
                       const TickInputs &a_inputs) {
  for (const auto id :
       ticked_ ? graph_.ChangingTickOrder() : graph_.TickOrder()) {
    if (!graph_.IsDisabled(id))
      Store({id, 0}, Evaluate(id, a_environment, a_inputs));
  }
  ticked_ = true;
}
}
