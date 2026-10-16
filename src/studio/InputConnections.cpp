// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/InputConnections.h"
#include "recipe/BusEvents.h"

#include "recipe/Words.h"
#include "studio/GameObjects.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

namespace BetterEnchantmentEffects::Studio {
namespace {
[[nodiscard]] std::string InputStem(std::string_view a_name) {
  std::string stem = "av_";
  for (const unsigned char c : a_name) {
    stem += std::isalnum(c) ? static_cast<char>(std::tolower(c)) : '_';
  }
  return stem;
}

constexpr std::string_view kFadeCurve = "1 - x";

struct ConnectionBuilder {
  EditBatch batch;
  std::vector<std::string> taken;

  [[nodiscard]] std::string Add(std::string_view a_stem, SignalKind a_kind) {
    const std::string name = UniqueName(a_stem, taken);
    taken.push_back(name);
    batch.edits.emplace_back(AddSignal{name});
    batch.edits.emplace_back(SetSignal{name, std::move(a_kind)});
    return name;
  }

  [[nodiscard]] std::string ActorValue(const std::string &a_name,
                                       Measure a_measure) {
    const std::string stem =
        std::format("{}_{}", InputStem(a_name), NameOf(kMeasures, a_measure));
    return Add(stem, ActorValueSignal{a_name, a_measure});
  }
};

[[nodiscard]] std::string
BuildResponseSignal(ConnectionBuilder &a_builder,
                    const InputConnectionSpec &a_spec) {
  if (a_spec.kind == InputConnectionKind::kHitResponse) {
    TriggerSignal trigger;
    trigger.origin = EventOrigin{std::string{kHitReceivedEvent}, {}};
    trigger.lifetime = 1.0f;
    trigger.max = 1;
    const std::string name = a_builder.Add("hit_glow", trigger);
    a_builder.batch.edits.emplace_back(
        SetSignalCurve{name, CurveRef{std::string{kFadeCurve}}});
    return name;
  }
  const Measure measure = a_spec.kind == InputConnectionKind::kMeasure
                              ? a_spec.measure
                              : Measure::kCurrent;
  std::string current = a_builder.ActorValue(a_spec.actorValue, measure);
  if (a_spec.kind == InputConnectionKind::kMeasure) {
    return current;
  }
  const std::string stem = InputStem(a_spec.actorValue);
  if (a_spec.kind == InputConnectionKind::kExhausted) {
    return a_builder.Add(stem + "_exhausted",
                         ExprSignal{std::format("@{} <= 0", current)});
  }
  const std::string maximum =
      a_builder.ActorValue(a_spec.actorValue, Measure::kMax);
  return a_builder.Add(stem + "_fraction",
                       ExprSignal{std::format("if(@{} > 0, @{} / @{}, 0)",
                                              maximum, current, maximum)});
}

[[nodiscard]] std::expected<std::pair<EditBatch, std::string>, std::string>
BuildInput(const Names &a_names, const InputConnectionSpec &a_spec) {
  if (IndexOf(a_spec.kind) >= kInputConnectionNames.size() ||
      (a_spec.kind != InputConnectionKind::kHitResponse &&
       a_spec.actorValue.empty())) {
    return std::unexpected("Choose an actor value or a hit response.");
  }
  if (RowOf(kMeasures, a_spec.measure) == nullptr) {
    return std::unexpected("Choose a supported actor-value measure.");
  }
  ConnectionBuilder builder{{}, ReservedNames(a_names)};
  std::string output = BuildResponseSignal(builder, a_spec);
  return std::pair{std::move(builder.batch), std::move(output)};
}
}

const ActorValueSample *SampleNamed(std::span<const ActorValueSample> a_samples,
                                    std::string_view a_name) {
  const auto found =
      std::ranges::find(a_samples, a_name, &ActorValueSample::name);
  return found == a_samples.end() ? nullptr : &*found;
}

std::vector<ReadAsOption> ReadAsOptions(const std::string &a_actorValue,
                                        const ActorValueSample *a_sample) {
  const auto sampled = [&](Measure a_measure) -> std::optional<float> {
    return a_sample ? SampleAt(*a_sample, a_measure) : std::nullopt;
  };
  std::vector<ReadAsOption> options;
  for (const InputMeasureInfo &info : kInputMeasures) {
    options.push_back(
        {std::string{NameOf(kMeasures, info.measure)},
         info.description,
         {InputConnectionKind::kMeasure, a_actorValue, info.measure},
         sampled(info.measure)});
  }
  const std::optional<float> current = sampled(Measure::kCurrent);
  const std::optional<float> maximum = sampled(Measure::kMax);
  options.push_back(
      {"current / max",
       "Ratio of current to maximum; zero when the maximum is zero or below.",
       {InputConnectionKind::kFraction, a_actorValue, Measure::kCurrent},
       current && maximum
           ? std::optional{*maximum > 0.0f ? *current / *maximum : 0.0f}
           : std::nullopt});
  options.push_back(
      {"exhausted",
       "One while current is zero or below, otherwise zero. Stays on while "
       "depleted.",
       {InputConnectionKind::kExhausted, a_actorValue, Measure::kCurrent},
       current ? std::optional{*current <= 0.0f ? 1.0f : 0.0f} : std::nullopt});
  return options;
}

InputConnectionSpec HitResponse() {
  return {InputConnectionKind::kHitResponse, {}, Measure::kCurrent};
}

bool CanConnectInput(const FormField &a_field) {
  return a_field.kind == FieldKind::kScalar && a_field.bind &&
         a_field.bind("@input").has_value();
}

std::expected<EditBatch, std::string>
ConnectInput(const FormField &a_field, const Names &a_names,
             const InputConnectionSpec &a_spec) {
  if (!CanConnectInput(a_field)) {
    return std::unexpected("This property does not accept a scalar input.");
  }
  std::expected<std::pair<EditBatch, std::string>, std::string> built =
      BuildInput(a_names, a_spec);
  if (!built) {
    return std::unexpected(std::move(built.error()));
  }
  EditBatch batch = std::move(built->first);
  const std::optional<RecipeEdit> binding =
      a_field.bind(ReferenceText(built->second));
  if (!binding) {
    return std::unexpected(
        "The input could not be connected to this property.");
  }
  batch.edits.push_back(*binding);
  return batch;
}
}
