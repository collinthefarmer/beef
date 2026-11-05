// GPL-3.0-only with the additional permission in COPYING.md.
#include "recipe/Recipe.h"
#include "studio/Create.h"
#include "studio/Edits.h"
#include "studio/Fields.h"
#include "studio/Forms.h"
#include "studio/Panels.h"
#include "studio/Rows.h"
#include "test_support.h"

#include <algorithm>
#include <format>
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
const FormField *Field(std::vector<FormField> &&, const std::string &) = delete;

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

void TestSignalFormWave() {
  SignalRow wave;
  wave.name = "beat";
  wave.kind = SignalKindId::kWave;
  wave.type = ValueType::kScalar;
  wave.definition = WaveSignal{};
  const std::vector<FormField> form = SignalForm(wave, SignalNames{});
  const FormField *base = Field(form, "base");
  const FormField *waveform = Field(form, "waveform");
  Check(base != nullptr && base->kind == FieldKind::kScalar,
        "wave exposes a scalar base field");
  Check(waveform != nullptr && waveform->kind == FieldKind::kChoice,
        "wave exposes a waveform choice");
  Check(base != nullptr && base->bind &&
            std::holds_alternative<SetSignal>(*base->bind("2")),
        "a wave member binding edits the signal record");
  Check(base != nullptr && base->bind && !base->bind("not a number"),
        "a wave member binding refuses unparsable text");
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

void TestClusterPickRoundTrip() {
  MaterialGroupsSource clusters;
  clusters.pick = Param{Ref{"which"}};
  clusters.areaBias = Param{0.5f};
  const SourceRow row = SourceRowOf(Source{"zone", clusters}, 0);
  const std::optional<SourceKind> back = SourceKindOf(row);
  const auto *picked = back ? Get<MaterialGroupsSource>(*back) : nullptr;
  Check(picked && picked->pick == clusters.pick &&
            picked->areaBias == clusters.areaBias,
        "a cluster pick and area bias survive the studio row");
  const std::vector<FormField> form = SourceForm(row, SignalNames{});
  const auto *field = Field(form, "pick");
  Check(field && field->allowEmpty, "the pick field may be left empty");
  MaterialGroupsSourceRow tooBiased =
      Get<MaterialGroupsSourceRow>(row.kind)
          ? *Get<MaterialGroupsSourceRow>(row.kind)
          : MaterialGroupsSourceRow{};
  tooBiased.areaBias = "9";
  SourceRow bad = row;
  bad.kind = tooBiased;
  Check(!SourceKindOf(bad), "an area bias above eight is refused");
}

std::optional<PickSource> PickOf(const std::optional<RecipeEdit> &a_edit) {
  const auto *set = a_edit ? std::get_if<SetSource>(&*a_edit) : nullptr;
  const auto *pick = set ? Get<PickSource>(set->kind) : nullptr;
  return pick ? std::optional{*pick} : std::nullopt;
}

void TestPickForm() {
  PickSource full;
  full.seed = Param{Ref{"seed"}};
  full.levels = {GeometryLevel{Param{2.0f}}, GroupLevel{Param{0.5f}, {0.4f, 6}},
                 ChartLevel{Param{Ref{"count"}}, 0.1f, Param{0.3f}}};
  const SourceRow fullRow = SourceRowOf(Source{"lit", full}, 0);
  const std::optional<SourceKind> back = SourceKindOf(fullRow);
  Check(back && *back == SourceKind{full},
        "every pick level survives the studio row");

  const SourceRow groupRow = SourceRowOf(Source{"lit", PickSource{}}, 0);
  const std::vector<FormField> form = SourceForm(groupRow, SignalNames{});
  Check(Field(form, "geometry level") && Field(form, "chart level") &&
            !Field(form, "geometries") && !Field(form, "charts") &&
            Field(form, "merge"),
        "a pick shows the fields of the levels it has");

  const FormField *chartLevel = Field(form, "chart level");
  const std::optional<PickSource> withCharts =
      chartLevel ? PickOf(chartLevel->bind("on")) : std::nullopt;
  Check(withCharts && withCharts->levels.size() == 2 &&
            Is<GroupLevel>(withCharts->levels[0]) &&
            Is<ChartLevel>(withCharts->levels[1]),
        "turning the chart level on appends it after the groups");

  const SourceRow chartRow =
      SourceRowOf(Source{"lit", withCharts ? *withCharts : PickSource{}}, 0);
  const std::vector<FormField> chartForm = SourceForm(chartRow, SignalNames{});
  const FormField *geometryLevel = Field(chartForm, "geometry level");
  const std::optional<PickSource> allLevels =
      geometryLevel ? PickOf(geometryLevel->bind("on")) : std::nullopt;
  Check(allLevels && SupportedLevels(*allLevels) &&
            allLevels->levels.size() == 3 &&
            Is<GeometryLevel>(allLevels->levels[0]),
        "turning the geometry level on puts it first");

  const FormField *charts = Field(chartForm, "charts");
  const std::optional<PickSource> threeCharts =
      charts ? PickOf(charts->bind("3")) : std::nullopt;
  const auto *chart = threeCharts && threeCharts->levels.size() == 2
                          ? Get<ChartLevel>(threeCharts->levels[1])
                          : nullptr;
  Check(chart && chart->count == Param{3.0f},
        "editing the chart count changes the chart level");
  const FormField *minShare = Field(chartForm, "minShare");
  const std::optional<PickSource> floored =
      minShare ? PickOf(minShare->bind("0.2")) : std::nullopt;
  const auto *flooredChart = floored && floored->levels.size() == 2
                                 ? Get<ChartLevel>(floored->levels[1])
                                 : nullptr;
  Check(flooredChart && flooredChart->minShare == Param{0.2f} && minShare &&
            !minShare->bind("1.5"),
        "the chart floor is editable and bounded to 0..1");
  const FormField *merge = Field(chartForm, "merge");
  Check(merge && !merge->bind("2"), "a merge above one is refused");
  const FormField *off = Field(chartForm, "chart level");
  const std::optional<PickSource> withoutCharts =
      off ? PickOf(off->bind("off")) : std::nullopt;
  Check(withoutCharts && withoutCharts->levels == PickSource{}.levels,
        "turning the chart level off removes it");
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

  const std::optional<RecipeEdit> switched = form[0].bind("material groups");
  const auto *set = switched ? std::get_if<SetSource>(&*switched) : nullptr;
  const auto *clusters =
      set ? std::get_if<MaterialGroupsSource>(&set->kind) : nullptr;
  Check(clusters && clusters->settings == ClusterSettings{},
        "switching a source to material groups starts from the defaults");
}

void TestEffectShaderImageForm() {
  const Source source{
      "fill", ImageSource{EffectShaderImage{EffectShaderTexture::kMembrane}}};
  const SourceRow row = SourceRowOf(source, 0);
  const std::optional<SourceKind> back = SourceKindOf(row);
  Check(back && *back == source.kind,
        "an effect-shader image round-trips through its row");
  const std::vector<FormField> form = SourceForm(row, SignalNames{});
  const FormField *shader = Field(form, "effectShader");
  Check(shader != nullptr && shader->kind == FieldKind::kChoice &&
            shader->text == "membrane" && Field(form, "path") == nullptr,
        "an effect-shader image shows its texture choice in place of a path");
  const auto edit =
      shader && shader->bind ? shader->bind("fill") : std::nullopt;
  const auto *set = edit ? Get<SetSource>(*edit) : nullptr;
  const auto *image = set ? Get<ImageSource>(set->kind) : nullptr;
  Check(image && image->texture == ImageTexture{EffectShaderImage{
                                       EffectShaderTexture::kFill}},
        "choosing the fill texture edits the effect-shader image");
}

void TestMatchedEfshForm() {
  SignalRow efsh;
  efsh.name = "glow";
  efsh.kind = SignalKindId::kEfsh;
  efsh.type = ValueType::kScalar;
  efsh.definition = EfshSignal{EfshField::kFillAlpha, std::nullopt};
  const std::vector<FormField> form = SignalForm(efsh, SignalNames{});
  const FormField *record = Field(form, "record");
  Check(record != nullptr && record->text.empty() && record->allowEmpty &&
            !record->help.empty(),
        "an efsh signal without a record shows an empty record that reads "
        "the matched effect");
  const auto named =
      record && record->bind ? record->bind("0x1~A.esp") : std::nullopt;
  const auto *setNamed = named ? Get<SetSignal>(*named) : nullptr;
  const auto *withRecord = setNamed ? Get<EfshSignal>(setNamed->kind) : nullptr;
  Check(withRecord && withRecord->record &&
            withRecord->record->text == "0x1~A.esp",
        "typing a form names the efsh record");
  const auto cleared = record && record->bind ? record->bind("") : std::nullopt;
  const auto *setCleared = cleared ? Get<SetSignal>(*cleared) : nullptr;
  const auto *matched =
      setCleared ? Get<EfshSignal>(setCleared->kind) : nullptr;
  Check(matched && !matched->record,
        "clearing the record returns the efsh signal to the matched effect");
}

void TestSourceFormLifetime() {
  for (int iteration = 0; iteration < 64; ++iteration) {
    FieldBinding retained;
    const std::string name(128, 's');
    {
      ImageSource image;
      image.texture = ImageFile{std::string(2048, 'p')};
      image.mirror[0] = true;
      std::vector<FormField> copied;
      {
        const auto original = SourceForm(
            SourceRowOf(Source{.name = name, .kind = image, .note = {}}, 0),
            SignalNames{});
        copied = original;
      }
      auto moved = std::move(copied);
      const auto *path = Field(moved, "path");
      Check(path && path->bind,
            "copied and moved image forms own their bindings");
      if (!path || !path->bind)
        return;
      retained = path->bind;
    }
    const auto edit = retained("textures/retained.dds");
    const auto *set = edit ? Get<SetSource>(*edit) : nullptr;
    const auto *image = set ? Get<ImageSource>(set->kind) : nullptr;
    Check(set && set->name == name && image && image->mirror[0] &&
              image->texture ==
                  ImageTexture{ImageFile{"textures/retained.dds"}},
          "image binding owns its source record after rows and forms are "
          "destroyed");
  }
}

void TestIncompleteRippleForm() {
  const Source source{"wave", RippleSource{}};
  const SourceRow row = SourceRowOf(source, 0);
  const std::optional<SourceKind> back = SourceKindOf(row);
  const RippleSource *ripple = back ? Get<RippleSource>(*back) : nullptr;
  Check(ripple != nullptr && ripple->trigger == Ref{},
        "SourceKindOf reconstructs a ripple with no trigger yet");
  const std::vector<FormField> form = SourceForm(row, SignalNames{});
  const FormField *trigger = Field(form, "trigger");
  Check(trigger != nullptr && trigger->kind == FieldKind::kReference,
        "an incomplete ripple still exposes its trigger field");
  Check(trigger != nullptr && trigger->bind && !trigger->bind("@"),
        "the trigger binding refuses an empty reference");
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

  OutputRow output;
  output.index = 2;
  output.replace = true;
  output.selection = selector;
  output.slot = Slot::kEmissive;
  const OutputHeader header = OutputHeaderForm(output);
  const FormField *resolution = Field(header.fields, "resolution");
  Check(resolution != nullptr && resolution->text == "default" &&
            resolution->bind,
        "an output without a resolution shows the slot default");
  const std::optional<RecipeEdit> half =
      resolution ? resolution->bind("half") : std::nullopt;
  const auto *setHalf = half ? Get<SetOutputResolution>(*half) : nullptr;
  const std::optional<RecipeEdit> reset =
      resolution ? resolution->bind("default") : std::nullopt;
  const auto *setDefault = reset ? Get<SetOutputResolution>(*reset) : nullptr;
  Check(setHalf && setHalf->output == 2 &&
            setHalf->resolution == std::optional{Resolution::kHalf} &&
            setDefault && !setDefault->resolution.has_value() &&
            !resolution->bind("tiny"),
        "the resolution choice sets, clears or refuses an output's resolution");
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

void TestCreatedMaskUnwires() {
  Recipe recipe;
  recipe.id = "sample";
  recipe.sources = {
      Source{"base", MaterialSource{MaterialChannel::kRoughness}}};
  SurfaceOutput surface;
  Layer layer;
  layer.source = Ref{"base"};
  surface.stack = {layer};
  recipe.outputs.push_back(surface);
  Inspector inspector;
  inspector.output = 0;
  inspector.layer = 0;
  inspector.row.source = "@base";
  inspector.row.opacityText = "1";
  inspector.sources = {"base"};
  const std::vector<FormField> form = InspectorForm(inspector);
  RecipeRow row;
  row.id = recipe.id;
  for (const char *name : {"mask", "source"}) {
    const FormField *field = Field(form, name);
    const std::optional<Created> created =
        field && field->create ? field->create("new mask", row) : std::nullopt;
    const auto *mask = created ? Get<MaskSubject>(created->subject) : nullptr;
    Check(mask != nullptr && !created->unwire.empty(),
          std::format("a mask made from the {} field records how to unwire it",
                      name));
    if (!mask) {
      continue;
    }
    const auto made = PrepareEdits(recipe, EditBatch{created->edits});
    std::vector<RecipeEdit> discard = created->unwire;
    discard.emplace_back(RemoveMask{.name = mask->name});
    const auto restored = made ? PrepareEdits(*made, EditBatch{discard}) : made;

    Check(made && restored && *restored == recipe,
          std::format("discarding a mask made from the {} field restores the "
                      "layer as it was",
                      name));
  }
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

  LayerStack glint;
  for (const char *name :
       {"screenSpaceScale", "logMicrofacetDensity", "densityRandomization"}) {
    ScalarRow row;
    row.name = name;
    row.text = "1";
    glint.scalars.push_back(row);
  }
  const std::vector<FormField> glintForm = ScalarForm(glint);
  Check(glintForm.size() == 3 &&
            glintForm[0].workingRange == std::pair{0.0f, 3.0f} &&
            glintForm[1].workingRange == std::pair{0.0f, 40.0f} &&
            glintForm[2].workingRange == std::pair{0.0f, 5.0f},
        "glint parameters slide over the ranges Community Shaders uses");
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

void TestEventFilterFields() {
  SignalRow hit;
  hit.name = "hit";
  hit.kind = SignalKindId::kTrigger;
  TriggerSignal trigger;
  trigger.origin = EventOrigin{"hit.received", {"*Foot*", "", {0.5f, {}}}};
  hit.definition = trigger;
  const auto form = SignalForm(hit, SignalNames{});
  const auto *node = Field(form, "filter node");
  const auto *arg = Field(form, "filter arg");
  const auto *min = Field(form, "filter min");
  const auto *max = Field(form, "filter max");
  Check(node && arg && min && max,
        "an event trigger exposes all four filter fields");
  if (!node || !arg || !min || !max)
    return;
  Check(node->text == "*Foot*" && min->text == "0.5" && max->text.empty(),
        "filter fields show the authored globs and bounds");
  {
    const auto edit = arg->bind("Left*");
    const auto *record = edit ? std::get_if<SetSignal>(&*edit) : nullptr;
    const auto *value =
        record ? std::get_if<TriggerSignal>(&record->kind) : nullptr;
    const auto *origin =
        value ? std::get_if<EventOrigin>(&value->origin) : nullptr;
    Check(origin && origin->filter.arg == "Left*",
          "the arg binding writes the filter's arg glob");
  }
  {
    const auto edit = node->bind("");
    const auto *record = edit ? std::get_if<SetSignal>(&*edit) : nullptr;
    const auto *value =
        record ? std::get_if<TriggerSignal>(&record->kind) : nullptr;
    const auto *origin =
        value ? std::get_if<EventOrigin>(&value->origin) : nullptr;
    Check(origin && origin->filter.node.empty(),
          "an empty node commit clears the glob");
  }
  {
    const auto edit = max->bind("2.5");
    const auto *record = edit ? std::get_if<SetSignal>(&*edit) : nullptr;
    const auto *value =
        record ? std::get_if<TriggerSignal>(&record->kind) : nullptr;
    const auto *origin =
        value ? std::get_if<EventOrigin>(&value->origin) : nullptr;
    Check(origin && origin->filter.value.max &&
              test::Near(*origin->filter.value.max, 2.5f) &&
              origin->filter.value.min &&
              test::Near(*origin->filter.value.min, 0.5f),
          "a bound commit writes one bound and keeps the other");
  }
  {
    const auto edit = min->bind("");
    const auto *record = edit ? std::get_if<SetSignal>(&*edit) : nullptr;
    const auto *value =
        record ? std::get_if<TriggerSignal>(&record->kind) : nullptr;
    const auto *origin =
        value ? std::get_if<EventOrigin>(&value->origin) : nullptr;
    Check(origin && !origin->filter.value.min,
          "an empty bound commit clears the bound");
  }
  Check(!min->bind("@glow"), "a bound refuses a reference");
  Check(!max->bind("wide"), "a bound refuses a word");
}

void TestOptionalFieldRefusals() {
  SignalRow count;
  count.name = "count";
  count.kind = SignalKindId::kCounter;
  count.definition = CounterSignal{Ref{"hit"}, Ref{"calm"}, 2.0f};
  const auto form = SignalForm(count, SignalNames{});
  const auto *cap = Field(form, "cap");
  const auto *reset = Field(form, "reset");
  Check(cap && cap->bind && reset && reset->bind,
        "a counter exposes cap and reset bindings");
  if (!cap || !reset)
    return;
  Check(!cap->bind("wide"), "an optional param refuses a word");
  {
    const auto edit = cap->bind("");
    const auto *record = edit ? std::get_if<SetSignal>(&*edit) : nullptr;
    const auto *value =
        record ? std::get_if<CounterSignal>(&record->kind) : nullptr;
    Check(value && !value->cap, "an empty cap commit clears the cap");
  }
  Check(!reset->bind("calm"), "an optional reference refuses a bare word");
  {
    const auto edit = reset->bind("");
    const auto *record = edit ? std::get_if<SetSignal>(&*edit) : nullptr;
    const auto *value =
        record ? std::get_if<CounterSignal>(&record->kind) : nullptr;
    Check(value && !value->reset, "an empty reset commit clears the reset");
  }
  const Source source{"tex", ImageSource{}};
  const SourceRow row = SourceRowOf(source, 0);
  const auto sourceForm = SourceForm(row, SignalNames{});
  const auto *scroll = Field(sourceForm, "scroll");
  Check(scroll && scroll->bind, "an image source exposes a scroll binding");
  if (!scroll || !scroll->bind)
    return;
  Check(!scroll->bind("wide, open"), "an optional vec2 refuses words");
  {
    const auto edit = scroll->bind("0.25, 4");
    const auto *record = edit ? std::get_if<SetSource>(&*edit) : nullptr;
    const auto *image =
        record ? std::get_if<ImageSource>(&record->kind) : nullptr;
    Check(image && image->scroll, "a vec2 commit writes the scroll");
  }
  {
    const auto edit = scroll->bind("");
    const auto *record = edit ? std::get_if<SetSource>(&*edit) : nullptr;
    const auto *image =
        record ? std::get_if<ImageSource>(&record->kind) : nullptr;
    Check(image && !image->scroll, "an empty scroll commit clears it");
  }
}

void TestClusterColor() {
  MaterialGroupsSource clusters;
  clusters.settings.merge = 0.4f;
  clusters.settings.smoothing = 5;
  const SourceRow row = SourceRowOf(Source{"clusters", clusters}, 0);
  const auto restored = SourceKindOf(row);
  Check(restored && *restored == SourceKind{clusters},
        "cluster source rows preserve merge and smoothing");
  const auto form = SourceForm(row, SignalNames{});
  const auto *merge = Field(form, "merge");
  Check(merge && merge->bind && merge->workingRange,
        "cluster sources expose a tunable merge share");
  if (merge && merge->bind) {
    const auto edit = merge->bind("0.2");
    const auto *set = edit ? Get<SetSource>(*edit) : nullptr;
    const auto *value = set ? Get<MaterialGroupsSource>(set->kind) : nullptr;
    Check(value && value->settings.merge == 0.2f &&
              value->settings.smoothing == 5,
          "editing the merge share keeps the smoothing");
    Check(!merge->bind("1.5"), "the merge share refuses values past one");
  }
  const auto *smoothing = Field(form, "smoothing");
  Check(smoothing && smoothing->integral && smoothing->bind &&
            !smoothing->bind("33"),
        "smoothing is a whole number up to its bound");
  const auto *palette = Field(form, "palette");
  Check(palette && palette->kind == FieldKind::kToggle,
        "a cluster source offers a palette toggle");
  Check(Field(form, "guess") == nullptr && Field(form, "metallic") == nullptr,
        "a cluster source has no guess or channel weights");
}

void TestHelpers() {
  const auto colour = LiteralColor("1, 0.5, 0.25");
  Check(colour.has_value() && test::Near(colour->y, 0.5f),
        "LiteralColor parses a three-part literal");
  Check(!LiteralColor("@signal").has_value(),
        "LiteralColor rejects a reference");
  const auto pair = SignalValueEdit("glow", "0.5, 0.25");
  const auto *pairConstant = pair ? Get<SetConstant>(*pair) : nullptr;
  Check(pairConstant && pairConstant->value == Value{Vec2{0.5f, 0.25f}},
        "SignalValueEdit makes a pair constant from the text it shows");
  const auto edit = SignalValueEdit("glow", "0.5");
  Check(edit.has_value() && std::holds_alternative<SetConstant>(*edit),
        "SignalValueEdit makes a scalar constant");
}
}

int main() {
  TestSignalNames();
  TestSignalFormConstant();
  TestSignalFormWave();
  TestSourceRoundTrip();
  TestClusterPickRoundTrip();
  TestPickForm();
  TestSourceForm();
  TestEffectShaderImageForm();
  TestMatchedEfshForm();
  TestSourceFormLifetime();
  TestIncompleteRippleForm();
  TestLightAndShellRows();
  TestRecipeHeaderForm();
  TestOutputHeaderForm();
  TestInspectorForm();
  TestCreatedMaskUnwires();
  TestScalarForm();
  TestStackAndInspectorViews();
  TestIntegerSignalFields();
  TestEventFilterFields();
  TestOptionalFieldRefusals();
  TestClusterColor();
  TestHelpers();
  return test::Finish("studio_panels");
}
