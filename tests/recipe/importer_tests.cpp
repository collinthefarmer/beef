#include "recipe/Importer.h"
#include "test_support.h"

using namespace BetterEnchantmentEffects;
using test::Check;
using test::Near;

namespace {
std::string ReadEfsh(const std::string &a_name) {
  return test::ReadFile(test::Fixtures() / "efsh" / (a_name + ".json"));
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

const Curve *CurveNamed(const Recipe &a_recipe, const char *a_name) {
  return a_recipe.FindCurve(a_name);
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

      const Recipe r = ImportEffectShader(rec);
      Check(r.id == "EnchArmorMagickaFXS",
            "the imported recipe carries the id");
      Check(r.metadata.name == "EnchArmorMagickaFXS",
            "the metadata name is the editor id");
      Check(r.metadata.imported == "BetterEnchantmentEffects 0.1.0",
            "the importer stamps its version");
      Check(r.metadata.description ==
                "Imported from effect shader EnchArmorMagickaFXS with the "
                "shipped defaults.",
            "the description names the source shader");

      Check(r.keys.size() == 1 && r.keys[0].kind == KeyKind::kEffectShader,
            "the recipe keys on the effect shader");
      const FormRef *keyForm = r.keys[0].Form();
      Check(keyForm && keyForm->text == "EnchArmorMagickaFXS",
            "the key names the shader by editor id");

      const Curve *rest = CurveNamed(r, "rest");
      const Curve *edgeRest = CurveNamed(r, "edgeRest");
      const Curve *crisp = CurveNamed(r, "crisp");
      const Curve *punchy = CurveNamed(r, "punchy");
      Check(rest && rest->text == "x / 0.05",
            "the rest curve is seeded from the fill baseline");
      Check(edgeRest && edgeRest->text == "x / 0.9",
            "the edgeRest curve is seeded from the edge baseline");
      Check(crisp && crisp->text == "(x - mean) * 3 + 0.5",
            "the crisp curve uses the relief contrast default");
      Check(punchy && punchy->text == "(x - mean) * 2 + 0.5",
            "the punchy curve uses the gloss contrast default");

      const EfshSignal *fillLevel = EfshSig(r, "fillLevel");
      Check(fillLevel && fillLevel->field == EfshField::kFillAlpha,
            "fillLevel reads the fill alpha");
      const Signal *fillLevelSig = r.FindSignal("fillLevel");
      Check(fillLevelSig && fillLevelSig->curve &&
                fillLevelSig->curve->text == "@rest",
            "fillLevel wears the rest curve");
      Check(EfshSig(r, "edgeLevel") &&
                EfshSig(r, "edgeLevel")->field == EfshField::kEdgeAlpha,
            "edgeLevel reads the edge alpha");
      Check(EfshSig(r, "edgeColor") &&
                EfshSig(r, "edgeColor")->field == EfshField::kEdgeColor,
            "edgeColor reads the edge colour");
      Check(EfshSig(r, "scroll") &&
                EfshSig(r, "scroll")->field == EfshField::kScroll,
            "scroll reads the scroll offset");

      const ConstantSignal *glowHue = Const(r, "glowHue");
      Check(glowHue != nullptr, "glowHue is a constant");
      if (glowHue) {
        const Vec3 *hue = Get<Vec3>(glowHue->value);
        Check(hue && Near(hue->x, 20.0f / 129.0f) &&
                  Near(hue->y, 50.0f / 129.0f) && Near(hue->z, 1.0f),
              "glowHue is the edge-tinted, hue-normalised emissive colour");
      }
      const ConstantSignal *glowStrength = Const(r, "glowStrength");
      Check(glowStrength && Get<float>(glowStrength->value) &&
                Near(*Get<float>(glowStrength->value), 1.0f),
            "glowStrength uses the emissive default");
      Check(Expression(r, "glowLevel") && Expression(r, "glowLevel")->text ==
                                              "@glowStrength * @fillLevel",
            "glowLevel scales fill by strength");

      Check(Const(r, "sheenScale") &&
                Get<float>(Const(r, "sheenScale")->value) &&
                Near(*Get<float>(Const(r, "sheenScale")->value), 0.5f),
            "sheenScale uses the default");
      Check(Expression(r, "sheenWeight") &&
                Expression(r, "sheenWeight")->text ==
                    "clamp(@edgeLevel * @sheenScale, 0, 1)",
            "sheenWeight clamps the edge level");
      Check(Expression(r, "sheenScroll") &&
                Expression(r, "sheenScroll")->text == "@scroll + 0.5",
            "sheenScroll offsets by the sheen phase");
      Check(Expression(r, "shimmerScroll") &&
                Expression(r, "shimmerScroll")->text == "@scroll + 0.25",
            "shimmerScroll offsets by the shimmer phase");
      Check(Expression(r, "glossAmount") &&
                Expression(r, "glossAmount")->text ==
                    "@glossBoost * saturate(@fillLevel)",
            "glossAmount boosts saturated fill");
      Check(Expression(r, "shellOpacity") &&
                Expression(r, "shellOpacity")->text ==
                    "saturate(0.5 * clamp(@fillLevel, 0, 2))",
            "shellOpacity uses the shell alpha default");
      Check(Expression(r, "inflate") &&
                Expression(r, "inflate")->text ==
                    "(1 + 1 * clamp(@fillLevel, 0, 2)) * 0.01",
            "inflate uses the inflate defaults");

      const Source *fill = r.FindSource("fill");
      Check(fill != nullptr,
            "the fill source exists when there is a fill texture");
      if (fill) {
        const ImageSource *img = Get<ImageSource>(fill->kind);
        Check(img && img->path == "Effects\\DarkSwirls.dds",
              "the fill source points at the fill texture");
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
      Check(r.FindSource("sheenField") && r.FindSource("shimmerField") &&
                r.FindSource("glossField"),
            "the fill-derived fields exist");
      const Source *metallic = r.FindSource("metallic");
      Check(metallic && Get<MaterialSource>(metallic->kind) &&
                Get<MaterialSource>(metallic->kind)->channel ==
                    MaterialChannel::kMetallic,
            "the glow mask reads the metallic channel by default");
      const Mask *metal = r.FindMask("metal");
      Check(metal && metal->text == "@metallic",
            "the metal mask reads the metallic source");

      Check(r.outputs.size() == 5,
            "the full fill import produces five outputs");
      const SurfaceOutput *emissive = Surf(r, 0);
      Check(emissive && emissive->surface == Surface::kShell &&
                emissive->slot == Slot::kEmissive,
            "output 0 is the shell emissive");
      if (emissive) {
        Check(emissive->scalars.strength &&
                  RefIs(*emissive->scalars.strength, "glowLevel"),
              "the emissive strength is glowLevel");
        Check(emissive->stack.size() == 1, "the emissive stack has one layer");
        if (!emissive->stack.empty()) {
          const Layer &l = emissive->stack[0];
          Check(Get<Ref>(l.source) && Get<Ref>(l.source)->name == "fill",
                "the emissive layer samples the fill source");
          Check(l.color && Get<Ref>(*l.color) &&
                    Get<Ref>(*l.color)->name == "glowHue",
                "the emissive layer is tinted by glowHue");
          Check(l.mask && l.mask->name == "metal",
                "the emissive layer is masked to metal");
        }
      }
      const SurfaceOutput *rmaos = Surf(r, 3);
      Check(rmaos && rmaos->slot == Slot::kRmaos,
            "the gloss output on rmaos appears with a fill texture");

      const LightOutput *light = Get<LightOutput>(r.outputs[4]);
      Check(light != nullptr, "output 4 is the light");
      if (light) {
        Check(Get<Ref>(light->intensity) &&
                  Get<Ref>(light->intensity)->name == "lightLevel",
              "the light intensity is lightLevel");
        const SkinnedBones *bones = Get<SkinnedBones>(light->bones);
        Check(bones && bones->max == 2,
              "the light rides at most two bones by default");
      }

      Check(RefIs(r.shell.alpha, "shellOpacity"),
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
      const Recipe r = ImportEffectShader(*parsed);
      Check(r.id == "WaterBreathingFXS", "the fill-less recipe carries its id");
      Check(r.FindSignal("sheenScroll") == nullptr,
            "no sheenScroll signal without a fill texture");
      Check(r.FindSignal("shimmerScroll") == nullptr,
            "no shimmerScroll signal without a fill texture");
      Check(r.FindSignal("glossBoost") == nullptr,
            "no glossBoost signal without a fill texture");
      Check(r.FindSource("fill") == nullptr &&
                r.FindSource("sheenField") == nullptr,
            "no fill-derived sources without a fill texture");
      Check(r.FindSource("relief") && r.FindSource("metallic"),
            "the material sources are always present");
      Check(r.outputs.size() == 4,
            "the fill-less import drops the rmaos gloss output");
      const SurfaceOutput *emissive = Surf(r, 0);
      Check(emissive && !emissive->stack.empty() &&
                Get<Vec3>(emissive->stack[0].source),
            "the fill-less emissive layer falls back to a white constant");
      const ConstantSignal *glowHue = Const(r, "glowHue");
      if (glowHue) {
        const Vec3 *hue = Get<Vec3>(glowHue->value);
        Check(hue && Near(hue->z, 1.0f) && Near(hue->x, 0.5365854f, 1e-3f),
              "glowHue is still resolved without a fill texture");
      }
    }
  }

  return test::Finish("importer");
}
