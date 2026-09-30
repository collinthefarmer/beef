// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Binders.h"
#include "recipe/Recipe.h"
#include "recipe/Words.h"

#include <format>
#include <span>
#include <string>
#include <utility>

namespace BetterEnchantmentEffects {
namespace {
std::string DescribeBakeSource(const BakeSource &a_source) {
  return Match(
      a_source.bake,
      [](const PositionBake &) {
        return std::string{"bake position (bind pose, -128..128 units "
                           "per axis as 0..1)"};
      },
      [](const LocalPositionBake &) {
        return std::string{
            "bake localPosition (this geometry's bound as 0..1)"};
      },
      [](const NormalBake &) {
        return std::string{"bake normal (bind-pose normal, each axis as 0..1)"};
      },
      [](const UvBake &) {
        return std::string{"bake uv (the coordinates as a vec2)"};
      },
      [](const PartitionBake &p) {
        return std::format("bake partition {}",
                           std::to_underlying(p.bipedSlot));
      },
      [](const BoneWeightBake &b) {
        return std::format("bake boneWeight of {} bone(s)", b.bones.size());
      },
      [](const ComponentIdBake &) {
        return std::string{
            "bake componentId (the mesh's connected pieces, id / 255)"};
      },
      [](const ChartIdBake &) {
        return std::string{"bake chartId (the mesh's UV charts, id / 255)"};
      });
}

std::string
DescribeMaterialClustersSource(const MaterialClustersSource &a_source) {
  const ClusterSettings defaults;
  const ClusterSettings &settings = a_source.settings;
  std::string text =
      std::format("materialClusters, {} clusters", settings.clusters);
  for (const ClusterWeightField &field : kClusterWeightFields) {
    const float weight = settings.weights.*field.member;
    if (weight != defaults.weights.*field.member) {
      text += std::format(", {} {}", field.name, weight);
    }
  }
  if (settings.seed != defaults.seed) {
    text += std::format(", seed {}", settings.seed);
  }
  if (settings.iterations != defaults.iterations) {
    text += std::format(", {} iterations", settings.iterations);
  }
  return text;
}
}

std::string DescribeSource(const SourceKind &a_kind) {
  return Match(
      a_kind,
      [](const ImageSource &s) {
        return std::format(
            "image {} ({}, {}{}{})", s.path, ImageChannelName(s.channel),
            s.space == ImageSpace::kMesh ? "mesh" : "tiled",
            s.scroll ? ", scrolling" : "", s.tile ? ", tiled" : "");
      },
      [](const MaterialSource &s) {
        return std::format("material {}", MaterialChannelName(s.channel));
      },
      [](const BakeSource &s) { return DescribeBakeSource(s); },
      [](const DistanceSource &s) {
        return std::format(
            "distance from node {} (bind pose, 0..256 units as 0..1)", s.from);
      },
      [](const RippleSource &s) {
        return std::format("ripple {} from @{}, speed {}, width {}, decay {}",
                           s.shape == RippleShape::kDisc ? "disc" : "ring",
                           s.trigger.name, ParamText(s.speed),
                           ParamText(s.width), ParamText(s.decay));
      },
      [](const MaterialClustersSource &s) {
        return DescribeMaterialClustersSource(s);
      });
}

namespace {
json CurveRefToJson(const CurveRef &a_curve) { return json(a_curve.text); }

json NotedExpressionToJson(const std::string &a_text,
                           const std::string &a_note) {
  if (a_note.empty()) {
    return json(a_text);
  }
  return json::object({{"expr", a_text}, {"note", a_note}});
}

json KeyToJson(const RecipeKey &a_key) {
  const std::string word{NameOf(kKeyKinds, a_key.kind)};
  return Match(
      a_key.operand, [&](const std::monostate &) { return json(word); },
      [&](const FormRef &form) { return json::object({{word, form.text}}); },
      [&](const std::string &glob) { return json::object({{word, glob}}); });
}

json SelectorToJson(const Selector &a_selector) {
  json out = json::array();
  for (const auto &t : a_selector.anyOf) {
    const std::string value = Match(
        t.operand, [](const FormRef &form) { return form.text; },
        [](const std::string &glob) { return glob; });
    out.push_back(
        json::object({{std::string{NameOf(kSelectorKinds, t.kind)}, value}}));
  }
  return out;
}

json PulseToJson(const WaveSignal &k) {
  json o = json::object();
  Writer w{o};
  w.Write("base", k.base);
  w.Write("amplitude", k.amplitude);
  w.Write("period", k.period);
  w.WriteIf("phase", k.phase, Param{0.0f});
  w.WriteEnumIf("waveform", kWaveforms, k.waveform, Waveform::kSine);
  return o;
}

json ActorValueToJson(const ActorValueSignal &k) {
  if (k.measure == Measure::kCurrent) {
    return json(k.actorValue);
  }
  return json::object(
      {{"of", k.actorValue}, {"measure", NameOf(kMeasures, k.measure)}});
}

json TriggerToJson(const TriggerSignal &k) {
  json o = json::object();
  Writer w{o};
  Match(
      k.origin,
      [&](const EventOrigin &e) {
        w.WriteText("event", e.event);
        if (e.filter != EventFilter{}) {
          json f = json::object();
          Writer wf{f};
          wf.WriteTextIf("node", e.filter.node);
          wf.WriteTextIf("arg", e.filter.arg);
          if (e.filter.value != ValueRange{}) {
            wf.Set("value",
                   json::array({e.filter.value.min ? Num(*e.filter.value.min)
                                                   : json(nullptr),
                                e.filter.value.max ? Num(*e.filter.value.max)
                                                   : json(nullptr)}));
          }
          w.Set("filter", std::move(f));
        }
      },
      [&](const PluginOrigin &p) { w.WriteText("plugin", p.id); },
      [&](const WhenOrigin &wo) {
        w.WriteRef("when", wo.when);
        w.WriteRefIf("value", wo.value);
      });
  w.Write("lifetime", k.lifetime);
  w.Set("max", k.max);
  if (k.payload != ValueType::kScalar) {
    w.WriteText("payload", Name(k.payload));
  }
  Match(
      k.anchor, [](const std::monostate &) {},
      [&](const WorldAnchor &) { o["anchor"] = "world"; },
      [&](const NodeAnchor &n) {
        o["anchor"] = json::object({{"node", n.node}});
      });
  return o;
}

json CounterToJson(const CounterSignal &k) {
  json o = json::object();
  Writer w{o};
  w.WriteRef("trigger", k.trigger);
  w.WriteRefIf("reset", k.reset);
  w.WriteIf("cap", k.cap);
  return o;
}

json NoiseToJson(const NoiseSignal &k) {
  json o = json::object();
  Writer w{o};
  w.Write("frequency", k.frequency);
  w.Write("amplitude", k.amplitude);
  w.WriteIf("seed", k.seed, 0u);
  return o;
}

json GradientToJson(const GradientSignal &k) {
  json stops = json::array();
  for (const auto &s : k.stops) {
    stops.push_back(
        json::object({{"at", Num(s.at)}, {"color", VecToJson(s.color)}}));
  }
  json o = json::object();
  Writer w{o};
  w.Write("t", k.t);
  w.Set("stops", std::move(stops));
  return o;
}

json RampToJson(const RampSignal &k) {
  return json::object({{"from", ParamToJson(k.from)},
                       {"to", ParamToJson(k.to)},
                       {"seconds", ParamToJson(k.seconds)}});
}

json EfshToJson(const EfshSignal &k) {
  return json::object(
      {{"field", NameOf(kEfshFields, k.field)}, {"record", k.record.text}});
}

json AccumulateToJson(const AccumulateSignal &k) {
  return json::object(
      {{"trigger", "@" + k.trigger.name}, {"decay", ParamToJson(k.decay)}});
}

json SmoothToJson(const SmoothSignal &k) {
  return json::object(
      {{"of", "@" + k.of.name}, {"seconds", ParamToJson(k.seconds)}});
}

json SignalKindToJson(const SignalKind &a_kind) {
  const std::string word{SignalKindName(SignalKindOf(a_kind))};
  json row = json::object();
  Match(
      a_kind,
      [&](const ConstantSignal &k) { row[word] = ValueToJson(k.value); },
      [&](const WaveSignal &k) { row[word] = PulseToJson(k); },
      [&](const RampSignal &k) { row[word] = RampToJson(k); },
      [&](const EfshSignal &k) { row[word] = EfshToJson(k); },
      [&](const ActorValueSignal &k) { row[word] = ActorValueToJson(k); },
      [&](const ActorStateSignal &k) {
        row[word] = NameOf(kActorStates, k.kind);
      },
      [&](const EnchantmentSignal &k) {
        row[word] = NameOf(kEnchantmentFields, k.field);
      },
      [&](const TriggerSignal &k) { row[word] = TriggerToJson(k); },
      [&](const PayloadSignal &k) { row[word] = "@" + k.trigger.name; },
      [&](const CounterSignal &k) { row[word] = CounterToJson(k); },
      [&](const AccumulateSignal &k) { row[word] = AccumulateToJson(k); },
      [&](const NoiseSignal &k) { row[word] = NoiseToJson(k); },
      [&](const GradientSignal &k) { row[word] = GradientToJson(k); },
      [&](const RateSignal &k) { row[word] = "@" + k.of.name; },
      [&](const SmoothSignal &k) { row[word] = SmoothToJson(k); },
      [&](const ToRootSignal &k) { row[word] = "@" + k.of.name; },
      [&](const ExprSignal &k) { row[word] = k.text; });
  return row;
}

json SignalToJson(const Signal &a_signal) {
  json row = SignalKindToJson(a_signal.kind);
  if (a_signal.curve) {
    row["curve"] = CurveRefToJson(*a_signal.curve);
  }
  if (!a_signal.note.empty()) {
    row["note"] = a_signal.note;
  }
  return row;
}

json ImageToJson(const ImageSource &k) {
  json o = json::object();
  Writer w{o};
  w.WriteText("path", k.path);
  w.WriteEnumIf("channel", kImageChannels, k.channel, ImageChannel::kRgb);
  w.WriteEnumIf("space", kImageSpaces, k.space, ImageSpace::kTiled);
  w.WriteIf("scroll", k.scroll);
  w.WriteIf("tile", k.tile);
  if (k.mirror[0] || k.mirror[1])
    w.Set("mirror", json::array({k.mirror[0], k.mirror[1]}));
  w.WriteIf("transpose", k.transpose, false);
  w.WriteNumberIf("mip", k.mip, 0.0f);
  return o;
}

json BakeToJson(const BakeSource &k) {
  return Match(
      k.bake,
      [&](const PartitionBake &p) {
        return json::object({{"partition", BipedSlotToJson(p.bipedSlot)}});
      },
      [&](const BoneWeightBake &b) {
        return json::object({{"boneWeight", b.bones}});
      },
      [&](const PositionBake &) { return json(BakeKindName(k.bake)); },
      [&](const LocalPositionBake &) { return json(BakeKindName(k.bake)); },
      [&](const NormalBake &) { return json(BakeKindName(k.bake)); },
      [&](const UvBake &) { return json(BakeKindName(k.bake)); },
      [&](const ComponentIdBake &) { return json(BakeKindName(k.bake)); },
      [&](const ChartIdBake &) { return json(BakeKindName(k.bake)); });
}

json MaterialClustersToJson(const MaterialClustersSource &k) {
  const ClusterSettings defaults;
  const ClusterSettings &s = k.settings;
  json o = json::object();
  Writer w{o};
  w.WriteIf("clusters", static_cast<std::uint32_t>(s.clusters),
            static_cast<std::uint32_t>(defaults.clusters));
  json weights = json::object();
  Writer ww{weights};
  for (const ClusterWeightField &field : kClusterWeightFields) {
    ww.WriteNumberIf(field.name, s.weights.*field.member,
                     defaults.weights.*field.member);
  }
  if (!weights.empty())
    o["weights"] = std::move(weights);
  w.WriteIf("seed", s.seed, defaults.seed);
  w.WriteIf("iterations", s.iterations, defaults.iterations);
  return o;
}

json RippleToJson(const RippleSource &k) {
  json o = json::object();
  Writer w{o};
  w.WriteRef("trigger", k.trigger);
  w.Write("speed", k.speed);
  w.Write("width", k.width);
  w.Write("decay", k.decay);
  w.WriteEnumIf("shape", kRippleShapes, k.shape, RippleShape::kRing);
  w.WriteIf("direction", k.direction,
            Vec3Param{std::array<Param, 3>{0.0f, 0.0f, 0.0f}});
  return o;
}

}

json SourceKindToJson(const SourceKind &a_kind) {
  const std::string word{SourceKindName(a_kind)};
  json row = json::object();
  Match(
      a_kind, [&](const ImageSource &k) { row[word] = ImageToJson(k); },
      [&](const MaterialSource &k) {
        row[word] = NameOf(kMaterialChannels, k.channel);
      },
      [&](const BakeSource &k) { row[word] = BakeToJson(k); },
      [&](const DistanceSource &k) { row[word] = k.from; },
      [&](const RippleSource &k) { row[word] = RippleToJson(k); },
      [&](const MaterialClustersSource &k) {
        row[word] = MaterialClustersToJson(k);
      });
  return row;
}

namespace {

json LayerToJson(const Layer &a_layer) {
  json o = json::object();
  Writer w{o};
  Match(
      a_layer.source, [&](const Ref &ref) { w.WriteRef("source", ref); },
      [&](const Vec3 &c) { w.Set("source", PointToJson(c)); });
  if (a_layer.curve)
    w.Set("curve", CurveRefToJson(*a_layer.curve));
  w.WriteEnumIf("blend", kBlends, a_layer.blend, Blend::kReplace);
  w.Write("opacity", a_layer.opacity);
  w.WriteIf("color", a_layer.color);
  w.WriteRefIf("mask", a_layer.mask);
  if (a_layer.channels != ChannelSet{})
    w.WriteText("channels", a_layer.channels.ToString());
  w.WriteTextIf("note", a_layer.note);
  return o;
}

json SurfaceOutputToJson(const SurfaceOutput &m) {
  json o = json::object();
  Writer w{o};
  w.WriteEnum("target", kSurfaces, m.surface);
  w.WriteEnum("slot", kSlots, m.slot);
  for (const auto &field : kScalarFields) {
    Match(
        field.member,
        [&](std::optional<Param> SlotScalars::*member) {
          w.WriteIf(field.name, m.scalars.*member);
        },
        [&](std::optional<Vec3Param> SlotScalars::*member) {
          w.WriteIf(field.name, m.scalars.*member);
        });
  }
  if (!m.selector.All())
    w.Set("selector", SelectorToJson(m.selector));
  w.WriteIf("replace", m.replace, false);
  if (m.resolution)
    w.Set("resolution", std::string{ResolutionName(*m.resolution)});
  json stack = json::array();
  for (const auto &l : m.stack) {
    stack.push_back(LayerToJson(l));
  }
  w.Set("stack", std::move(stack));
  w.WriteTextIf("note", m.note);
  return o;
}

json LightOutputToJson(const LightOutput &l) {
  json o = json::object();
  Writer w{o};
  w.WriteText("target", "light");
  Match(
      l.bones,
      [&](const SkinnedBones &s) {
        json b = json::object({{"max", s.max}});
        if (s.minShare != 0.0f)
          b["minShare"] = Num(s.minShare);
        w.Set("bones", json::object({{"skinned", std::move(b)}}));
      },
      [&](const NamedBones &n) {
        w.Set("bones", json::object({{"named", n.bones}}));
      });
  w.WriteIf("offset", l.offset,
            Vec3Param{std::array<Param, 3>{0.0f, 0.0f, 0.0f}});
  w.Write("color", l.color);
  w.Write("intensity", l.intensity);
  w.Write("size", l.size);
  w.Write("cutoff", l.cutoff);
  w.WriteIf("shadow", l.shadow, false);
  if (!l.selector.All())
    w.Set("selector", SelectorToJson(l.selector));
  w.WriteIf("replace", l.replace, false);
  w.WriteTextIf("note", l.note);
  return o;
}

json OutputToJson(const Output &a_output) {
  return Match(
      a_output, [](const SurfaceOutput &m) { return SurfaceOutputToJson(m); },
      [](const LightOutput &l) { return LightOutputToJson(l); });
}

json ShellToJson(const ShellSettings &a_shell) {
  const ShellSettings defaults;
  json o = json::object();
  Writer w{o};
  w.WriteEnumIf("material", kShellMaterials, a_shell.material,
                defaults.material);
  w.WriteEnumIf("blend", kShellBlends, a_shell.blend, defaults.blend);
  w.WriteIf("depthBias", a_shell.depthBias, defaults.depthBias);
  w.WriteNumberIf("alphaTest", a_shell.alphaTest, defaults.alphaTest);
  w.WriteIf("opacity", a_shell.opacity, defaults.opacity);
  w.WriteIf("rimPower", a_shell.rimPower, defaults.rimPower);
  w.WriteIf("emissive", a_shell.emissive, defaults.emissive);
  const ShellPose &p = a_shell.pose;
  const ShellPose &d = defaults.pose;
  json pose = json::object();
  Writer wp{pose};
  wp.WriteIf("inflate", p.inflate, d.inflate);
  wp.WriteIf("offset", p.offset, d.offset);
  wp.WriteIf("scale", p.scale, d.scale);
  wp.WritePointIf("scalePoint", p.scalePoint, d.scalePoint);
  wp.WriteIf("spin", p.spin, d.spin);
  wp.WritePointIf("spinAxis", p.spinAxis, d.spinAxis);
  if (!pose.empty())
    o["pose"] = std::move(pose);
  return o;
}

json VariantToJson(const Variant &a_variant) {
  json o = json::object({{"name", a_variant.name}});
  Match(
      a_variant.key,
      [&](const FormRef &armor) {
        o["key"] = json::object({{"armor", armor.text}});
      },
      [&](const Selector &s) {
        o["key"] = json::object({{"selector", SelectorToJson(s)}});
      });
  json overrides = json::object();
  for (const auto &[name, value] : a_variant.overrides) {
    overrides[name] = ValueToJson(value);
  }
  o["overrides"] = std::move(overrides);
  return o;
}
}

namespace {
json CurveToJson(const Curve &a_curve) {
  return NotedExpressionToJson(a_curve.text, a_curve.note);
}

json MaskToJson(const Mask &a_mask) {
  return NotedExpressionToJson(a_mask.text, a_mask.note);
}

json SourceToJson(const Source &a_source) {
  json row = SourceKindToJson(a_source.kind);
  if (!a_source.note.empty()) {
    row["note"] = a_source.note;
  }
  return row;
}

json OutputsToJson(std::span<const Output> a_outputs) {
  json outputs = json::array();
  for (const Output &output : a_outputs) {
    outputs.push_back(OutputToJson(output));
  }
  return outputs;
}

json VariantsToJson(std::span<const Variant> a_variants) {
  json variants = json::array();
  for (const Variant &variant : a_variants) {
    variants.push_back(VariantToJson(variant));
  }
  return variants;
}

void WriteMetadata(json &a_root, const Metadata &a_metadata) {
  if (!a_metadata.name.empty())
    a_root["name"] = a_metadata.name;
  if (!a_metadata.author.empty())
    a_root["author"] = a_metadata.author;
  if (!a_metadata.description.empty())
    a_root["description"] = a_metadata.description;
  if (!a_metadata.version.empty())
    a_root["version"] = a_metadata.version;
  if (!a_metadata.imported.empty())
    a_root["imported"] = a_metadata.imported;
  if (!a_metadata.meta.empty()) {
    json m = json::parse(a_metadata.meta, nullptr, false);
    a_root["meta"] = m.is_discarded() ? json::object() : m;
  }
}

void WriteSelection(json &a_root, const Recipe &a_recipe) {
  json keys = json::array();
  for (const RecipeKey &key : a_recipe.keys) {
    keys.push_back(KeyToJson(key));
  }
  a_root["keys"] = std::move(keys);
  if (a_recipe.priority)
    a_root["priority"] = *a_recipe.priority;
  if (a_recipe.mergeMode != MergeMode::kStack)
    a_root["merge"] = std::string{MergeModeName(a_recipe.mergeMode)};
  if (a_recipe.clock != Clock{})
    a_root["clock"] = json::object({{"speed", Num(a_recipe.clock.speed)}});
}

template <typename Row>
void WriteNamedSection(json &a_root, const char *a_section,
                       std::span<const Row> a_rows,
                       json (*a_toJson)(const Row &)) {
  if (a_rows.empty()) {
    return;
  }
  json section = json::object();
  for (const Row &row : a_rows) {
    section[row.name] = a_toJson(row);
  }
  a_root[a_section] = std::move(section);
}
}

std::string SerializeRecipe(const Recipe &a_recipe) {
  json root = json::object();
  root["format"] = kRecipeFormat;
  WriteMetadata(root, a_recipe.metadata);
  WriteSelection(root, a_recipe);
  WriteNamedSection<Signal>(root, "signals", a_recipe.signals, SignalToJson);
  WriteNamedSection<Curve>(root, "curves", a_recipe.curves, CurveToJson);
  WriteNamedSection<Source>(root, "sources", a_recipe.sources, SourceToJson);
  WriteNamedSection<Mask>(root, "masks", a_recipe.masks, MaskToJson);
  if (!a_recipe.outputs.empty()) {
    root["outputs"] = OutputsToJson(a_recipe.outputs);
  }
  if (json shell = ShellToJson(a_recipe.shell); !shell.empty()) {
    root["shell"] = std::move(shell);
  }
  if (!a_recipe.variants.empty()) {
    root["variants"] = VariantsToJson(a_recipe.variants);
  }
  return DumpDocument(root);
}
}
