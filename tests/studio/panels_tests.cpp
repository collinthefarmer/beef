#include "recipe/Recipe.h"
#include "studio/Fields.h"
#include "studio/Forms.h"
#include "studio/Panels.h"
#include "studio/Rows.h"
#include "test_support.h"

#include <algorithm>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
[[nodiscard]] const FormField *Field(const std::vector<FormField> &a_form,
                                     const std::string &a_name) {
  for (const auto &field : a_form) {
    if (field.name == a_name) {
      return &field;
    }
  }
  return nullptr;
}

[[nodiscard]] SignalRow ConstantRow(std::string a_name, float a_value) {
  SignalRow row;
  row.name = std::move(a_name);
  row.kind = SignalKindId::kConstant;
  row.type = ValueType::kScalar;
  row.value = a_value;
  row.definition = ConstantSignal{a_value};
  return row;
}

[[nodiscard]] RecipeRow SampleRecipe() {
  RecipeRow recipe;
  recipe.id = "sample";
  SignalRow glow = ConstantRow("glow", 0.5f);
  SignalRow tint;
  tint.name = "tint";
  tint.kind = SignalKindId::kConstant;
  tint.type = ValueType::kVec3;
  tint.definition = ConstantSignal{Vec3{1.0f, 0.0f, 0.0f}};
  SignalRow hit;
  hit.name = "hit";
  hit.kind = SignalKindId::kTrigger;
  hit.type = ValueType::kScalar;
  hit.definition = TriggerSignal{};
  recipe.signals = {glow, tint, hit};
  recipe.curves = {TextRow{"ease", "x", 0}};
  recipe.masks = {"edge"};
  return recipe;
}

void TestSignalNames() {
  const RecipeRow recipe = SampleRecipe();
  const SignalNames names = SignalNamesOf(recipe);
  Check(names.scalar.size() == 2 &&
            std::ranges::find(names.scalar, "glow") != names.scalar.end(),
        "SignalNamesOf collects scalar signals");
  Check(names.color.size() == 1 && names.color.front() == "tint",
        "SignalNamesOf collects colour signals");
  Check(names.triggers.size() == 1 && names.triggers.front() == "hit",
        "SignalNamesOf collects triggers");
}

void TestSignalFormConstant() {
  const RecipeRow recipe = SampleRecipe();
  const SignalNames names = SignalNamesOf(recipe);
  const std::vector<FormField> form = SignalForm(recipe.signals[0], names);
  Check(form.size() >= 2 && form[0].name == "kind" &&
            form[0].kind == FieldKind::kChoice,
        "SignalForm opens with a kind choice");
  const FormField *value = Field(form, "value");
  Check(value != nullptr && value->kind == FieldKind::kSignalValue,
        "constant signal exposes a signal-value field");
  Check(value != nullptr && value->bind &&
            std::holds_alternative<SetConstant>(*value->bind("0.25")),
        "the value binding parses a constant");
}

void TestSignalFormPulse() {
  SignalRow pulse;
  pulse.name = "beat";
  pulse.kind = SignalKindId::kPulse;
  pulse.type = ValueType::kScalar;
  pulse.definition = PulseSignal{};
  const std::vector<FormField> form = SignalForm(pulse, SignalNames{});
  const FormField *base = Field(form, "base");
  const FormField *waveform = Field(form, "waveform");
  Check(base != nullptr && base->kind == FieldKind::kScalar,
        "pulse exposes a scalar base field");
  Check(waveform != nullptr && waveform->kind == FieldKind::kChoice,
        "pulse exposes a waveform choice");
  Check(base != nullptr && base->bind &&
            std::holds_alternative<SetSignal>(*base->bind("2")),
        "a pulse member binding edits the signal record");
  Check(base != nullptr && base->bind && !base->bind("not a number"),
        "a pulse member binding refuses unparsable text");
}

void TestSourceRoundTrip() {
  const Source source{"mat", MaterialSource{MaterialChannel::kRoughness}};
  const SourceRow row = SourceRowOf(source, 3);
  Check(Is<MaterialSourceRow>(row.kind) && row.references == 3,
        "SourceRowOf projects a material source");
  const std::optional<SourceKind> back = SourceKindOf(row);
  const MaterialSource *material = back ? Get<MaterialSource>(*back) : nullptr;
  Check(material != nullptr && material->channel == MaterialChannel::kRoughness,
        "SourceKindOf reconstructs the material channel");
}

void TestSourceForm() {
  const Source source{"tex", ImageSource{}};
  const SourceRow row = SourceRowOf(source, 0);
  const std::vector<FormField> form = SourceForm(row, SignalNames{});
  Check(!form.empty() && form[0].name == "kind" &&
            form[0].kind == FieldKind::kChoice,
        "SourceForm opens with a kind choice");
  const FormField *path = Field(form, "path");
  const FormField *mirrorU = Field(form, "mirrorU");
  Check(path != nullptr && path->kind == FieldKind::kText,
        "an image source exposes its path");
  Check(mirrorU != nullptr && mirrorU->kind == FieldKind::kToggle,
        "an image source exposes a mirror toggle");
  Check(path != nullptr && path->bind &&
            std::holds_alternative<SetSource>(*path->bind("textures/a.dds")),
        "editing an image field emits a source edit");
}

void TestLightAndShellRows() {
  Recipe recipe;
  LightOutput light;
  light.intensity = 2.0f;
  light.replace = true;
  light.selector.anyOf.push_back(
      {SelectorKind::kAddon, FormRef::From("ArmorAddon")});
  recipe.outputs.push_back(SurfaceOutput{});
  recipe.outputs.push_back(light);
  recipe.shell.material = ShellMaterial::kVanilla;

  const LightRow lightRow = LightRowOf(recipe);
  Check(lightRow.present && lightRow.output == 1 && lightRow.replace,
        "LightRowOf finds the light output and its replace flag");
  Check(lightRow.selection == light.selector,
        "LightRowOf preserves the typed form selector for editing");
  const ShellRow shellRow = ShellRowOf(recipe);
  Check(shellRow.material == ShellMaterial::kVanilla,
        "ShellRowOf reads the shell material");

  const std::vector<FormField> lightForm = LightForm(lightRow, SignalNames{});
  Check(Field(lightForm, "replace") != nullptr &&
            Field(lightForm, "color") != nullptr,
        "LightForm exposes colour and the replace toggle");
  const std::vector<FormField> shellForm = ShellForm(shellRow, SignalNames{});
  Check(Field(shellForm, "material") != nullptr &&
            Field(shellForm, "spinAxis") != nullptr,
        "ShellForm exposes the full pose surface");
  recipe.outputs.push_back(light);
  const auto secondForm = LightForm(LightRowOf(recipe, 2), SignalNames{});
  const FormField *intensity = Field(secondForm, "intensity");
  const auto edit =
      intensity && intensity->bind ? intensity->bind("7") : std::nullopt;
  const auto *change = edit ? Get<SetLightParam>(*edit) : nullptr;
  Check(change && change->output == 2,
        "editing a second light targets its authored output rather than the "
        "first light");
  Check(!LightRowOf(recipe, 0).present && !LightRowOf(recipe, 99).present,
        "non-light and missing output indices cannot become light inspectors");
}

void TestRecipeHeaderForm() {
  RecipeRow recipe = SampleRecipe();
  recipe.priority = 40;
  recipe.clockSpeed = 2.0f;
  const std::vector<FormField> form = RecipeHeaderForm(recipe);
  const FormField *priority = Field(form, "priority");
  const FormField *clock = Field(form, "clockSpeed");
  Check(priority != nullptr && priority->allowEmpty && priority->text == "40" &&
            priority->bind &&
            std::holds_alternative<SetPriority>(*priority->bind("50")),
        "RecipeHeaderForm exposes an editable priority bound to SetPriority");
  Check(clock != nullptr && clock->bind &&
            std::holds_alternative<SetClockSpeed>(*clock->bind("3")),
        "RecipeHeaderForm exposes a clock speed bound to SetClockSpeed");
}

void TestOutputHeaderForm() {
  Selector selector;
  SelectorClause clause;
  clause.kind = SelectorKind::kGeometry;
  clause.operand = std::string{"Body*"};
  selector.anyOf.push_back(clause);

  const OutputHeader header = OutputHeaderForm(2, true, selector);
  const FormField *replace = Field(header.fields, "replace");
  Check(
      replace != nullptr && replace->kind == FieldKind::kToggle &&
          replace->text == "on" && replace->bind &&
          std::holds_alternative<SetOutputReplace>(*replace->bind("off")),
      "OutputHeaderForm exposes the replace toggle bound to SetOutputReplace");
  Check(!header.selector.matchAll && header.selector.clauses.size() == 1 &&
            header.selector.clauses[0].value == "Body*",
        "OutputHeaderForm surfaces the selector as an editable clause view");
}

void TestInspectorForm() {
  Inspector inspector;
  inspector.output = 0;
  inspector.layer = 1;
  inspector.row.source = "@base";
  inspector.row.opacityText = "1";
  inspector.sources = {"base"};
  const std::vector<FormField> form = InspectorForm(inspector);
  Check(form.size() == 7, "InspectorForm builds the seven layer fields");
  const FormField *source = Field(form, "source");
  Check(source != nullptr && source->kind == FieldKind::kLayerSource &&
            !source->creators.empty(),
        "the source field offers resource creators");
}

void TestScalarForm() {
  LayerStack stack;
  stack.output = 0;
  ScalarRow strength;
  strength.name = "strength";
  strength.value = 1.0f;
  strength.text = "1";
  stack.scalars = {strength};
  const std::vector<FormField> form = ScalarForm(stack);
  Check(form.size() == 1 && form.front().name == "strength" &&
            form.front().kind == FieldKind::kScalar,
        "ScalarForm builds a field per slot scalar");
  Check(!form.front().value.has_value(),
        "ScalarForm omits the value so its rows match other field rows");
}

void TestStackAndInspectorViews() {
  RecipeRow recipe = SampleRecipe();
  GeometryRow geometry;
  geometry.name = "body";
  OutputRow output;
  output.index = 0;
  output.target = Target::kMaterial;
  output.surface = Surface::kMaterial;
  output.slot = Slot::kEmissive;
  output.layers = {LayerRow{}, LayerRow{}};
  geometry.outputs = {output};
  recipe.geometries = {geometry};

  PieceRow piece;
  piece.recipes = {recipe};

  Selection selection;
  selection.recipeID = recipe.id;
  selection.target = Target::kMaterial;
  selection.slot = Slot::kEmissive;
  selection.layer = 0;

  const std::optional<LayerStack> stack =
      BuildStackView({.piece = piece,
                      .recipe = recipe,
                      .geometry = geometry,
                      .selection = selection,
                      .view = View{}});
  Check(stack.has_value() && stack->rows.size() == 2,
        "BuildStackView projects the selected output's layers");

  const std::optional<Inspector> inspector =
      BuildInspector(recipe, geometry, selection);
  Check(inspector.has_value() && inspector->layer == 0,
        "BuildInspector opens the selected layer");

  recipe.outputs = {output};
  recipe.geometries.clear();
  selection.subject = LayerSubject{0, 1};
  selection.layer = 1;
  const auto documentStack = BuildStackView(recipe, selection, View{});
  const auto documentInspector = BuildInspector(recipe, selection);
  Check(documentStack && documentStack->rows.size() == 2 &&
            documentStack->composite == TextureHandle{} &&
            documentStack->below.empty() && documentStack->above.empty(),
        "authored stack inspection needs no live geometry or foreign "
        "contributions");
  Check(documentInspector && documentInspector->layer == 1 &&
            !documentInspector->source && !documentInspector->mask,
        "authored layer fields are editable without fabricated image previews");
  if (documentInspector) {
    Check(documentInspector->sources.size() == recipe.sourceRows.size() &&
              documentInspector->masks.size() == recipe.maskRows.size(),
          "document inspector choices come from authored resource definitions");
  }
  selection.subject = LayerSubject{9, 1};
  Check(!BuildInspector(recipe, selection) &&
            !BuildStackView(recipe, selection, View{}),
        "missing authored outputs cannot borrow a live slot match");
}

void TestIntegerSignalFields() {
  SignalRow noise;
  noise.name = "noise";
  noise.kind = SignalKindId::kNoise;
  noise.definition = NoiseSignal{};
  const auto form = SignalForm(noise, SignalNames{});
  const auto *seed = Field(form, "seed");
  Check(seed && seed->bind, "noise exposes a seed binding");
  if (!seed || !seed->bind)
    return;
  for (const auto &[text, expected] :
       std::vector<std::pair<std::string, std::uint32_t>>{
           {"16777217", 16777217u}, {"4294967295", 4294967295u}, {"0", 0u}}) {
    const auto edit = seed->bind(text);
    const auto *record = edit ? std::get_if<SetSignal>(&*edit) : nullptr;
    const auto *value =
        record ? std::get_if<NoiseSignal>(&record->kind) : nullptr;
    Check(value && value->seed == expected,
          "integer seeds preserve every bit: " + text);
  }
  for (const char *text : {"1.5", "-1", "4294967296", "1e30"}) {
    Check(!seed->bind(text),
          "invalid integer seed is refused: " + std::string{text});
  }
}

void TestHelpers() {
  const auto colour = LiteralColor("1, 0.5, 0.25");
  Check(colour.has_value() && test::Near(colour->y, 0.5f),
        "LiteralColor parses a three-part literal");
  Check(!LiteralColor("@signal").has_value(),
        "LiteralColor rejects a reference");
  const auto edit = SignalValueEdit("glow", "0.5");
  Check(edit.has_value() && std::holds_alternative<SetConstant>(*edit),
        "SignalValueEdit makes a scalar constant");
}
}

int main() {
  TestSignalNames();
  TestSignalFormConstant();
  TestSignalFormPulse();
  TestSourceRoundTrip();
  TestSourceForm();
  TestLightAndShellRows();
  TestRecipeHeaderForm();
  TestOutputHeaderForm();
  TestInspectorForm();
  TestScalarForm();
  TestStackAndInspectorViews();
  TestIntegerSignalFields();
  TestHelpers();
  return test::Finish("studio_panels");
}
