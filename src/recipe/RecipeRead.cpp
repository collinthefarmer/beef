#include "recipe/Binders.h"
#include "recipe/Recipe.h"
#include "recipe/Words.h"

#include <algorithm>
#include <array>
#include <format>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
namespace {
std::optional<FormRef> FormFrom(const json &a_j, const Reporter &a_ctx,
                                std::string_view a_what) {
  if (!a_j.is_string() || a_j.get<std::string>().empty()) {
    a_ctx.Error(std::format("'{}' must be an editor ID or \"0x<id>~<plugin>\"",
                            a_what));
    return std::nullopt;
  }
  return FormRef::From(a_j.get<std::string>());
}

std::optional<std::string> GlobFrom(const json &a_j, const Reporter &a_ctx,
                                    std::string_view a_what) {
  if (!a_j.is_string() || a_j.get<std::string>().empty()) {
    a_ctx.Error(std::format("'{}' must be a glob string", a_what));
    return std::nullopt;
  }
  return a_j.get<std::string>();
}

std::optional<RecipeKey> KeyFrom(const json &a_j, const Reporter &a_ctx) {
  if (a_j.is_string()) {
    if (a_j.get<std::string>() == "default") {
      return RecipeKey{KeyKind::kDefault};
    }
    a_ctx.Error(
        std::format("a key is \"default\" or {{\"<kind>\": ...}}; got '{}'",
                    a_j.get<std::string>()));
    return std::nullopt;
  }
  const auto entry = OneKey(a_j, a_ctx, "a key");
  if (!entry) {
    return std::nullopt;
  }
  const auto kind = FromName(kKeyKinds, entry->key);
  const auto *row = kind ? RowOf(kKeyKinds, *kind) : nullptr;
  if (!row || row->operand == KeyOperand::kNone) {
    a_ctx.Error(std::format("unknown key kind '{}'; one of {}", entry->key,
                            Choices(kKeyKinds)));
    return std::nullopt;
  }
  RecipeKey key;
  key.kind = row->value;
  if (row->operand == KeyOperand::kGlob) {
    const auto glob = GlobFrom(*entry->value, a_ctx, entry->key);
    if (!glob) {
      return std::nullopt;
    }
    key.operand = *glob;
    return key;
  }
  const auto form = FormFrom(*entry->value, a_ctx, entry->key);
  if (!form) {
    return std::nullopt;
  }
  key.operand = *form;
  return key;
}

Selector SelectorFrom(const json &a_j, const Reporter &a_ctx) {
  Selector s;
  if (!a_j.is_array()) {
    a_ctx.Error("'selector' must be an array of {\"kind\": ...} terms");
    return s;
  }
  for (const auto &e : a_j) {
    if (RowCapReached(s.anyOf.size(), a_ctx, "selector")) {
      break;
    }
    const auto entry = OneKey(e, a_ctx, "a selector term");
    if (!entry) {
      continue;
    }
    const auto kind = FromName(kSelectorKinds, entry->key);
    if (!kind) {
      a_ctx.Error(std::format("unknown selector kind '{}'; one of {}",
                              entry->key, Choices(kSelectorKinds)));
      continue;
    }
    SelectorClause term;
    term.kind = *kind;
    if (*kind == SelectorKind::kAddon) {
      const auto form = FormFrom(*entry->value, a_ctx, entry->key);
      if (!form) {
        continue;
      }
      term.operand = *form;
    } else {
      const auto glob = GlobFrom(*entry->value, a_ctx, entry->key);
      if (!glob) {
        continue;
      }
      term.operand = *glob;
    }
    s.anyOf.push_back(std::move(term));
  }
  return s;
}

std::optional<CurveRef> CurveRefFrom(Reader &a_reader) {
  const auto text = a_reader.String("curve");
  if (!text) {
    return std::nullopt;
  }
  if (text->empty()) {
    a_reader.Context().Error("'curve' is empty");
    return std::nullopt;
  }
  return CurveRef{*text};
}

std::optional<SignalKind> ParseConstant(const json &a_v,
                                        const Reporter &a_ctx) {
  const auto value = Reader::ValueFrom(a_v, "constant", a_ctx);
  if (!value) {
    return std::nullopt;
  }
  return ConstantSignal{*value};
}

std::optional<SignalKind> ParsePulse(const json &a_v, const Reporter &a_ctx) {
  PulseSignal k;
  if (!ReadObject(a_v, "pulse", a_ctx, [&](Reader &r) {
        r.Read("base", k.base);
        r.Read("amplitude", k.amplitude);
        r.Read("period", k.period);
        r.Read("phase", k.phase);
        r.Read("waveform", kWaveforms, k.waveform);
      })) {
    return std::nullopt;
  }
  return k;
}

std::optional<SignalKind> ParseRamp(const json &a_v, const Reporter &a_ctx) {
  RampSignal k;
  if (!ReadObject(a_v, "ramp", a_ctx, [&](Reader &r) {
        r.Read("from", k.from);
        r.Read("to", k.to);
        r.Read("seconds", k.seconds);
      })) {
    return std::nullopt;
  }
  return k;
}

std::optional<SignalKind> ParseEfsh(const json &a_v, const Reporter &a_ctx) {
  EfshSignal k;
  if (!ReadObject(a_v, "efsh", a_ctx, [&](Reader &r) {
        r.Read("field", kEfshFields, k.field);
        if (!r.Has("field"))
          a_ctx.Error("'efsh' needs 'field'");
        if (const auto *rec = r.Child("record")) {
          if (auto form = FormFrom(*rec, a_ctx, "record"))
            k.record = *form;
        } else {
          a_ctx.Error("'efsh' needs 'record'");
        }
      })) {
    return std::nullopt;
  }
  return k;
}

std::optional<SignalKind> ParseActorValue(const json &a_v,
                                          const Reporter &a_ctx) {
  ActorValueSignal k;
  if (a_v.is_string()) {
    k.actorValue = a_v.get<std::string>();
  } else if (!ReadObject(a_v, "av", a_ctx, [&](Reader &r) {
               k.actorValue = r.Required("of");
               r.Read("measure", kMeasures, k.measure);
             })) {
    return std::nullopt;
  }
  if (k.actorValue.empty()) {
    a_ctx.Error("'av' needs an actor value name");
  }
  return k;
}

std::optional<SignalKind> ParseActorState(const json &a_v,
                                          const Reporter &a_ctx) {
  const auto state = EnumShorthand(a_v, kActorStates, "actorState", a_ctx);
  if (!state) {
    return std::nullopt;
  }
  return ActorStateSignal{*state};
}

std::optional<SignalKind> ParseEnchantment(const json &a_v,
                                           const Reporter &a_ctx) {
  const auto field =
      EnumShorthand(a_v, kEnchantmentFields, "enchantment", a_ctx);
  if (!field) {
    return std::nullopt;
  }
  return EnchantmentSignal{*field};
}

EventFilter FilterFrom(const json &a_f, const Reporter &a_ctx) {
  EventFilter filter;
  Reader fr(a_f, a_ctx);
  if (auto n = fr.String("node"))
    filter.node = *n;
  if (auto arg = fr.String("arg"))
    filter.arg = *arg;
  if (const auto *range = fr.Child("value")) {
    if (!range->is_array() || range->size() != 2) {
      a_ctx.Error("'filter.value' is [min, max], either may be null");
    } else {
      if ((*range)[0].is_number())
        filter.value.min = (*range)[0].get<float>();
      if ((*range)[1].is_number())
        filter.value.max = (*range)[1].get<float>();
    }
  }
  fr.Finish();
  return filter;
}

std::optional<TriggerOrigin> EventOriginFrom(const json &a_source, Reader &a_r,
                                             const Reporter &a_ctx) {
  if (!a_source.is_string() || a_source.get<std::string>().empty()) {
    a_ctx.Error("'event' is an id glob string");
    return std::nullopt;
  }
  EventOrigin es;
  es.event = a_source.get<std::string>();
  if (auto at = a_r.String("at"))
    es.at = *at;
  if (const auto *f = a_r.Child("filter"))
    es.filter = FilterFrom(*f, a_ctx);
  if (a_r.Has("value"))
    a_ctx.Error("'value' belongs to a 'when' trigger");
  return TriggerOrigin{es};
}

std::optional<TriggerOrigin> PluginOriginFrom(const json &a_source, Reader &a_r,
                                              const Reporter &a_ctx) {
  if (!a_source.is_string() || a_source.get<std::string>().empty()) {
    a_ctx.Error("'plugin' is an id string");
    return std::nullopt;
  }
  if (a_r.Has("filter") || a_r.Has("at") || a_r.Has("value"))
    a_ctx.Error(
        "'filter', 'at' and 'value' belong to 'event' or 'when' triggers");
  return TriggerOrigin{PluginOrigin{a_source.get<std::string>()}};
}

std::optional<TriggerOrigin> WhenOriginFrom(const json &a_source, Reader &a_r,
                                            const Reporter &a_ctx) {
  const auto when = a_r.RefFrom(a_source, "when");
  if (!when) {
    return std::nullopt;
  }
  WhenOrigin ws;
  ws.when = *when;
  ws.value = a_r.Reference("value");
  if (a_r.Has("filter") || a_r.Has("at"))
    a_ctx.Error("'filter' and 'at' belong to 'event' triggers");
  return TriggerOrigin{ws};
}

std::optional<SignalKind> ParseTrigger(const json &a_v, const Reporter &a_ctx) {
  if (!a_v.is_object()) {
    a_ctx.Error("'trigger' takes an object");
    return std::nullopt;
  }
  const auto source = OneKey(a_v, a_ctx, "a trigger",
                             {"lifetime", "max", "filter", "at", "value"});
  if (!source) {
    return std::nullopt;
  }
  TriggerSignal k;
  Reader r(a_v, a_ctx);
  r.Read("lifetime", k.lifetime);
  if (auto m = r.Integer("max")) {
    if (*m < 1)
      a_ctx.Error("'max' must be at least 1");
    else
      k.max = static_cast<std::uint32_t>(*m);
  }
  r.Child(source->key);
  std::optional<TriggerOrigin> origin;
  if (source->key == "event") {
    origin = EventOriginFrom(*source->value, r, a_ctx);
  } else if (source->key == "plugin") {
    origin = PluginOriginFrom(*source->value, r, a_ctx);
  } else if (source->key == "when") {
    origin = WhenOriginFrom(*source->value, r, a_ctx);
  } else {
    a_ctx.Error(std::format(
        "a trigger's source is 'event', 'plugin' or 'when', not '{}'",
        source->key));
    return std::nullopt;
  }
  if (!origin) {
    return std::nullopt;
  }
  k.origin = *origin;
  r.Finish();
  return k;
}

std::optional<SignalKind> ParsePayload(const json &a_v, const Reporter &a_ctx) {
  PayloadSignal k;
  if (!ReadObject(a_v, "payload", a_ctx, [&](Reader &r) {
        r.Read("trigger", k.trigger);
        if (!r.Has("trigger"))
          a_ctx.Error("'payload' needs 'trigger'");
        r.Read("field", kPayloadFields, k.field);
        if (!r.Has("field"))
          a_ctx.Error("'payload' needs 'field'");
      })) {
    return std::nullopt;
  }
  return k;
}

std::optional<SignalKind> ParseCounter(const json &a_v, const Reporter &a_ctx) {
  CounterSignal k;
  if (!ReadObject(a_v, "counter", a_ctx, [&](Reader &r) {
        r.Read("trigger", k.trigger);
        if (!r.Has("trigger"))
          a_ctx.Error("'counter' needs 'trigger'");
        r.Read("reset", k.reset);
        r.Read("cap", k.cap);
      })) {
    return std::nullopt;
  }
  return k;
}

std::optional<SignalKind> ParseAccumulate(const json &a_v,
                                          const Reporter &a_ctx) {
  AccumulateSignal k;
  if (!ReadObject(a_v, "accumulate", a_ctx, [&](Reader &r) {
        r.Read("trigger", k.trigger);
        if (!r.Has("trigger"))
          a_ctx.Error("'accumulate' needs 'trigger'");
        r.Read("decay", k.decay);
      })) {
    return std::nullopt;
  }
  return k;
}

std::optional<SignalKind> ParseNoise(const json &a_v, const Reporter &a_ctx) {
  NoiseSignal k;
  if (!ReadObject(a_v, "noise", a_ctx, [&](Reader &r) {
        r.Read("frequency", k.frequency);
        r.Read("amplitude", k.amplitude);
        if (auto seed = r.Integer("seed"))
          k.seed = static_cast<std::uint32_t>(std::max(0, *seed));
      })) {
    return std::nullopt;
  }
  return k;
}

std::optional<SignalKind> ParseGradient(const json &a_v,
                                        const Reporter &a_ctx) {
  GradientSignal k;
  if (!ReadObject(a_v, "gradient", a_ctx, [&](Reader &r) {
        r.Read("t", k.t);
        if (!r.Has("t"))
          a_ctx.Error("'gradient' needs 't'");
        const auto *stops = r.Child("stops");
        if (!stops || !stops->is_array() || stops->empty()) {
          a_ctx.Error("'gradient' needs a non-empty 'stops' array");
          return;
        }
        for (const auto &stop : *stops) {
          if (RowCapReached(k.stops.size(), a_ctx, "stops")) {
            break;
          }
          GradientStop gs;
          Reader sr(stop, a_ctx);
          if (auto at = sr.Number("at"))
            gs.at = *at;
          else if (!sr.Has("at"))
            a_ctx.Error("a stop needs 'at'");
          if (auto c = sr.Vector3("color", true))
            gs.color = *c;
          else if (!sr.Has("color"))
            a_ctx.Error("a stop needs 'color'");
          sr.Finish();
          k.stops.push_back(gs);
        }
      })) {
    return std::nullopt;
  }
  return k;
}

std::optional<SignalKind> ParseDelta(const json &a_v, const Reporter &a_ctx) {
  Reader r(a_v, a_ctx);
  const auto of = r.RefFrom(a_v, "delta");
  if (!of) {
    return std::nullopt;
  }
  return DeltaSignal{*of};
}

std::optional<SignalKind> ParseSmooth(const json &a_v, const Reporter &a_ctx) {
  SmoothSignal k;
  if (!ReadObject(a_v, "smooth", a_ctx, [&](Reader &r) {
        r.Read("of", k.of);
        if (!r.Has("of"))
          a_ctx.Error("'smooth' needs 'of'");
        r.Read("seconds", k.seconds);
      })) {
    return std::nullopt;
  }
  return k;
}

std::optional<SignalKind> ParseExpr(const json &a_v, const Reporter &a_ctx) {
  if (!a_v.is_string() || a_v.get<std::string>().empty()) {
    a_ctx.Error("'expr' is an expression string");
    return std::nullopt;
  }
  return ExprSignal{a_v.get<std::string>()};
}

using SignalParser = std::optional<SignalKind> (*)(const json &,
                                                   const Reporter &);
constexpr SignalParser kSignalParsers[]{
    &ParseConstant,   &ParsePulse,      &ParseRamp,        &ParseEfsh,
    &ParseActorValue, &ParseActorState, &ParseEnchantment, &ParseTrigger,
    &ParsePayload,    &ParseCounter,    &ParseAccumulate,  &ParseNoise,
    &ParseGradient,   &ParseDelta,      &ParseSmooth,      &ParseExpr};
static_assert(std::size(kSignalParsers) == kSignalKindCount);

std::optional<Signal> SignalFrom(const std::string &a_name, const json &a_j,
                                 const Reporter &a_ctx) {
  const auto entry = OneKey(a_j, a_ctx, "a signal", {"curve"});
  if (!entry) {
    return std::nullopt;
  }
  const auto kindId = ParseSignalKind(entry->key);
  if (!kindId) {
    a_ctx.Error(std::format("unknown signal kind '{}'; one of {}", entry->key,
                            Choices(kSignalKinds)));
    return std::nullopt;
  }
  auto kind = kSignalParsers[IndexOf(*kindId)](*entry->value, a_ctx);
  if (!kind) {
    return std::nullopt;
  }
  Signal s;
  s.name = a_name;
  s.kind = std::move(*kind);
  Reader row(a_j, a_ctx);
  row.Child(entry->key);
  s.curve = CurveRefFrom(row);
  row.Finish();
  return s;
}

template <class Alternative>
std::optional<Alternative> ParseSourceAlternative(const json &a_v,
                                                  const Reporter &a_ctx);

template <>
std::optional<ImageSource>
ParseSourceAlternative<ImageSource>(const json &a_v, const Reporter &a_ctx) {
  if (!a_v.is_object()) {
    a_ctx.Error("'image' takes an object with 'path'");
    return std::nullopt;
  }
  ImageSource k;
  Reader r(a_v, a_ctx);
  k.path = r.Required("path");
  r.Read("channel", kImageChannels, k.channel);
  r.Read("space", kImageSpaces, k.space);
  r.Read("scroll", k.scroll);
  r.Read("tile", k.tile);
  if (const auto *m = r.Child("mirror")) {
    if (!m->is_array() || m->size() != 2 || !(*m)[0].is_boolean() ||
        !(*m)[1].is_boolean()) {
      a_ctx.Error("'mirror' is [u, v] booleans");
    } else {
      k.mirror = {(*m)[0].get<bool>(), (*m)[1].get<bool>()};
    }
  }
  r.Read("transpose", k.transpose);
  if (auto mip = r.Number("mip"))
    k.mip = std::max(0.0f, *mip);
  r.Finish();
  return k;
}

template <>
std::optional<MaterialSource>
ParseSourceAlternative<MaterialSource>(const json &a_v, const Reporter &a_ctx) {
  const auto channel = EnumShorthand(a_v, kMaterialChannels, "material", a_ctx);
  if (!channel) {
    return std::nullopt;
  }
  return MaterialSource{*channel};
}

std::optional<BakeKind> PartitionBakeFrom(const json &a_v,
                                          const Reporter &a_ctx) {
  const auto slot = Reader::BipedSlotFrom(a_v, "partition", a_ctx);
  if (!slot) {
    return std::nullopt;
  }
  return PartitionBake{*slot};
}

std::optional<BakeKind> BoneWeightBakeFrom(const json &a_v,
                                           const Reporter &a_ctx) {
  if (!a_v.is_array()) {
    a_ctx.Error("'boneWeight' is an array of bone names");
    return std::nullopt;
  }
  BoneWeightBake bw;
  for (const auto &b : a_v) {
    if (RowCapReached(bw.bones.size(), a_ctx, "boneWeight")) {
      break;
    }
    if (b.is_string())
      bw.bones.push_back(b.get<std::string>());
    else
      a_ctx.Error("'boneWeight' entries are bone names");
  }
  return bw;
}

template <>
std::optional<BakeSource>
ParseSourceAlternative<BakeSource>(const json &a_v, const Reporter &a_ctx) {
  BakeSource k;
  if (a_v.is_string()) {
    const auto bare = DefaultBakeKind(a_v.get<std::string>());
    if (!bare || Is<PartitionBake>(*bare) || Is<BoneWeightBake>(*bare)) {
      a_ctx.Error(std::format("'bake' is one of {}, or {{\"partition\": slot}} "
                              "or {{\"boneWeight\": [bones]}}",
                              Choices(kBakeKindWords)));
      return std::nullopt;
    }
    k.bake = *bare;
    return k;
  }
  const auto inner = OneKey(a_v, a_ctx, "'bake'");
  if (!inner) {
    return std::nullopt;
  }
  std::optional<BakeKind> bake;
  if (inner->key == "partition") {
    bake = PartitionBakeFrom(*inner->value, a_ctx);
  } else if (inner->key == "boneWeight") {
    bake = BoneWeightBakeFrom(*inner->value, a_ctx);
  } else {
    a_ctx.Error(std::format("unknown bake '{}'", inner->key));
    return std::nullopt;
  }
  if (!bake) {
    return std::nullopt;
  }
  k.bake = *bake;
  return k;
}

template <>
std::optional<UvSource>
ParseSourceAlternative<UvSource>(const json &a_v, const Reporter &a_ctx) {
  const auto axis = a_v.is_string() ? FromName(kUvAxes, a_v.get<std::string>())
                                    : std::nullopt;
  if (!axis) {
    a_ctx.Error("'uv' is \"u\" or \"v\"");
    return std::nullopt;
  }
  return UvSource{*axis};
}

template <>
std::optional<DistanceSource>
ParseSourceAlternative<DistanceSource>(const json &a_v, const Reporter &a_ctx) {
  DistanceSource k;
  if (a_v.is_string()) {
    k.from = a_v.get<std::string>();
  } else if (a_v.is_object()) {
    Reader r(a_v, a_ctx);
    if (const auto *from = r.Child("from")) {
      if (from->is_string()) {
        k.from = from->get<std::string>();
      } else if (auto point = Reader::PointFrom(*from, "from", a_ctx)) {
        k.from = *point;
      }
    } else {
      a_ctx.Error("'distance' needs 'from'");
    }
    r.Finish();
  } else {
    a_ctx.Error("'distance' is a node name or {\"from\": ...}");
    return std::nullopt;
  }
  return k;
}

template <>
std::optional<RippleSource>
ParseSourceAlternative<RippleSource>(const json &a_v, const Reporter &a_ctx) {
  if (!a_v.is_object()) {
    a_ctx.Error("'ripple' takes an object with 'trigger'");
    return std::nullopt;
  }
  RippleSource k;
  Reader r(a_v, a_ctx);
  r.Read("trigger", k.trigger);
  if (!r.Has("trigger"))
    a_ctx.Error("'ripple' needs 'trigger'");
  r.Read("speed", k.speed);
  r.Read("width", k.width);
  r.Read("decay", k.decay);
  r.Read("shape", kRippleShapes, k.shape);
  r.Finish();
  return k;
}

bool ClusterWeightsFrom(Reader &a_r, MaterialClustersSource &a_k,
                        const Reporter &a_ctx) {
  const auto *w = a_r.Child("weights");
  if (!w) {
    return true;
  }
  if (!w->is_object()) {
    a_ctx.Error("'weights' is an object of roughness, metallic, occlusion, "
                "reflectance and luma");
    return false;
  }
  bool ok = true;
  Reader wr(*w, a_ctx);
  for (const auto &[field, weight] :
       {std::pair{"roughness", &a_k.roughness},
        std::pair{"metallic", &a_k.metallic},
        std::pair{"occlusion", &a_k.occlusion},
        std::pair{"reflectance", &a_k.reflectance},
        std::pair{"luma", &a_k.luma}}) {
    if (auto x = wr.Number(field)) {
      if (*x < 0.0f || *x > kMaxChannelWeight) {
        a_ctx.Error(
            std::format("'weights.{}' is 0..{}", field, kMaxChannelWeight));
        ok = false;
      } else {
        *weight = *x;
      }
    }
  }
  wr.Finish();
  return ok;
}

template <>
std::optional<MaterialClustersSource>
ParseSourceAlternative<MaterialClustersSource>(const json &a_v,
                                               const Reporter &a_ctx) {
  if (!a_v.is_object()) {
    a_ctx.Error("'materialClusters' takes an object with 'clusters', "
                "'weights', 'seed' and 'iterations'");
    return std::nullopt;
  }
  MaterialClustersSource k;
  Reader r(a_v, a_ctx);
  bool ok = true;
  ok &= r.IntRange("clusters", 1, kMaxMaterialClusters, k.clusters);
  ok &= ClusterWeightsFrom(r, k, a_ctx);
  if (auto n = r.Integer("seed")) {
    if (*n < 0) {
      a_ctx.Error("'seed' is a whole number");
      ok = false;
    } else {
      k.seed = static_cast<std::uint32_t>(*n);
    }
  }
  ok &= r.IntRange("iterations", 1, static_cast<int>(kMaxClusterIterations),
                   k.iterations);
  r.Finish();
  if (!ok) {
    return std::nullopt;
  }
  return k;
}

using SourceParser = std::optional<SourceKind> (*)(const json &,
                                                   const Reporter &);

template <class Alternative>
std::optional<SourceKind> ParseSourceAs(const json &a_v,
                                        const Reporter &a_ctx) {
  std::optional<Alternative> parsed =
      ParseSourceAlternative<Alternative>(a_v, a_ctx);
  if (!parsed) {
    return std::nullopt;
  }
  return SourceKind{std::move(*parsed)};
}

template <std::size_t... I>
constexpr std::array<SourceParser, sizeof...(I)>
SourceParsersInVariantOrder(std::index_sequence<I...>) {
  return {&ParseSourceAs<std::variant_alternative_t<I, SourceKind>>...};
}

constexpr std::array<SourceParser, kSourceKindCount> kSourceParsers =
    SourceParsersInVariantOrder(std::make_index_sequence<kSourceKindCount>{});

std::optional<Source> SourceFrom(const std::string &a_name, const json &a_j,
                                 const Reporter &a_ctx) {
  Reader r(a_j, a_ctx);
  auto kind = ParseSourceKind(r);
  if (!kind) {
    return std::nullopt;
  }
  Source s;
  s.name = a_name;
  s.kind = std::move(*kind);
  return s;
}
}

std::optional<SourceKind> ParseSourceKind(Reader &a_reader) {
  const Reporter &ctx = a_reader.Context();
  const auto entry = OneKey(a_reader.Object(), ctx, "a source");
  if (!entry) {
    return std::nullopt;
  }
  const auto blank = DefaultSourceKind(entry->key);
  if (!blank) {
    ctx.Error(std::format("unknown source kind '{}'", entry->key));
    return std::nullopt;
  }
  a_reader.Child(entry->key);
  return kSourceParsers[IndexOf(SourceKindIdOf(*blank))](*entry->value, ctx);
}

namespace {

std::optional<Layer> LayerFrom(const json &a_j, const Reporter &a_ctx) {
  if (!a_j.is_object()) {
    a_ctx.Error("a layer must be an object");
    return std::nullopt;
  }
  Layer l;
  Reader r(a_j, a_ctx);
  if (const auto *src = r.Child("source")) {
    if (src->is_string()) {
      if (auto ref = r.RefFrom(*src, "source"))
        l.source = *ref;
    } else if (auto value = Reader::ValueFrom(*src, "source", a_ctx);
               value && Get<Vec3>(*value)) {
      const Vec3 raw = *Get<Vec3>(*value);
      std::array<Param, 3> parts{raw.x, raw.y, raw.z};
      NormaliseColor(parts);
      l.source = Vec3{*Get<float>(parts[0]), *Get<float>(parts[1]),
                      *Get<float>(parts[2])};
    } else {
      a_ctx.Error("'source' is \"@name\" or [r, g, b]");
    }
  } else {
    a_ctx.Error("'source' is required");
  }
  l.curve = CurveRefFrom(r);
  if (auto b = r.Enum("blend", kBlends))
    l.blend = *b;
  if (auto p = r.Parameter("opacity"))
    l.opacity = *p;
  else if (!r.Has("opacity"))
    a_ctx.Error("'opacity' is required");
  l.color = r.Vector3("color", true);
  l.mask = r.Reference("mask");
  if (auto channels = r.String("channels")) {
    if (auto set = ChannelSet::Parse(*channels)) {
      l.channels = *set;
    } else {
      a_ctx.Error("'channels' is a subset of \"rgba\"");
    }
  }
  r.Finish();
  return l;
}

Bones SkinnedBonesFrom(const json &a_v, const Reporter &a_ctx) {
  SkinnedBones sb;
  if (a_v.is_object()) {
    Reader b(a_v, a_ctx);
    if (auto m = b.Integer("max"))
      sb.max = static_cast<std::uint32_t>(std::max(0, *m));
    if (auto share = b.Number("minShare"))
      sb.minShare = *share;
    b.Finish();
  } else {
    a_ctx.Error("'skinned' takes {\"max\", \"minShare\"}");
  }
  return sb;
}

Bones NamedBonesFrom(const json &a_v, const Reporter &a_ctx) {
  NamedBones nb;
  if (a_v.is_array()) {
    for (const auto &b : a_v) {
      if (RowCapReached(nb.bones.size(), a_ctx, "named")) {
        break;
      }
      if (b.is_string())
        nb.bones.push_back(b.get<std::string>());
      else
        a_ctx.Error("'named' entries are bone names");
    }
  } else {
    a_ctx.Error("'named' is an array of bone names");
  }
  return nb;
}

std::optional<Bones> BonesFrom(const json &a_j, const Reporter &a_ctx) {
  const auto entry = OneKey(a_j, a_ctx, "'bones'");
  if (!entry) {
    return std::nullopt;
  }
  if (entry->key == "skinned") {
    return SkinnedBonesFrom(*entry->value, a_ctx);
  }
  if (entry->key == "named") {
    return NamedBonesFrom(*entry->value, a_ctx);
  }
  a_ctx.Error(
      std::format("'bones' is 'skinned' or 'named', not '{}'", entry->key));
  return std::nullopt;
}

void StackFrom(const json &a_stack, std::vector<Layer> &a_out,
               const Reporter &a_ctx) {
  if (!a_stack.is_array()) {
    a_ctx.Error("'stack' must be an array of layers");
    return;
  }
  ReadRows(a_stack, "stack", a_ctx, a_out,
           [&](const json &a_layer, std::size_t a_index) {
             return LayerFrom(
                 a_layer,
                 a_ctx.At(std::format("{} layer {}", a_ctx.where, a_index)));
           });
}

Output LightOutputFrom(Reader &a_r, const Reporter &a_ctx) {
  LightOutput l;
  if (const auto *bones = a_r.Child("bones")) {
    if (auto b = BonesFrom(*bones, a_ctx))
      l.bones = *b;
  } else {
    a_ctx.Error("a light needs 'bones'");
  }
  a_r.Read("offset", l.offset);
  a_r.Read("color", l.color, true);
  if (!a_r.Has("color"))
    a_ctx.Error("a light needs 'color'");
  a_r.Read("intensity", l.intensity);
  if (!a_r.Has("intensity"))
    a_ctx.Error("a light needs 'intensity'");
  a_r.Read("size", l.size);
  a_r.Read("cutoff", l.cutoff);
  a_r.Read("shadow", l.shadow);
  if (const auto *bulb = a_r.Child("bulb"))
    l.bulb = FormFrom(*bulb, a_ctx, "bulb");
  if (const auto *sel = a_r.Child("selector"))
    l.selector = SelectorFrom(*sel, a_ctx);
  a_r.Read("replace", l.replace);
  a_r.Finish();
  return Output{l};
}

Output SurfaceOutputFrom(Reader &a_r, Surface a_surface,
                         const Reporter &a_ctx) {
  SurfaceOutput m;
  m.surface = a_surface;
  a_r.Read("slot", kSlots, m.slot);
  if (!a_r.Has("slot"))
    a_ctx.Error("'slot' is required");
  for (const auto &field : kScalarFields) {
    Match(
        field.member,
        [&](std::optional<Param> SlotScalars::*member) {
          m.scalars.*member = a_r.Parameter(field.name);
        },
        [&](std::optional<Vec3Param> SlotScalars::*member) {
          m.scalars.*member = a_r.Vector3(field.name, true);
        });
  }
  if (const auto *sel = a_r.Child("selector"))
    m.selector = SelectorFrom(*sel, a_ctx);
  a_r.Read("replace", m.replace);
  if (const auto *stack = a_r.Child("stack"))
    StackFrom(*stack, m.stack, a_ctx);
  a_r.Finish();
  return Output{m};
}

std::optional<Output> OutputFrom(const json &a_j, const Reporter &a_ctx) {
  if (!a_j.is_object()) {
    a_ctx.Error("an output must be an object");
    return std::nullopt;
  }
  Reader r(a_j, a_ctx);
  const auto target = r.Required("target");
  if (target == "light") {
    return LightOutputFrom(r, a_ctx);
  }
  const auto surface = FromName(kSurfaces, target);
  if (!surface) {
    a_ctx.Error(
        std::format("'target' is one of {}, light", Choices(kSurfaces)));
    return std::nullopt;
  }
  return SurfaceOutputFrom(r, *surface, a_ctx);
}

void PoseFrom(const json &a_j, ShellPose &a_pose, const Reporter &a_ctx) {
  if (!a_j.is_object()) {
    a_ctx.Error("'pose' must be an object");
    return;
  }
  Reader p(a_j, a_ctx.At("shell pose"));
  p.Read("inflate", a_pose.inflate);
  p.Read("offset", a_pose.offset);
  p.Read("scale", a_pose.scale);
  if (auto pt = p.Point("scalePoint"))
    a_pose.scalePoint = *pt;
  p.Read("spin", a_pose.spin);
  if (auto ax = p.Point("spinAxis"))
    a_pose.spinAxis = *ax;
  p.Finish();
}

ShellSettings ShellFrom(const json &a_j, const Reporter &a_ctx) {
  ShellSettings s;
  if (!a_j.is_object()) {
    a_ctx.Error("'shell' must be an object");
    return s;
  }
  Reader r(a_j, a_ctx);
  r.Read("material", kShellMaterials, s.material);
  r.Read("blend", kShellBlends, s.blend);
  r.Read("depthBias", s.depthBias);
  if (auto t = r.Number("alphaTest"))
    s.alphaTest = std::clamp(*t, 0.0f, 1.0f);
  r.Read("alpha", s.alpha);
  r.Read("rimPower", s.rimPower);
  r.Read("emissive", s.emissive);
  if (const auto *pose = r.Child("pose"))
    PoseFrom(*pose, s.pose, a_ctx);
  r.Finish();
  return s;
}

void ReadOverrides(const json &a_overrides, std::map<std::string, Value> &a_out,
                   const Reporter &a_ctx) {
  if (!a_overrides.is_object()) {
    a_ctx.Error("'overrides' is an object of signal name to value");
    return;
  }
  for (const auto &[name, value] : a_overrides.items()) {
    if (RowCapReached(a_out.size(), a_ctx, "overrides")) {
      break;
    }
    if (auto val = Reader::ValueFrom(value, name, a_ctx)) {
      a_out[name] = *val;
    }
  }
}

std::optional<VariantKey> VariantKeyFrom(const json &a_key,
                                         const Reporter &a_ctx) {
  const auto entry = OneKey(a_key, a_ctx, "'key'");
  if (!entry) {
    return std::nullopt;
  }
  if (entry->key == "armor") {
    if (auto form = FormFrom(*entry->value, a_ctx, "armor"))
      return VariantKey{*form};
    return std::nullopt;
  }
  if (entry->key == "selector") {
    return VariantKey{SelectorFrom(*entry->value, a_ctx)};
  }
  a_ctx.Error(
      std::format("'key' is 'armor' or 'selector', not '{}'", entry->key));
  return std::nullopt;
}

std::optional<Variant> VariantFrom(const json &a_j, const Reporter &a_ctx) {
  if (!a_j.is_object()) {
    a_ctx.Error("a variant must be an object");
    return std::nullopt;
  }
  Variant v;
  Reader r(a_j, a_ctx);
  v.name = r.Required("name");
  const auto ctx = a_ctx.At(VariantWhere(v.name));
  if (const auto *key = r.Child("key")) {
    if (auto k = VariantKeyFrom(*key, ctx))
      v.key = *k;
  } else {
    ctx.Error("a variant needs 'key'");
  }
  if (const auto *overrides = r.Child("overrides"))
    ReadOverrides(*overrides, v.overrides, ctx);
  r.Finish();
  return v;
}

std::optional<Curve> CurveFrom(const std::string &a_name, const json &a_j,
                               const Reporter &a_ctx) {
  if (!a_j.is_string() || a_j.get<std::string>().empty()) {
    a_ctx.Error("a curve is an expression string in x");
    return std::nullopt;
  }
  return Curve{a_name, a_j.get<std::string>()};
}

std::optional<Mask> MaskFrom(const std::string &a_name, const json &a_j,
                             const Reporter &a_ctx) {
  if (!a_j.is_string() || a_j.get<std::string>().empty()) {
    a_ctx.Error("a mask is an expression string over sources");
    return std::nullopt;
  }
  return Mask{a_name, a_j.get<std::string>()};
}

void ReadMetadata(Reader &a_r, const Reporter &a_ctx, Metadata &a_meta) {
  a_meta.name = a_r.String("name").value_or("");
  a_meta.author = a_r.String("author").value_or("");
  a_meta.description = a_r.String("description").value_or("");
  a_meta.version = a_r.String("version").value_or("");
  a_meta.imported = a_r.String("imported").value_or("");
  if (const auto *m = a_r.Child("meta")) {
    if (m->is_object()) {
      a_meta.meta = m->dump();
    } else {
      a_ctx.Error("'meta' must be an object");
    }
  }
}

void ReadKeys(Reader &a_r, const Reporter &a_ctx,
              std::vector<RecipeKey> &a_out) {
  const auto *keys = a_r.Child("keys");
  if (!keys) {
    a_ctx.Error("'keys' is required");
    return;
  }
  if (!keys->is_array() || keys->empty()) {
    a_ctx.Error("'keys' must be a non-empty array");
    return;
  }
  ReadRows(*keys, "keys", a_ctx, a_out, [&](const json &a_key, std::size_t) {
    return KeyFrom(a_key, a_ctx);
  });
}

void ReadOutputs(Reader &a_r, const Reporter &a_ctx,
                 std::vector<Output> &a_out) {
  const auto *outputs = a_r.Child("outputs");
  if (!outputs) {
    return;
  }
  if (!outputs->is_array()) {
    a_ctx.Error("'outputs' must be an array");
    return;
  }
  ReadRows(*outputs, "outputs", a_ctx, a_out,
           [&](const json &a_output, std::size_t a_index) {
             return OutputFrom(a_output, a_ctx.At(OutputWhere(a_index)));
           });
}

void ReadVariants(Reader &a_r, const Reporter &a_ctx,
                  std::vector<Variant> &a_out) {
  const auto *variants = a_r.Child("variants");
  if (!variants) {
    return;
  }
  if (!variants->is_array()) {
    a_ctx.Error("'variants' must be an array");
    return;
  }
  ReadRows(*variants, "variants", a_ctx, a_out,
           [&](const json &a_variant, std::size_t a_index) {
             return VariantFrom(
                 a_variant, a_ctx.At(VariantWhere(std::to_string(a_index))));
           });
}
}

LoadResult ParseRecipe(std::string_view a_json, std::string_view a_id) {
  LoadResult result;
  const Reporter fileCtx{result.diagnostics, "file"};
  const std::optional<json> root = ParseObjectDocument(a_json, fileCtx);
  if (!root) {
    return result;
  }

  Recipe recipe;
  recipe.id = std::string{a_id};
  const Reporter ctx{result.diagnostics, "recipe"};
  Reader r(*root, ctx);

  const auto format = r.Integer("format");
  if (!r.Has("format")) {
    ctx.Error(std::format("'format' is required; this loader reads format {}",
                          kRecipeFormat));
  } else if (format && *format > kRecipeFormat) {
    ctx.Error(std::format("format {} is newer than this loader's {}", *format,
                          kRecipeFormat));
  }

  ReadMetadata(r, ctx, recipe.metadata);
  ReadKeys(r, ctx, recipe.keys);
  recipe.priority = r.Integer("priority");
  if (const auto *clock = r.Child("clock")) {
    Reader c(*clock, ctx.At("clock"));
    if (auto speed = c.Number("speed"))
      recipe.clock.speed = *speed;
    c.Finish();
  }

  NamedRows(r, "signals", SignalWhere, recipe.signals, SignalFrom);
  NamedRows(r, "curves", CurveWhere, recipe.curves, CurveFrom);
  NamedRows(r, "sources", SourceWhere, recipe.sources, SourceFrom);
  NamedRows(r, "masks", MaskWhere, recipe.masks, MaskFrom);

  ReadOutputs(r, ctx, recipe.outputs);
  if (const auto *shell = r.Child("shell")) {
    recipe.shell = ShellFrom(*shell, ctx.At("shell"));
  }
  ReadVariants(r, ctx, recipe.variants);
  r.Finish();

  for (auto &d : Validate(recipe)) {
    result.diagnostics.push_back(std::move(d));
  }
  result.recipe = std::move(recipe);
  return result;
}
}
