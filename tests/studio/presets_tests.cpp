#include "studio/Presets.h"
#include "test_support.h"

#include <format>
#include <string>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

int main() {
  const auto ok = ParsePresets(R"({
    "presets": [
      { "name": "leather",
        "expression": "(1 - @m) * @r",
        "sources": { "m": { "material": "metallic" }, "r": { "material": "roughness" } } },
      { "name": "chest", "partition": "body", "bones": ["NPC Spine2 [Spn2]"] }
    ]
  })");
  Check(ok.has_value(), "a well-formed preset file parses");
  if (ok) {
    Check(ok->presets.size() == 2, "both presets are read");
    Check(ok->presets[0].name == "leather" &&
              ok->presets[0].sources.size() == 2 &&
              ok->presets[0].expression == "(1 - @m) * @r",
          "the material preset keeps its expression and sources");
    Check(ok->presets[1].partition == std::uint32_t{32} &&
              ok->presets[1].bones.size() == 1,
          "the spatial preset resolves its partition name and bones");
  }

  const auto notObject = ParsePresets("[]");
  Check(!notObject.has_value(), "a non-object file is refused");

  const auto noContent = ParsePresets(R"({"presets":[{"name":"empty"}]})");
  Check(!noContent.has_value(),
        "a preset with no expression, partition or bones is refused");

  const auto badName =
      ParsePresets(R"({"presets":[{"name":"1bad","expression":"1"}]})");
  Check(!badName.has_value(), "a preset name must be a valid identifier");

  std::string bones;
  for (std::size_t i = 0; i <= kMaxPresetBones; ++i) {
    bones += std::format("{}\"b{}\"", bones.empty() ? "" : ",", i);
  }
  const auto tooManyBones = ParsePresets(
      std::format(R"({{"presets":[{{"name":"big","bones":[{}]}}]}})", bones));
  Check(!tooManyBones.has_value(), "the per-preset bone cap is enforced");

  std::string entries;
  for (std::size_t i = 0; i <= kMaxPresets; ++i) {
    entries += std::format("{}{{\"name\":\"p{}\",\"expression\":\"1\"}}",
                           entries.empty() ? "" : ",", i);
  }
  const auto tooMany =
      ParsePresets(std::format(R"({{"presets":[{}]}})", entries));
  Check(!tooMany.has_value(), "the preset count cap is enforced");

  const auto missingArray = ParsePresets(R"({"note":"none"})");
  Check(missingArray.has_value() && missingArray->presets.empty(),
        "a file without a presets array yields no presets, not an error");

  return test::Finish("studio_presets");
}
