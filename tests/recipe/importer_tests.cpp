#include "recipe/Importer.h"
#include "test_support.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace {
std::string ReadEfsh(const std::string &a_name) {
  return test::ReadFile(test::Fixtures() / "efsh" / (a_name + ".json"));
}

Recipe LoadTemplate(std::string_view a_id) {
  const std::string text =
      test::ReadFile(test::Templates() / (std::string{a_id} + ".json"));
  Check(!text.empty(), std::format("template {} is readable", a_id));
  const LoadResult loaded = ParseRecipe(text, a_id);
  Check(loaded.recipe.has_value(), std::format("template {} parses", a_id));
  Check(!loaded.HasErrors(),
        std::format("template {} parses without errors", a_id));
  return loaded.recipe.value_or(Recipe{});
}

const ConstantSignal *Const(const Recipe &a_recipe, const char *a_name) {
  const Signal *s = a_recipe.FindSignal(a_name);
  return s ? Get<ConstantSignal>(s->kind) : nullptr;
}

const ExprSignal *Expression(const Recipe &a_recipe, const char *a_name) {
  const Signal *s = a_recipe.FindSignal(a_name);
  return s ? Get<ExprSignal>(s->kind) : nullptr;
}

const EfshSignal *EfshSig(const Recipe &a_recipe, const char *a_name) {
  const Signal *s = a_recipe.FindSignal(a_name);
  return s ? Get<EfshSignal>(s->kind) : nullptr;
}

const SurfaceOutput *Surf(const Recipe &a_recipe, std::size_t a_index) {
  return a_index < a_recipe.outputs.size()
             ? Get<SurfaceOutput>(a_recipe.outputs[a_index])
             : nullptr;
}

bool RefIs(const Param &a_param, const char *a_name) {
  const Ref *ref = Get<Ref>(a_param);
  return ref && ref->name == a_name;
}

bool ConstNear(const Recipe &a_recipe, const char *a_name, float a_expected) {
  const ConstantSignal *c = Const(a_recipe, a_name);
  const float *v = c ? Get<float>(c->value) : nullptr;
  return v && Near(*v, a_expected);
}

std::vector<std::string> EfshFixtureNames() {
  std::vector<std::string> names;
  for (const std::filesystem::directory_entry &entry :
       std::filesystem::directory_iterator{test::Fixtures() / "efsh"}) {
    if (entry.path().extension() == ".json") {
      names.push_back(entry.path().stem().string());
    }
  }
  std::ranges::sort(names);
  return names;
}
}

int main() {
  {
    EffectShaderRecord record;
    record.editorId = "EnchArmorMagickaFXS";
    record.fillTexture = "Effects\\DarkSwirls.dds";
    record.params.fill.fullAlphaRatio = 0.05f;

    Check(record.tileU == 1.0f && record.tileV == 1.0f,
          "an effect-shader record defaults its tiling");
    Check(record.editorId == "EnchArmorMagickaFXS",
          "the record keeps its editor id");
    Check(record.params.fill.fullAlphaRatio == 0.05f,
          "the record carries effect params");
    Check(ImportTemplateId(record) == "fill",
          "a record with a fill texture selects the fill template");
    record.fillTexture.clear();
    Check(ImportTemplateId(record) == "bare",
          "a record without a fill texture selects the bare template");
  }

  {
    const std::expected<EffectShaderRecord, std::string> notJson =
        ParseEffectShaderRecord("{ this is not json");
    Check(!notJson.has_value(), "malformed json is reported, never parsed");

    const std::expected<EffectShaderRecord, std::string> notObject =
        ParseEffectShaderRecord("[1, 2, 3]");
    Check(!notObject.has_value(), "a non-object document is reported");

    const std::expected<EffectShaderRecord, std::string> noKey =
        ParseEffectShaderRecord(R"({"editorId":"X"})");
    Check(!noKey.has_value(), "a record without a form key is reported");

    const std::expected<EffectShaderRecord, std::string> badColor =
        ParseEffectShaderRecord(
            R"({"formKey":"0x1~Skyrim.esm","fillColorKey1":["a","b","c"]})");
    Check(badColor.has_value(),
          "a non-numeric colour is tolerated, not a crash");
    if (badColor) {
      Check(badColor->params.colorKeys[0] == Vec3{0.0f, 0.0f, 0.0f},
            "a non-numeric colour falls back to black");
    }
  }

  const Recipe fillTemplate = LoadTemplate("fill");
  const Recipe bareTemplate = LoadTemplate("bare");

  {
    const std::span<const std::string_view> names = ImportSignalNames();
    for (std::size_t i = 0; i < names.size(); ++i) {
      Check(
          names[i].starts_with("import"),
          std::format("reserved name {} carries the import prefix", names[i]));
      Check(std::ranges::count(names, names[i]) == 1,
            std::format("reserved name {} is unique", names[i]));
    }
    for (const Recipe *t : {&fillTemplate, &bareTemplate}) {
      for (const Signal &s : t->signals) {
        if (!s.name.starts_with("import")) {
          continue;
        }
        Check(std::ranges::find(names, s.name) != names.end(),
              std::format("template {} row {} is a reserved import name", t->id,
                          s.name));
        Check(Get<ConstantSignal>(s.kind) != nullptr,
              std::format("template {} row {} is a constant", t->id, s.name));
      }
      for (const Signal &s : t->signals) {
        if (const EfshSignal *efsh = Get<EfshSignal>(s.kind)) {
          Check(efsh->record.text == "ImportedEffectShader",
                std::format("template {} efsh row {} names the placeholder",
                            t->id, s.name));
        }
      }
    }
    for (const Source &s : bareTemplate.sources) {
      const ImageSource *image = Get<ImageSource>(s.kind);
      Check(!image || image->path != kFillTextureToken,
            "the bare template has no fill-texture sources");
    }
  }

  {
    const std::expected<EffectShaderRecord, std::string> parsed =
        ParseEffectShaderRecord(ReadEfsh("EnchArmorMagickaFXS"));
    Check(parsed.has_value(), "the magicka effect shader parses");
    if (parsed) {
      const EffectShaderRecord &rec = *parsed;
      Check(rec.editorId == "EnchArmorMagickaFXS", "editor id is read");
      Check(rec.key.file == "Skyrim.esm" && rec.key.localId == 0x92DEDu,
            "the form key is read");
      Check(rec.fillTexture == "Effects\\DarkSwirls.dds",
            "the fill texture is read");
      Check(Near(rec.tileU, 3.0f) && Near(rec.tileV, 3.0f),
            "the tiling is read");
      Check(Near(rec.params.fill.fullAlphaRatio, 0.05f),
            "the fill full-alpha ratio is read");
      Check(Near(rec.params.fill.persistentAlphaRatio, 0.05f),
            "the fill persistent-alpha ratio is read");
      Check(Near(rec.params.edge.persistentAlphaRatio, 0.9f),
            "the edge persistent-alpha ratio is read");
      Check(Near(rec.params.animationSpeedV, 0.1f),
            "the v animation speed is read");
      Check(Near(rec.params.edgeColor.x, 20.0f / 255.0f),
            "the edge colour is read and normalised to 0..1");

      Check(RecipeIdFor(rec) == "EnchArmorMagickaFXS",
            "the recipe id is the editor id");

      const Recipe r = ImportEffectShader(rec, fillTemplate);
      Check(r.id == "EnchArmorMagickaFXS",
            "the imported recipe carries the id");
      Check(r.metadata.name == "EnchArmorMagickaFXS",
            "the metadata name is the editor id");
      Check(r.metadata.imported == "BetterEnchantmentEffects 0.1.0",
            "the importer stamps its version");
      Check(r.metadata.description ==
                "Imported from effect shader EnchArmorMagickaFXS with the "
                "fill template.",
            "the description names the source shader and the template");

      Check(r.keys.size() == 1 && r.keys[0].kind == KeyKind::kEffectShader,
            "the recipe keys on the effect shader");
      const FormRef *keyForm = r.keys[0].Form();
      Check(keyForm && keyForm->text == "EnchArmorMagickaFXS",
            "the key names the shader by editor id");

      const EfshSignal *fillRaw = EfshSig(r, "fillRaw");
      Check(fillRaw && fillRaw->field == EfshField::kFillAlpha &&
                fillRaw->record.text == "EnchArmorMagickaFXS",
            "fillRaw reads the fill alpha of the imported shader");
      Check(EfshSig(r, "edgeRaw") &&
                EfshSig(r, "edgeRaw")->record.text == "EnchArmorMagickaFXS",
            "edgeRaw is retargeted to the imported shader");
      Check(EfshSig(r, "edgeColor") &&
                EfshSig(r, "edgeColor")->field == EfshField::kEdgeColor,
            "edgeColor reads the edge colour");
      Check(EfshSig(r, "scroll") &&
                EfshSig(r, "scroll")->field == EfshField::kScroll,
            "scroll reads the scroll offset");

      Check(ConstNear(r, "importFillBase", 0.05f),
            "importFillBase is the fill baseline alpha");
      Check(ConstNear(r, "importEdgeBase", 0.9f),
            "importEdgeBase is the edge baseline alpha");
      Check(Expression(r, "fillLevel") && Expression(r, "fillLevel")->text ==
                                              "@fillRaw / @importFillBase",
            "fillLevel normalises by the imported baseline");

      const ConstantSignal *hue = Const(r, "importHue");
      Check(hue != nullptr, "importHue is a constant");
      if (hue) {
        const Vec3 *v = Get<Vec3>(hue->value);
        Check(v && Near(v->x, 20.0f / 129.0f) && Near(v->y, 50.0f / 129.0f) &&
                  Near(v->z, 1.0f),
              "importHue is the edge-tinted, hue-normalised emissive colour");
      }
      Check(ConstNear(r, "glowStrength", 1.0f),
            "glowStrength keeps the template default");
      Check(Expression(r, "glowLevel") && Expression(r, "glowLevel")->text ==
                                              "@glowStrength * @fillLevel",
            "glowLevel scales fill by strength");
      Check(Expression(r, "sheenScroll") &&
                Expression(r, "sheenScroll")->text == "@scroll + @sheenPhase",
            "sheenScroll offsets by the sheen phase signal");
      Check(Expression(r, "shellOpacity") &&
                Expression(r, "shellOpacity")->text ==
                    "saturate(@shellAlpha * clamp(@fillLevel, 0, 2))",
            "shellOpacity scales by the shell alpha signal");
      Check(Expression(r, "inflate") &&
                Expression(r, "inflate")->text ==
                    "(@inflatePercent + @inflatePulse * clamp(@fillLevel, 0, "
                    "2)) * 0.01",
            "inflate combines the inflate signals");

      const Source *fill = r.FindSource("fill");
      Check(fill != nullptr, "the fill source exists in the fill template");
      if (fill) {
        const ImageSource *img = Get<ImageSource>(fill->kind);
        Check(img && img->path == "Effects\\DarkSwirls.dds",
              "the fill source is patched to the fill texture");
        Check(img && img->channel == ImageChannel::kRgb,
              "the fill source samples rgb");
        if (img && img->tile) {
          const std::array<Param, 2> *tile =
              Get<std::array<Param, 2>>(*img->tile);
          Check(tile && Get<float>((*tile)[0]) &&
                    Near(*Get<float>((*tile)[0]), 3.0f),
                "the fill source tiles by the shader scale");
        }
      }
      for (const char *name : {"sheenField", "shimmerField", "glossField"}) {
        const Source *s = r.FindSource(name);
        const ImageSource *img = s ? Get<ImageSource>(s->kind) : nullptr;
        Check(
            img && img->path == "Effects\\DarkSwirls.dds",
            std::format("the {} source is patched to the fill texture", name));
      }
      const Mask *metal = r.FindMask("metal");
      Check(metal && metal->text == "@metallic",
            "the metal mask reads the metallic source");

      Check(r.outputs.size() == 5, "the fill import produces five outputs");
      const SurfaceOutput *emissive = Surf(r, 0);
      Check(emissive && emissive->surface == Surface::kShell &&
                emissive->slot == Slot::kEmissive,
            "output 0 is the shell emissive");
      if (emissive) {
        Check(emissive->scalars.strength &&
                  RefIs(*emissive->scalars.strength, "glowLevel"),
              "the emissive strength is glowLevel");
        Check(!emissive->stack.empty() && emissive->stack[0].color &&
                  Get<Ref>(*emissive->stack[0].color) &&
                  Get<Ref>(*emissive->stack[0].color)->name == "importHue",
              "the emissive layer is tinted by importHue");
      }
      const LightOutput *light = Get<LightOutput>(r.outputs[4]);
      Check(light != nullptr, "output 4 is the light");
      if (light) {
        Check(Get<Ref>(light->color) &&
                  Get<Ref>(light->color)->name == "importHue",
              "the light colour is importHue");
        Check(Get<Ref>(light->intensity) &&
                  Get<Ref>(light->intensity)->name == "lightLevel",
              "the light intensity is lightLevel");
      }

      Check(RefIs(r.shell.opacity, "shellOpacity"),
            "the shell alpha is shellOpacity");
      const std::array<Param, 3> *inflate =
          Get<std::array<Param, 3>>(r.shell.pose.inflate);
      Check(inflate && Get<float>((*inflate)[0]) &&
                Near(*Get<float>((*inflate)[0]), 0.0f) &&
                RefIs((*inflate)[1], "inflate"),
            "the shell inflates on y and z by the inflate signal");
    }
  }

  {
    const std::expected<EffectShaderRecord, std::string> parsed =
        ParseEffectShaderRecord(ReadEfsh("WaterBreathingFXS"));
    Check(parsed.has_value(), "the fill-less water-breathing shader parses");
    if (parsed) {
      Check(parsed->fillTexture.empty(),
            "the water-breathing shader has no fill texture");
      Check(ImportTemplateId(*parsed) == "bare",
            "the fill-less shader selects the bare template");
      const Recipe r = ImportEffectShader(*parsed, bareTemplate);
      Check(r.id == "WaterBreathingFXS", "the fill-less recipe carries its id");
      Check(r.FindSignal("sheenScroll") == nullptr,
            "the bare template has no sheenScroll signal");
      Check(r.FindSource("fill") == nullptr &&
                r.FindSource("sheenField") == nullptr,
            "the bare template has no fill-derived sources");
      Check(r.FindSource("relief") && r.FindSource("metallic"),
            "the material sources are always present");
      Check(ConstNear(r, "importFillBase", 0.1f),
            "importFillBase is patched without a fill texture");
      Check(ConstNear(r, "importEdgeBase", 1.0f),
            "importEdgeBase is patched without a fill texture");
      Check(r.outputs.size() == 4,
            "the bare import drops the rmaos gloss output");
      const SurfaceOutput *emissive = Surf(r, 0);
      Check(emissive && !emissive->stack.empty() &&
                Get<Vec3>(emissive->stack[0].source),
            "the bare emissive layer is a white constant");
      const ConstantSignal *hue = Const(r, "importHue");
      if (hue) {
        const Vec3 *v = Get<Vec3>(hue->value);
        Check(v && Near(v->z, 1.0f) && Near(v->x, 0.5365854f, 1e-3f),
              "importHue is still resolved without a fill texture");
      }
    }
  }

  {
    const bool update = std::getenv("BEEF_UPDATE") != nullptr;
    for (const std::string &name : EfshFixtureNames()) {
      const std::expected<EffectShaderRecord, std::string> parsed =
          ParseEffectShaderRecord(ReadEfsh(name));
      Check(parsed.has_value(), std::format("fixture efsh {} parses", name));
      if (!parsed) {
        continue;
      }
      const Recipe &chosen =
          ImportTemplateId(*parsed) == "fill" ? fillTemplate : bareTemplate;
      const Recipe imported = ImportEffectShader(*parsed, chosen);
      const std::string text = SerializeRecipe(imported);
      const LoadResult back = ParseRecipe(text, imported.id);
      Check(back.recipe.has_value() && !back.HasErrors() &&
                *back.recipe == imported,
            std::format("imported {} reads back identical", name));
      const std::filesystem::path golden =
          test::Fixtures() / "recipes" / (name + ".json");
      if (update) {
        Check(test::WriteFile(golden, text),
              std::format("golden {} rewritten", name));
      } else {
        Check(text == test::ReadFile(golden),
              std::format("imported {} matches its golden (BEEF_UPDATE=1 "
                          "rewrites it)",
                          name));
      }
    }
  }

  return test::Finish("importer");
}
