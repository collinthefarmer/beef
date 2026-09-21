#pragma once

#include "Core.h"
#include "recipe/Recipe.h"
#include "recipe/Words.h"

#include <array>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

namespace BetterEnchantmentEffects {
enum class ResourceKind { kSignal, kSource, kMask, kCurve, kCount };
inline constexpr std::array<std::string_view, 4> kResourceKindNames{
    "signal", "source", "mask", "curve"};
static_assert(kResourceKindNames.size() ==
              static_cast<std::size_t>(ResourceKind::kCount));

struct ResourceRef {
  ResourceKind kind = ResourceKind::kSignal;
  std::string name;
  [[nodiscard]] bool operator==(const ResourceRef &) const = default;
};
struct OutputOwner {
  std::size_t output = 0;
  [[nodiscard]] bool operator==(const OutputOwner &) const = default;
};
struct LayerOwner {
  std::size_t output = 0;
  std::size_t layer = 0;
  [[nodiscard]] bool operator==(const LayerOwner &) const = default;
};
struct ShellOwner {
  [[nodiscard]] bool operator==(const ShellOwner &) const = default;
};
struct VariantOwner {
  std::size_t index = 0;
  [[nodiscard]] bool operator==(const VariantOwner &) const = default;
};
using RelationshipOwner = std::variant<ResourceRef, OutputOwner, LayerOwner,
                                       ShellOwner, VariantOwner>;
struct PropertyLocation {
  RelationshipOwner owner = ShellOwner{};
  std::string property;
  std::optional<std::size_t> component;
  [[nodiscard]] bool operator==(const PropertyLocation &) const = default;
};

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

inline std::optional<float> LiteralOf(const Param &a_param) {
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

inline std::optional<float> ScalarDefault(Slot a_slot, ScalarField a_field) {
  return ScalarRequired(a_slot, a_field)
             ? std::optional{ScalarFallback(a_field)}
             : std::nullopt;
}

template <class Visitor>
void VisitSignalParams(SignalKind &a_kind, Visitor &a_visit) {
  Match(
      a_kind,
      [&](WaveSignal &s) {
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
      [&](RateSignal &s) { VisitRef(s.of, a_visit, "of"); },
      [&](SmoothSignal &s) {
        VisitRef(s.of, a_visit, "of");
        VisitParam(s.seconds, std::nullopt, a_visit, "seconds");
      },
      [](ConstantSignal &) {}, [](EfshSignal &) {}, [](ActorValueSignal &) {},
      [](ActorStateSignal &) {}, [](EnchantmentSignal &) {},
      [](ExprSignal &) {});
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
      [](MaterialSource &) {}, [](BakeSource &) {}, [](UvSource &) {},
      [](DistanceSource &) {}, [](MaterialClustersSource &) {});
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
  const Layer layerDefaults{};
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
  VisitParam(a_shell.opacity, LiteralOf(shellDefaults.opacity), a_visit,
             "opacity");
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

template <class Derived> struct ParamRefVisitor : LocatedVisitor {
  Derived &Self() { return static_cast<Derived &>(*this); }
  void Reference(Ref &a_ref) {
    Self().OnRef(a_ref, [] {});
  }
  void Scalar(Param &a_param, std::optional<float> a_default) {
    if (Ref *ref = Get<Ref>(a_param)) {
      Self().OnRef(*ref, [&] { a_param = Param{a_default.value_or(0.0f)}; });
    }
  }
  void OptionalScalar(std::optional<Param> &a_param,
                      std::optional<float> a_default) {
    if (!a_param) {
      return;
    }
    if (Ref *ref = Get<Ref>(*a_param)) {
      Self().OnRef(*ref, [&] {
        if (a_default) {
          *a_param = Param{*a_default};
        } else {
          a_param.reset();
        }
      });
    }
  }
  template <std::size_t N>
  void Vector(std::variant<std::array<Param, N>, Ref> &a_param,
              std::optional<std::array<float, N>> a_default) {
    if (Ref *ref = Get<Ref>(a_param)) {
      Self().OnRef(*ref, [&] {
        std::array<Param, N> literal{};
        for (std::size_t i = 0; i < N; ++i) {
          literal[i] = Param{a_default ? (*a_default)[i] : 0.0f};
        }
        a_param = literal;
      });
    } else if (auto *parts = Get<std::array<Param, N>>(a_param)) {
      for (std::size_t i = 0; i < parts->size(); ++i) {
        location.component = i;
        Scalar((*parts)[i], a_default ? std::optional<float>{(*a_default)[i]}
                                      : std::nullopt);
      }
      location.component.reset();
    }
  }
  template <std::size_t N>
  void OptionalVector(
      std::optional<std::variant<std::array<Param, N>, Ref>> &a_param,
      std::optional<std::array<float, N>> a_default) {
    if (!a_param) {
      return;
    }
    if (Ref *ref = Get<Ref>(*a_param)) {
      Self().OnRef(*ref, [&] {
        if (!a_default) {
          a_param.reset();
          return;
        }
        std::array<Param, N> literal{};
        for (std::size_t i = 0; i < N; ++i) {
          literal[i] = Param{(*a_default)[i]};
        }
        *a_param = literal;
      });
      return;
    }
    auto *parts = Get<std::array<Param, N>>(*a_param);
    if (!parts) {
      return;
    }
    for (std::size_t i = 0; i < parts->size(); ++i) {
      if (Ref *ref = Get<Ref>((*parts)[i])) {
        location.component = i;
        Self().OnRef(*ref, [&] {
          if (a_default) {
            (*parts)[i] = Param{(*a_default)[i]};
          } else {
            a_param.reset();
          }
        });
        location.component.reset();
        if (!a_param) {
          return;
        }
      }
    }
  }
};

template <class Fn> struct RefVisitor : ParamRefVisitor<RefVisitor<Fn>> {
  Fn &visit;
  explicit RefVisitor(Fn &a_visit) : visit(a_visit) {}
  template <class Replace> void OnRef(Ref &a_ref, Replace) {
    visit(a_ref, this->location);
  }
};

template <class Fn> void ForEachSignalRef(Recipe &a_recipe, Fn a_visit) {
  RefVisitor<Fn> visitor{a_visit};
  ForEachParam(a_recipe, visitor);
}

template <class Fn> void ForEachOverrideName(Recipe &a_recipe, Fn a_visit) {
  for (std::size_t index = 0; index < a_recipe.variants.size(); ++index) {
    for (const auto &[name, value] : a_recipe.variants[index].overrides) {
      a_visit(name, PropertyLocation{VariantOwner{index}, "override", {}});
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
}
