#include "recipe/Recipe.h"

#include "recipe/Words.h"

#include <cmath>
#include <format>
#include <limits>

namespace BetterEnchantmentEffects {
namespace {
void Count(const Reporter &a_report, std::size_t a_size,
           std::string_view a_field) {
  if (a_size > kMaxRecipeRows) {
    a_report.Error(
        std::format("'{}' allows at most {} entries", a_field, kMaxRecipeRows));
  }
}

void Number(const Reporter &a_report, float a_value, std::string_view a_field) {
  if (!std::isfinite(a_value)) {
    a_report.Error(std::format("'{}' must be finite", a_field));
  }
}

void Point(const Reporter &a_report, const Value &a_value,
           std::string_view a_field) {
  Match(
      a_value, [&](float v) { Number(a_report, v, a_field); },
      [&](const Vec2 &v) {
        Number(a_report, v.x, a_field);
        Number(a_report, v.y, a_field);
      },
      [&](const Vec3 &v) {
        Number(a_report, v.x, a_field);
        Number(a_report, v.y, a_field);
        Number(a_report, v.z, a_field);
      });
}

void Scalar(const Reporter &a_report, const Param &a_value,
            std::string_view a_field) {
  if (const auto *value = Get<float>(a_value)) {
    Number(a_report, *value, a_field);
  } else if (const auto *ref = Get<Ref>(a_value); ref && !IsName(ref->name)) {
    a_report.Error(
        std::format("'{}' must reference a valid resource name", a_field));
  }
}

template <std::size_t N>
void Vector(const Reporter &a_report,
            const std::variant<std::array<Param, N>, Ref> &a_value,
            std::string_view a_field) {
  Match(
      a_value, [&](const Ref &r) { Scalar(a_report, Param{r}, a_field); },
      [&](const std::array<Param, N> &parts) {
        for (const Param &part : parts) {
          Scalar(a_report, part, a_field);
        }
      });
}

void Integer(const Reporter &a_report, std::uint32_t a_value,
             std::uint32_t a_min, std::string_view a_field) {
  if (a_value < a_min || a_value > std::numeric_limits<int>::max()) {
    a_report.Error(std::format("'{}' must be {}..{}", a_field, a_min,
                               std::numeric_limits<int>::max()));
  }
}

void CheckBoneNames(const Reporter &a_report,
                    const std::vector<std::string> &a_bones) {
  Count(a_report, a_bones.size(), "bones");
  for (const std::string &bone : a_bones) {
    if (bone.empty()) {
      a_report.Error("bone names must not be empty");
    }
  }
}

void Select(const Reporter &a_report, const Selector &a_selector) {
  Count(a_report, a_selector.anyOf.size(), "selector");
}

struct SignalFields {
  Reporter report;
  void operator()(const ConstantSignal &s) const {
    Point(report, s.value, "value");
  }
  void operator()(const WaveSignal &s) const {
    Scalar(report, s.base, "base");
    Scalar(report, s.amplitude, "amplitude");
    Scalar(report, s.period, "period");
    Scalar(report, s.phase, "phase");
  }
  void operator()(const RampSignal &s) const {
    Scalar(report, s.from, "from");
    Scalar(report, s.to, "to");
    Scalar(report, s.seconds, "seconds");
  }
  void operator()(const TriggerSignal &s) const {
    Scalar(report, s.lifetime, "lifetime");
    Integer(report, s.max, 1, "max");
    if (const auto *event = Get<EventOrigin>(s.origin)) {
      if (event->filter.value.min) {
        Number(report, *event->filter.value.min, "filter.value.min");
      }
      if (event->filter.value.max) {
        Number(report, *event->filter.value.max, "filter.value.max");
      }
    }
  }
  void operator()(const CounterSignal &s) const {
    if (s.cap) {
      Scalar(report, *s.cap, "cap");
    }
  }
  void operator()(const AccumulateSignal &s) const {
    Scalar(report, s.decay, "decay");
  }
  void operator()(const NoiseSignal &s) const {
    Scalar(report, s.frequency, "frequency");
    Scalar(report, s.amplitude, "amplitude");
    Integer(report, s.seed, 0, "seed");
  }
  void operator()(const GradientSignal &s) const {
    Scalar(report, s.t, "t");
    Count(report, s.stops.size(), "stops");
    for (const GradientStop &stop : s.stops) {
      Number(report, stop.at, "stops.at");
      Vector(report, stop.color, "stops.color");
    }
  }
  void operator()(const SmoothSignal &s) const {
    Scalar(report, s.seconds, "seconds");
  }
  void operator()(const EfshSignal &) const {}
  void operator()(const ActorValueSignal &) const {}
  void operator()(const ActorStateSignal &) const {}
  void operator()(const EnchantmentSignal &) const {}
  void operator()(const PayloadSignal &) const {}
  void operator()(const RateSignal &) const {}
  void operator()(const ToRootSignal &) const {}
  void operator()(const ExprSignal &) const {}
};
struct SourceFields {
  Reporter report;
  void operator()(const ImageSource &s) const {
    Number(report, s.mip, "mip");
    if (s.mip < 0.0f)
      report.Error("'mip' must be at least 0");
    if (s.scroll)
      Vector(report, *s.scroll, "scroll");
    if (s.tile)
      Vector(report, *s.tile, "tile");
  }
  void operator()(const BakeSource &s) const {
    if (const auto *bones = Get<BoneWeightBake>(s.bake)) {
      CheckBoneNames(report, bones->bones);
    }
    if (const auto *partition = Get<PartitionBake>(s.bake);
        partition && (partition->bipedSlot < kFirstBipedSlot ||
                      partition->bipedSlot > kLastBipedSlot)) {
      report.Error("'bipedSlot' must be 30..61");
    }
  }
  void operator()(const RippleSource &s) const {
    Scalar(report, s.speed, "speed");
    Scalar(report, s.width, "width");
    Scalar(report, s.decay, "decay");
    Vector(report, s.direction, "direction");
  }
  void operator()(const MaterialClustersSource &s) const {
    Integer(report, s.settings.seed, 0, "seed");
    if (s.settings.clusters < 1 || s.settings.clusters > kMaxMaterialClusters) {
      report.Error(std::format("'clusters' is 1..{}", kMaxMaterialClusters));
    }
    if (s.settings.iterations < 1 ||
        s.settings.iterations > kMaxClusterIterations) {
      report.Error(std::format("'iterations' is 1..{}", kMaxClusterIterations));
    }
    for (const ClusterWeightField &field : kClusterWeightFields) {
      const float weight = s.settings.weights.*field.member;
      Number(report, weight, field.name);
      if (weight < 0.0f || weight > kMaxChannelWeight) {
        report.Error(std::format("'weights.{}' is 0..{}", field.name,
                                 kMaxChannelWeight));
      }
    }
  }
  void operator()(const MaterialSource &) const {}
  void operator()(const DistanceSource &) const {}
};

void CheckSkinnedBones(const Reporter &a_report, const SkinnedBones &a_bones) {
  Integer(a_report, a_bones.max, 1, "bones.skinned.max");
  Number(a_report, a_bones.minShare, "bones.skinned.minShare");
  if (a_bones.minShare != 0.0f &&
      (a_bones.minShare < 0.3f || a_bones.minShare > 1.0f)) {
    a_report.Error("'bones.skinned.minShare' must be 0.3..1 when set");
  }
}

struct OutputFields {
  Reporter report;
  std::size_t index;
  void operator()(const SurfaceOutput &s) const {
    Select(report, s.selector);
    Count(report, s.stack.size(), "stack");
    for (const auto &field : kScalarFields) {
      if (const auto *value = ScalarOf(s.scalars, field.value);
          value && *value) {
        Scalar(report, **value, field.name);
      }
    }
    if (s.scalars.color)
      Vector(report, *s.scalars.color, "color");
    for (std::size_t j = 0; j < s.stack.size(); ++j) {
      const Layer &layer = s.stack[j];
      const Reporter row = report.At(LayerWhere(index, j));
      if (const auto *point = Get<Vec3>(layer.source))
        Point(row, *point, "source");
      Scalar(row, layer.opacity, "opacity");
      if (layer.color)
        Vector(row, *layer.color, "color");
    }
  }
  void operator()(const LightOutput &s) const {
    Select(report, s.selector);
    Vector(report, s.offset, "offset");
    Vector(report, s.color, "color");
    Scalar(report, s.intensity, "intensity");
    Scalar(report, s.size, "size");
    Scalar(report, s.cutoff, "cutoff");
    Match(
        s.bones, [&](const NamedBones &b) { CheckBoneNames(report, b.bones); },
        [&](const SkinnedBones &b) { CheckSkinnedBones(report, b); });
  }
};

void CheckShellFields(const Reporter &a_report, const ShellSettings &s) {
  Number(a_report, s.alphaTest, "alphaTest");
  if (s.alphaTest < 0.0f || s.alphaTest > 1.0f) {
    a_report.Error("'alphaTest' must be 0..1");
  }
  Scalar(a_report, s.opacity, "opacity");
  Scalar(a_report, s.rimPower, "rimPower");
  Scalar(a_report, s.emissive, "emissive");
  Vector(a_report, s.pose.inflate, "inflate");
  Vector(a_report, s.pose.offset, "offset");
  Scalar(a_report, s.pose.scale, "scale");
  Scalar(a_report, s.pose.spin, "spin");
  Point(a_report, s.pose.scalePoint, "scalePoint");
  Point(a_report, s.pose.spinAxis, "spinAxis");
}

void CheckVariantFields(const Reporter &a_report, const Variant &a_variant) {
  if (a_variant.name.empty())
    a_report.Error("variant name must not be empty");
  Count(a_report, a_variant.overrides.size(), "overrides");
  if (const auto *selector = Get<Selector>(a_variant.key))
    Select(a_report, *selector);
  for (const auto &[name, value] : a_variant.overrides)
    Point(a_report, value, name);
}
}

std::vector<Diagnostic> CheckRecipeFields(const Recipe &a_recipe) {
  std::vector<Diagnostic> out;
  const Reporter recipe{out, "recipe"};
  Count(recipe, a_recipe.keys.size(), "keys");
  Count(recipe, a_recipe.signals.size(), "signals");
  Count(recipe, a_recipe.sources.size(), "sources");
  Count(recipe, a_recipe.masks.size(), "masks");
  Count(recipe, a_recipe.curves.size(), "curves");
  Count(recipe, a_recipe.outputs.size(), "outputs");
  Count(recipe, a_recipe.variants.size(), "variants");
  Number(Reporter{out, "recipe clock"}, a_recipe.clock.speed, "speed");
  for (const Signal &signal : a_recipe.signals) {
    Match(signal.kind, SignalFields{Reporter{out, SignalWhere(signal.name)}});
  }
  for (const Source &source : a_recipe.sources) {
    Match(source.kind, SourceFields{Reporter{out, SourceWhere(source.name)}});
  }
  for (std::size_t i = 0; i < a_recipe.outputs.size(); ++i) {
    Match(a_recipe.outputs[i], OutputFields{Reporter{out, OutputWhere(i)}, i});
  }
  CheckShellFields(Reporter{out, "shell"}, a_recipe.shell);
  for (const Variant &variant : a_recipe.variants) {
    CheckVariantFields(Reporter{out, VariantWhere(variant.name)}, variant);
  }
  return out;
}
}
