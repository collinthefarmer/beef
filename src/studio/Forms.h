#pragma once

#include "Core.h"
#include "studio/Create.h"
#include "studio/Edits.h"
#include "studio/FieldParsing.h"
#include "studio/GameObjects.h"
#include "studio/Names.h"
#include "studio/Panels.h"
#include "studio/SelectorEdit.h"
#include "studio/Snapshot.h"

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects::Studio {
enum class FieldKind {
  kScalar,
  kColor,
  kVector,
  kReference,
  kExpression,
  kCurve,
  kMask,
  kChannels,
  kToggle,
  kChoice,
  kText,
  kVec2,
  kName,
  kSignalValue,
  kLayerSource,
};
inline constexpr std::size_t kFieldKindCount = 15;

enum class FieldInputKind {
  kCombo,
  kChoice,
  kToggle,
  kText,
  kPlain,
  kValue,
  kChannels,
};

enum class FieldCheckKind {
  kScalar,
  kColorOrVector,
  kVec2,
  kReference,
  kExpression,
  kMask,
  kCurve,
  kChannels,
  kChoice,
  kName,
  kSignalValue,
  kLayerSource,
  kNone,
};

enum class Swatch {
  kNone,
  kAlways,
  kWhenColour,
};

struct FieldKindSpec {
  FieldKind value;
  const char *glyph;
  bool takesSignal;
  const char *help;
  FieldInputKind input;
  FieldCheckKind check;
  Swatch swatch;
  std::array<float, 3> colour;
};

inline constexpr FieldKindSpec kFieldKinds[]{
    {FieldKind::kScalar,
     "#",
     true,
     "scalar: a number, or @signal of scalar type",
     FieldInputKind::kValue,
     FieldCheckKind::kScalar,
     Swatch::kNone,
     {0.55f, 0.80f, 1.00f}},
    {FieldKind::kColor,
     "c",
     true,
     "colour: r, g, b in 0..1, or one number for all three, or @signal of "
     "colour type",
     FieldInputKind::kValue,
     FieldCheckKind::kColorOrVector,
     Swatch::kAlways,
     {1.00f, 0.70f, 0.45f}},
    {FieldKind::kVector,
     "v",
     true,
     "vector: x, y, z (a position, direction or scale), or one number for all "
     "three, or @signal of vector type",
     FieldInputKind::kValue,
     FieldCheckKind::kColorOrVector,
     Swatch::kNone,
     {0.55f, 0.95f, 0.80f}},
    {FieldKind::kReference,
     "@",
     false,
     "reference: @name of a row of the recipe",
     FieldInputKind::kCombo,
     FieldCheckKind::kReference,
     Swatch::kNone,
     {0.60f, 0.95f, 0.60f}},
    {FieldKind::kExpression,
     "=",
     false,
     "expression: numbers, [r, g, b], @signals, + - * /, comparisons, "
     "and/or/not, if(c, a, b), abs min max clamp saturate floor ceil frac sqrt "
     "pow sin cos step smoothstep lerp length distance dot cross normalize, "
     "time, pi",
     FieldInputKind::kText,
     FieldCheckKind::kExpression,
     Swatch::kNone,
     {1.00f, 0.90f, 0.45f}},
    {FieldKind::kCurve,
     "x",
     false,
     "curve: an expression in x (mean is the source's mean), or @curve",
     FieldInputKind::kValue,
     FieldCheckKind::kCurve,
     Swatch::kNone,
     {0.55f, 0.95f, 0.95f}},
    {FieldKind::kMask,
     "m",
     false,
     "mask: an expression per texel where @source and @mask names are images "
     "and @signals are this tick's values",
     FieldInputKind::kText,
     FieldCheckKind::kMask,
     Swatch::kNone,
     {0.85f, 0.65f, 1.00f}},
    {FieldKind::kChannels,
     "ch",
     false,
     "channels: any of r g b a, in any order",
     FieldInputKind::kChannels,
     FieldCheckKind::kChannels,
     Swatch::kNone,
     {0.85f, 0.85f, 0.85f}},
    {FieldKind::kToggle,
     "?",
     false,
     "on or off",
     FieldInputKind::kToggle,
     FieldCheckKind::kNone,
     Swatch::kNone,
     {0.85f, 0.85f, 0.85f}},
    {FieldKind::kChoice,
     "o",
     false,
     "one of the listed values",
     FieldInputKind::kChoice,
     FieldCheckKind::kChoice,
     Swatch::kNone,
     {0.85f, 0.85f, 0.85f}},
    {FieldKind::kText,
     "\"",
     false,
     "text",
     FieldInputKind::kText,
     FieldCheckKind::kNone,
     Swatch::kNone,
     {0.85f, 0.85f, 0.85f}},
    {FieldKind::kVec2,
     "v",
     true,
     "vec2: x, y, or one number for both, or @signal of vec2 type",
     FieldInputKind::kValue,
     FieldCheckKind::kVec2,
     Swatch::kNone,
     {0.55f, 0.95f, 0.80f}},
    {FieldKind::kName,
     "n",
     false,
     "name: letters, digits and underscores, not starting with a digit, and "
     "not another row's",
     FieldInputKind::kPlain,
     FieldCheckKind::kName,
     Swatch::kNone,
     {0.85f, 0.85f, 0.85f}},
    {FieldKind::kSignalValue,
     "=",
     false,
     "value: a number keeps or makes a scalar constant, r, g, b a colour "
     "constant, anything else that parses an expression over signals",
     FieldInputKind::kValue,
     FieldCheckKind::kSignalValue,
     Swatch::kWhenColour,
     {1.00f, 0.90f, 0.45f}},
    {FieldKind::kLayerSource,
     "c",
     true,
     "source: @source or @mask of the recipe, or r, g, b as a constant colour",
     FieldInputKind::kValue,
     FieldCheckKind::kLayerSource,
     Swatch::kWhenColour,
     {1.00f, 0.70f, 0.45f}},
};
static_assert(std::size(kFieldKinds) == kFieldKindCount);

enum class FieldDetail {
  kSource,
  kCurve,
  kOpacity,
  kColor,
  kMask,
  kSignal,
  kReferences,
};

using FieldBinding =
    std::function<std::optional<RecipeEdit>(const std::string &)>;
using FieldCreator = std::function<std::optional<Created>(const std::string &,
                                                          const RecipeRow &)>;

struct FormField {
  std::string name;
  FieldKind kind = FieldKind::kScalar;
  std::string text;
  std::vector<std::string> names;
  bool allowEmpty = false;
  std::optional<FieldDetail> detail;
  std::optional<Value> value;
  FieldBinding bind;
  std::vector<std::string> creators{};
  FieldCreator create{};
  std::optional<std::pair<float, float>> range{};
  std::optional<std::pair<float, float>> workingRange{};
  std::string units{};
  bool integral = false;
  std::string help{};
  std::optional<ChannelSet> channelMask{};
  std::optional<GameObjectKind> catalog{};
  std::optional<std::uint64_t> expectedRevision = std::nullopt;
};

[[nodiscard]] FormField CurveTextField(const std::string &a_curve,
                                       const std::string &a_text);
[[nodiscard]] FormField MaskTextField(const std::string &a_mask,
                                      const std::string &a_text);

[[nodiscard]] std::vector<FormField>
InspectorForm(const Inspector &a_inspector);
[[nodiscard]] std::vector<FormField> ScalarForm(const LayerStack &a_stack);
[[nodiscard]] std::vector<FormField> SignalForm(const SignalRow &a_signal,
                                                const SignalNames &a_names);
[[nodiscard]] std::optional<RecipeEdit>
SignalValueEdit(const std::string &a_signal, const std::string &a_text);
[[nodiscard]] std::vector<FormField> SourceForm(const SourceRow &a_source,
                                                const SignalNames &a_names);
[[nodiscard]] std::vector<FormField>
RecipeHeaderForm(const RecipeRow &a_recipe);

struct OutputHeader {
  std::vector<FormField> fields;
  SelectorView selector;
};
[[nodiscard]] OutputHeader OutputHeaderForm(std::size_t a_output,
                                            bool a_replace,
                                            const Selector &a_selector);

[[nodiscard]] std::vector<FormField> LightForm(const LightRow &a_light,
                                               const SignalNames &a_names);
[[nodiscard]] std::vector<FormField> ShellForm(const ShellRow &a_shell,
                                               const SignalNames &a_names);

}
