#include "recipe/Expression.h"
#include "recipe/Words.h"
#include "test_support.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Equal;

namespace {
using json = nlohmann::json;

json LoadSchema() {
  const std::filesystem::path path =
      test::Fixtures().parent_path().parent_path() / "schema" /
      "recipe.schema.json";
  return json::parse(test::ReadFile(path), nullptr, false);
}

const json *At(const json &a_root, std::string_view a_pointer) {
  const json::json_pointer pointer{std::string{a_pointer}};
  return a_root.contains(pointer) ? &a_root.at(pointer) : nullptr;
}

std::vector<std::string> Words(const json *a_array) {
  std::vector<std::string> words;
  if (a_array && a_array->is_array()) {
    for (const json &word : *a_array) {
      if (word.is_string()) {
        words.push_back(word.get<std::string>());
      }
    }
  }
  return words;
}

std::string Joined(const std::vector<std::string> &a_words) {
  std::string out;
  for (const std::string &word : a_words) {
    out += out.empty() ? word : ", " + word;
  }
  return out;
}

void EnumMatches(const json &a_schema, std::string_view a_pointer,
                 const std::vector<std::string> &a_table,
                 std::string_view a_what) {
  const std::vector<std::string> words = Words(At(a_schema, a_pointer));
  Check(!words.empty(), std::string{a_what} + ": schema enum found at " +
                            std::string{a_pointer});
  Equal(Joined(words), Joined(a_table), std::string{a_what} + " enum");
}

std::vector<std::string> Sorted(std::vector<std::string> a_words) {
  std::ranges::sort(a_words);
  return a_words;
}

const json *KindEntry(const json &a_schema, std::string_view a_section,
                      std::string_view a_word) {
  const json *alternatives =
      At(a_schema, "/$defs/" + std::string{a_section} + "/oneOf");
  if (!alternatives || !alternatives->is_array()) {
    return nullptr;
  }
  for (const json &alternative : *alternatives) {
    const std::vector<std::string> required =
        Words(alternative.contains("required") ? &alternative.at("required")
                                               : nullptr);
    if (required.size() == 1 && required.front() == a_word) {
      return &alternative;
    }
  }
  return nullptr;
}

std::vector<std::string> KindWords(const json &a_schema,
                                   std::string_view a_section) {
  std::vector<std::string> words;
  const json *alternatives =
      At(a_schema, "/$defs/" + std::string{a_section} + "/oneOf");
  if (!alternatives || !alternatives->is_array()) {
    return words;
  }
  for (const json &alternative : *alternatives) {
    const std::vector<std::string> required =
        Words(alternative.contains("required") ? &alternative.at("required")
                                               : nullptr);
    if (required.size() == 1) {
      words.push_back(required.front());
    }
  }
  return words;
}

void SourceKindsMatchTheTable(const json &a_schema) {
  Equal(Joined(KindWords(a_schema, "source")),
        Joined(WordsOf(kSourceKindWords)), "source kinds");
  Equal(Joined(KindWords(a_schema, "signal")), Joined(WordsOf(kSignalKinds)),
        "signal kinds by required key");
  std::vector<std::string> propertyNames =
      Words(At(a_schema, "/$defs/signalKind/propertyNames/enum"));
  std::erase(propertyNames, "curve");
  std::erase(propertyNames, "note");
  Equal(Joined(propertyNames), Joined(WordsOf(kSignalKinds)),
        "signal kinds by property name");
}

void SignalEnumsMatchTheTables(const json &a_schema) {
  EnumMatches(a_schema,
              "/$defs/signal/oneOf/1/properties/wave/properties/waveform/enum",
              WordsOf(kWaveforms), "waveform");
  EnumMatches(a_schema,
              "/$defs/signal/oneOf/3/properties/efsh/properties/field/enum",
              WordsOf(kEfshFields), "efsh field");
  EnumMatches(a_schema,
              "/$defs/signal/oneOf/4/properties/av/oneOf/1/properties/"
              "measure/enum",
              WordsOf(kMeasures), "measure");
  EnumMatches(a_schema, "/$defs/signal/oneOf/5/properties/actorState/enum",
              WordsOf(kActorStates), "actor state");
  EnumMatches(a_schema, "/$defs/signal/oneOf/6/properties/enchantment/enum",
              WordsOf(kEnchantmentFields), "enchantment field");
  EnumMatches(a_schema, "/$defs/trigger/properties/payload/enum",
              WordsOf(kValueTypes), "trigger payload type");
  Check(KindEntry(a_schema, "signal", "wave") ==
            At(a_schema, "/$defs/signal/oneOf/1"),
        "signal alternatives sit at the table's index");
}

void SourceEnumsMatchTheTables(const json &a_schema) {
  EnumMatches(a_schema,
              "/$defs/source/oneOf/0/properties/image/properties/channel/enum",
              WordsOf(kImageChannels), "image channel");
  EnumMatches(a_schema,
              "/$defs/source/oneOf/0/properties/image/properties/space/enum",
              WordsOf(kImageSpaces), "image space");
  EnumMatches(a_schema, "/$defs/source/oneOf/1/properties/material/enum",
              WordsOf(kMaterialChannels), "material channel");
  EnumMatches(a_schema,
              "/$defs/source/oneOf/4/properties/ripple/properties/shape/enum",
              WordsOf(kRippleShapes), "ripple shape");
  std::vector<std::string> bakes =
      Words(At(a_schema, "/$defs/source/oneOf/2/properties/bake/oneOf/0/enum"));
  bakes.emplace_back("partition");
  bakes.emplace_back("boneWeight");
  Equal(Joined(Sorted(bakes)), Joined(Sorted(WordsOf(kBakeKindWords))),
        "bake kinds");
  Check(KindEntry(a_schema, "source", "ripple") ==
            At(a_schema, "/$defs/source/oneOf/4"),
        "source alternatives sit at the table's index");
}

void RecipeShapeMatchesTheTables(const json &a_schema) {
  std::vector<std::string> keys = KindWords(a_schema, "key");
  keys.emplace_back("default");
  keys.emplace_back("enchanted");
  Equal(Joined(Sorted(keys)), Joined(Sorted(WordsOf(kKeyKinds))), "key kinds");
  Equal(Joined(KindWords(a_schema, "selectorTerm")),
        Joined(WordsOf(kSelectorKinds)), "selector kinds");
  Equal(Joined(KindWords(a_schema, "trigger")),
        Joined(WordsOf(kTriggerOriginWords)), "trigger origins");
  const std::vector<std::string> partitions = Words(
      At(a_schema, "/$defs/source/oneOf/2/properties/bake/oneOf/1/properties/"
                   "partition/oneOf/0/enum"));
  Check(!partitions.empty(), "the partition name enum exists");
  Equal(Joined(Sorted(partitions)), Joined(Sorted(WordsOf(kBipedSlots))),
        "partition names");

  EnumMatches(a_schema, "/properties/merge/enum", WordsOf(kMergeModes),
              "merge mode");
}

void OutputScalarsMatchTheTable(const json &a_schema) {
  const json *properties = At(a_schema, "/$defs/output/oneOf/0/properties");
  Check(properties != nullptr && properties->is_object(),
        "the material output's properties exist");
  std::vector<std::string> scalars;
  if (properties != nullptr && properties->is_object()) {
    for (const auto &[name, value] : properties->items()) {
      if (name != "target" && name != "slot" && name != "selector" &&
          name != "replace" && name != "resolution" && name != "stack" &&
          name != "note") {
        scalars.push_back(name);
      }
    }
  }
  Equal(Joined(Sorted(scalars)), Joined(Sorted(WordsOf(kScalarFields))),
        "output scalar fields");
}

void OutputEnumsMatchTheTables(const json &a_schema) {
  EnumMatches(a_schema, "/$defs/layer/properties/blend/enum", WordsOf(kBlends),
              "blend");
  EnumMatches(a_schema, "/$defs/output/oneOf/0/properties/target/enum",
              WordsOf(kSurfaces), "surface");
  EnumMatches(a_schema, "/$defs/output/oneOf/0/properties/slot/enum",
              WordsOf(kSlots), "slot");
  EnumMatches(a_schema, "/$defs/output/oneOf/0/properties/resolution/enum",
              WordsOf(kResolutions), "resolution");
  EnumMatches(a_schema, "/$defs/shell/properties/material/enum",
              WordsOf(kShellMaterials), "shell material");
  EnumMatches(a_schema, "/$defs/shell/properties/blend/enum",
              WordsOf(kShellBlends), "shell blend");
}

bool ContainsWord(std::string_view a_text, std::string_view a_word) {
  std::size_t at = a_text.find(a_word);
  while (at != std::string_view::npos) {
    const bool startsClean =
        at == 0 || !std::isalnum(static_cast<unsigned char>(a_text[at - 1]));
    const std::size_t end = at + a_word.size();
    const bool endsClean =
        end >= a_text.size() ||
        !std::isalnum(static_cast<unsigned char>(a_text[end]));
    if (startsClean && endsClean) {
      return true;
    }
    at = a_text.find(a_word, at + 1);
  }
  return false;
}

void ExpressionFunctionsAreDescribed(const json &a_schema) {
  const json *description = At(a_schema, "/$defs/expression/description");
  Check(description && description->is_string(),
        "the expression description exists");
  const std::string text = description && description->is_string()
                               ? description->get<std::string>()
                               : std::string{};
  for (const std::string_view name : FunctionNames()) {
    Check(ContainsWord(text, name),
          "expression description names " + std::string{name});
  }
}
}

int main() {
  const json schema = LoadSchema();
  Check(schema.is_object(), "schema/recipe.schema.json parses");
  if (!schema.is_object()) {
    return test::Finish("recipe_schema");
  }
  SourceKindsMatchTheTable(schema);
  SignalEnumsMatchTheTables(schema);
  SourceEnumsMatchTheTables(schema);
  RecipeShapeMatchesTheTables(schema);
  OutputScalarsMatchTheTable(schema);
  OutputEnumsMatchTheTables(schema);
  ExpressionFunctionsAreDescribed(schema);
  return test::Finish("recipe_schema");
}
