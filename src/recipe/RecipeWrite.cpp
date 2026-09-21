#include "recipe/Binders.h"
#include "recipe/Recipe.h"
#include "recipe/Words.h"

#include <format>
#include <string>
#include <utility>

namespace BetterEnchantmentEffects {
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
      [](const BakeSource &s) {
        return Match(
            s.bake,
            [](const PositionBake &) {
              return std::string{"bake position (bind pose, -128..128 units "
                                 "per axis as 0..1)"};
            },
            [](const LocalPositionBake &) {
              return std::string{
                  "bake localPosition (this geometry's bound as 0..1)"};
            },
            [](const WorldUpBake &) {
              return std::string{"bake worldUp (bind-pose normal)"};
            },
            [](const PartitionBake &p) {
              return std::format("bake partition {}",
                                 std::to_underlying(p.bipedSlot));
            },
            [](const BoneWeightBake &b) {
              return std::format("bake boneWeight of {} bone(s)",
                                 b.bones.size());
            },
            [](const ComponentIdBake &) {
              return std::string{
                  "bake componentId (the mesh's connected pieces, id / 255)"};
            },
            [](const ChartIdBake &) {
              return std::string{
                  "bake chartId (the mesh's UV charts, id / 255)"};
            });
      },
      [](const UvSource &s) {
        return std::format("uv {} (the coordinate as a ramp over the islands)",
                           s.axis == UvAxis::kU ? "u" : "v");
      },
      [](const DistanceSource &s) {
        return Match(
            s.from,
            [](const std::string &node) {
              return std::format(
                  "distance from node {} (bind pose, 0..256 units as 0..1)",
                  node);
            },
            [](const Vec3 &p) {
              return std::format("distance from ({:.0f}, {:.0f}, {:.0f}) (bind "
                                 "pose, 0..256 units as 0..1)",
                                 p.x, p.y, p.z);
            });
      },
      [](const RippleSource &s) {
        return std::format("ripple {} from @{}, speed {}, width {}, decay {}",
                           s.shape == RippleShape::kDisc ? "disc" : "ring",
                           s.trigger.name, ParamText(s.speed),
                           ParamText(s.width), ParamText(s.decay));
      },
      [](const MaterialClustersSource &s) {
        const MaterialClustersSource defaults;
        std::string text =
            std::format("materialClusters, {} clusters", s.clusters);
        for (const ClusterWeightField &field : kClusterWeightFields) {
          const float weight = s.*field.member;
          if (weight != defaults.*field.member) {
            text += std::format(", {} {}", field.name, weight);
          }
        }
        if (s.seed != defaults.seed) {
          text += std::format(", seed {}", s.seed);
        }
        if (s.iterations != defaults.iterations) {
          text += std::format(", {} iterations", s.iterations);
        }
        return text;
      });
}

namespace {
json CurveRefToJson(const CurveRef &a_curve) { return json(a_curve.text); }

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

json PulseToJson(const PulseSignal &k) {
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
        w.WriteTextIf("at", e.at);
      },
      [&](const PluginOrigin &p) { w.WriteText("plugin", p.id); },
      [&](const WhenOrigin &wo) {
        w.WriteRef("when", wo.when);
        w.WriteRefIf("value", wo.value);
      });
  w.Write("lifetime", k.lifetime);
  w.Set("max", k.max);
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

json SignalToJson(const Signal &a_signal) {
  json row = json::object();
  const auto key = [](SignalKindId a_id) {
    return std::string{SignalKindName(a_id)};
  };
  Match(
      a_signal.kind,
      [&](const ConstantSignal &k) {
        row[key(SignalKindId::kConstant)] = ValueToJson(k.value);
      },
      [&](const PulseSignal &k) {
        row[key(SignalKindId::kPulse)] = PulseToJson(k);
      },
      [&](const RampSignal &k) {
        row[key(SignalKindId::kRamp)] =
            json::object({{"from", ParamToJson(k.from)},
                          {"to", ParamToJson(k.to)},
                          {"seconds", ParamToJson(k.seconds)}});
      },
      [&](const EfshSignal &k) {
        row[key(SignalKindId::kEfsh)] =
            json::object({{"field", NameOf(kEfshFields, k.field)},
                          {"record", k.record.text}});
      },
      [&](const ActorValueSignal &k) {
        row[key(SignalKindId::kActorValue)] = ActorValueToJson(k);
      },
      [&](const ActorStateSignal &k) {
        row[key(SignalKindId::kActorState)] = NameOf(kActorStates, k.kind);
      },
      [&](const EnchantmentSignal &k) {
        row[key(SignalKindId::kEnchantment)] =
            NameOf(kEnchantmentFields, k.field);
      },
      [&](const TriggerSignal &k) {
        row[key(SignalKindId::kTrigger)] = TriggerToJson(k);
      },
      [&](const PayloadSignal &k) {
        row[key(SignalKindId::kPayload)] =
            json::object({{"trigger", "@" + k.trigger.name},
                          {"field", NameOf(kPayloadFields, k.field)}});
      },
      [&](const CounterSignal &k) {
        row[key(SignalKindId::kCounter)] = CounterToJson(k);
      },
      [&](const AccumulateSignal &k) {
        row[key(SignalKindId::kAccumulate)] =
            json::object({{"trigger", "@" + k.trigger.name},
                          {"decay", ParamToJson(k.decay)}});
      },
      [&](const NoiseSignal &k) {
        row[key(SignalKindId::kNoise)] = NoiseToJson(k);
      },
      [&](const GradientSignal &k) {
        row[key(SignalKindId::kGradient)] = GradientToJson(k);
      },
      [&](const DeltaSignal &k) {
        row[key(SignalKindId::kDelta)] = "@" + k.of.name;
      },
      [&](const SmoothSignal &k) {
        row[key(SignalKindId::kSmooth)] = json::object(
            {{"of", "@" + k.of.name}, {"seconds", ParamToJson(k.seconds)}});
      },
      [&](const ExprSignal &k) { row[key(SignalKindId::kExpr)] = k.text; });
  if (a_signal.curve) {
    row["curve"] = CurveRefToJson(*a_signal.curve);
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
      [&](const WorldUpBake &) { return json(BakeKindName(k.bake)); },
      [&](const ComponentIdBake &) { return json(BakeKindName(k.bake)); },
      [&](const ChartIdBake &) { return json(BakeKindName(k.bake)); });
}

json MaterialClustersToJson(const MaterialClustersSource &k) {
  const MaterialClustersSource defaults;
  json o = json::object();
  Writer w{o};
  w.WriteIf("clusters", static_cast<std::uint32_t>(k.clusters),
            static_cast<std::uint32_t>(defaults.clusters));
  json weights = json::object();
  Writer ww{weights};
  for (const ClusterWeightField &field : kClusterWeightFields) {
    ww.WriteNumberIf(field.name, k.*field.member, defaults.*field.member);
  }
  if (!weights.empty())
    o["weights"] = std::move(weights);
  w.WriteIf("seed", k.seed, defaults.seed);
  w.WriteIf("iterations", k.iterations, defaults.iterations);
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
      [&](const UvSource &k) { row[word] = NameOf(kUvAxes, k.axis); },
      [&](const DistanceSource &k) {
        Match(
            k.from, [&](const std::string &node) { row[word] = node; },
            [&](const Vec3 &p) {
              row[word] = json::object({{"from", PointToJson(p)}});
            });
      },
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
  if (l.bulb)
    w.WriteText("bulb", l.bulb->text);
  if (!l.selector.All())
    w.Set("selector", SelectorToJson(l.selector));
  w.WriteIf("replace", l.replace, false);
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
  w.WriteIf("alpha", a_shell.alpha, defaults.alpha);
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

std::string SerializeRecipe(const Recipe &a_recipe) {
  json root = json::object();
  root["format"] = kRecipeFormat;
  const auto &meta = a_recipe.metadata;
  if (!meta.name.empty())
    root["name"] = meta.name;
  if (!meta.author.empty())
    root["author"] = meta.author;
  if (!meta.description.empty())
    root["description"] = meta.description;
  if (!meta.version.empty())
    root["version"] = meta.version;
  if (!meta.imported.empty())
    root["imported"] = meta.imported;
  if (!meta.meta.empty()) {
    json m = json::parse(meta.meta, nullptr, false);
    root["meta"] = m.is_discarded() ? json::object() : m;
  }
  json keys = json::array();
  for (const auto &k : a_recipe.keys) {
    keys.push_back(KeyToJson(k));
  }
  root["keys"] = std::move(keys);
  if (a_recipe.priority)
    root["priority"] = *a_recipe.priority;
  if (a_recipe.overrideMode != OverrideMode::kStack)
    root["override"] = std::string{OverrideModeName(a_recipe.overrideMode)};
  if (a_recipe.clock != Clock{})
    root["clock"] = json::object({{"speed", Num(a_recipe.clock.speed)}});

  const auto named = [&](const char *a_section, const auto &a_rows,
                         auto a_toJson) {
    if (a_rows.empty()) {
      return;
    }
    json section = json::object();
    for (const auto &row : a_rows) {
      section[row.name] = a_toJson(row);
    }
    root[a_section] = std::move(section);
  };
  named("signals", a_recipe.signals,
        [](const Signal &s) { return SignalToJson(s); });
  named("curves", a_recipe.curves, [](const Curve &c) { return json(c.text); });
  named("sources", a_recipe.sources,
        [](const Source &s) { return SourceKindToJson(s.kind); });
  named("masks", a_recipe.masks, [](const Mask &m) { return json(m.text); });

  if (!a_recipe.outputs.empty()) {
    json outputs = json::array();
    for (const auto &o : a_recipe.outputs) {
      outputs.push_back(OutputToJson(o));
    }
    root["outputs"] = std::move(outputs);
  }
  if (json shell = ShellToJson(a_recipe.shell); !shell.empty()) {
    root["shell"] = std::move(shell);
  }
  if (!a_recipe.variants.empty()) {
    json variants = json::array();
    for (const auto &v : a_recipe.variants) {
      variants.push_back(VariantToJson(v));
    }
    root["variants"] = std::move(variants);
  }
  return DumpDocument(root);
}
}
