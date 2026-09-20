#include "studio/Create.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
RecipeRow Document() {
  RecipeRow recipe;
  recipe.id = "glow";
  OutputRow output;
  output.index = 3;
  output.slot = Slot::kEmissive;
  output.layers.resize(2);
  recipe.outputs.push_back(output);
  OutputRow light;
  light.index = 5;
  light.target = Target::kLight;
  recipe.outputs.push_back(light);
  SignalRow signal;
  signal.name = "strength";
  recipe.signals.push_back(signal);
  SourceRow source;
  source.name = "pattern";
  recipe.sourceRows.push_back(source);
  recipe.maskRows.push_back(TextRow{.name = "spine", .text = "1"});
  recipe.curves.push_back(TextRow{.name = "response", .text = "x"});
  return recipe;
}
}

int main() {
  const RecipeRow recipe = Document();
  {
    const Created made =
        Create(NewOutput{Surface::kMaterial, Slot::kEmissive, {}}, recipe);
    const auto *edit =
        made.edits.size() == 1 ? Get<AddOutput>(made.edits[0]) : nullptr;
    Check(edit && edit->slot == Slot::kEmissive &&
              made.subject == InspectorSubject{OutputSubject{2}},
          "a new output appends and focuses the next index");
  }
  {
    const Created made = Create(NewLight{}, recipe);
    Check(made.edits.size() == 1 && Is<AddLight>(made.edits[0]) &&
              made.subject == InspectorSubject{OutputSubject{2}},
          "a new light appends and focuses the next output index");
  }
  {
    const Created made = Create(NewLayer{3}, recipe);
    const auto *edit =
        made.edits.size() == 1 ? Get<AddLayer>(made.edits[0]) : nullptr;
    Check(edit && edit->output == 3 &&
              edit->at == std::optional<std::size_t>{2} &&
              made.subject == InspectorSubject{LayerSubject{3, 2}},
          "a new layer appends to its output's stack and focuses it");
  }
  {
    const Created made = Create(NewSignal{"strength"}, recipe);
    const auto *edit =
        made.edits.size() == 1 ? Get<AddSignal>(made.edits[0]) : nullptr;
    Check(edit && edit->name == "strength2" &&
              made.subject == InspectorSubject{SignalSubject{"strength2"}},
          "a new signal uniquifies its stem against existing signals");
  }
  {
    const Created signal = Create(NewSignal{"pattern"}, recipe);
    const auto *sig = Get<AddSignal>(signal.edits.at(0));
    const Created curve = Create(NewCurve{"pattern"}, recipe);
    const auto *cur = Get<AddCurve>(curve.edits.at(0));
    Check(sig && sig->name == "pattern2" && cur && cur->name == "pattern2",
          "a new name avoids every other kind — signals, sources, masks, "
          "curves share one namespace");
  }
  {
    const Created made = Create(NewSignal{"trigger", TriggerSignal{}}, recipe);
    const auto *added =
        made.edits.size() == 2 ? Get<AddSignal>(made.edits[0]) : nullptr;
    const auto *set =
        made.edits.size() == 2 ? Get<SetSignal>(made.edits[1]) : nullptr;
    Check(added && added->name == "trigger" && set &&
              set->signal == "trigger" && Is<TriggerSignal>(set->kind) &&
              made.subject == InspectorSubject{SignalSubject{"trigger"}},
          "a new non-constant signal adds then sets its kind");
  }
  {
    const Created made = Create(NewSource{"pattern", MaterialSource{}}, recipe);
    const auto *edit =
        made.edits.size() == 1 ? Get<AddSource>(made.edits[0]) : nullptr;
    Check(edit && edit->name == "pattern2" &&
              made.subject == InspectorSubject{SourceSubject{"pattern2"}},
          "a new source uniquifies its stem");
  }
  {
    const Created made = Create(NewMask{"pattern"}, recipe);
    const auto *edit =
        made.edits.size() == 1 ? Get<AddMask>(made.edits[0]) : nullptr;
    Check(edit && edit->name == "pattern2" &&
              made.subject == InspectorSubject{MaskSubject{"pattern2"}},
          "a new mask shares the source namespace when uniquifying");
  }
  {
    const Created made = Create(NewCurve{"response"}, recipe);
    const auto *edit =
        made.edits.size() == 1 ? Get<AddCurve>(made.edits[0]) : nullptr;
    Check(edit && edit->name == "response2" &&
              made.subject == InspectorSubject{CurveSubject{"response2"}},
          "a new curve uniquifies its stem against existing curves");
  }
  {
    Check(
        ResourceName(SignalSubject{"s"}) == std::optional<std::string>{"s"} &&
            ResourceName(SourceSubject{"r"}) ==
                std::optional<std::string>{"r"} &&
            ResourceName(MaskSubject{"m"}) == std::optional<std::string>{"m"} &&
            ResourceName(CurveSubject{"c"}) == std::optional<std::string>{"c"},
        "ResourceName reads the name of a name-keyed subject");
    Check(!ResourceName(OutputSubject{2}) &&
              !ResourceName(LayerSubject{3, 2}) &&
              !ResourceName(RecipeSubject{}),
          "ResourceName is empty for index-keyed and recipe subjects");
  }
  return test::Finish("studio_create");
}
