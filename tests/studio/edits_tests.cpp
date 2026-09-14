#include "recipe/Recipe.h"
#include "studio/Edits.h"
#include "studio/Relationships.h"
#include "test_support.h"
#include <algorithm>

#include <filesystem>
#include <optional>
#include <string>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
const Recipe &Canonical() {
  static const Recipe recipe = [] {
    const auto path =
        std::filesystem::path{BEEF_FIXTURES_DIR}.parent_path().parent_path() /
        "schema" / "example-magicka.json";
    const auto loaded = ParseRecipe(test::ReadFile(path), "example-magicka");
    Check(loaded.recipe.has_value(), "schema/example-magicka.json parses");
    return loaded.recipe.value_or(Recipe{});
  }();
  return recipe;
}

const SurfaceOutput *MaterialAt(const Recipe &a_recipe, std::size_t a_output) {
  return a_output < a_recipe.outputs.size()
             ? Get<SurfaceOutput>(a_recipe.outputs[a_output])
             : nullptr;
}

const LightOutput *LightAt(const Recipe &a_recipe, std::size_t a_output) {
  return a_output < a_recipe.outputs.size()
             ? Get<LightOutput>(a_recipe.outputs[a_output])
             : nullptr;
}

bool RoundTrips(const Recipe &a_recipe) {
  const auto back = ParseRecipe(SerializeRecipe(a_recipe), a_recipe.id);
  return back.recipe && *back.recipe == a_recipe;
}

void Accepts(Recipe &a_recipe, const RecipeEdit &a_edit,
             const std::string &a_what) {
  const auto problem = Apply(a_recipe, a_edit);
  Check(!problem,
        a_what + " is accepted" +
            (problem ? ": " + problem->where + ": " + problem->message : ""));
  if (!problem) {
    Check(RoundTrips(a_recipe), a_what + ": edited recipe round-trips");
  }
}

void Refused(const Recipe &a_base, const RecipeEdit &a_edit,
             const std::string &a_what) {
  Recipe copy = a_base;
  const auto problem = Apply(copy, a_edit);
  Check(problem.has_value(), a_what + " is refused");
  Check(copy == a_base,
        a_what + ": a refused edit leaves the recipe unchanged");
}

void Undoes(const Recipe &a_base, const RecipeEdit &a_edit,
            const RecipeEdit &a_inverse, const std::string &a_what) {
  Recipe copy = a_base;
  Accepts(copy, a_edit, a_what);
  Accepts(copy, a_inverse, a_what + " (undo)");
  Check(copy == a_base, a_what + ": applying the inverse returns the recipe");
}
}

namespace {
Recipe RelationshipRecipe() {
  Recipe recipe;
  recipe.signals = {{"drive", ConstantSignal{}, {}},
                    {"pattern", ConstantSignal{}, {}},
                    {"response", ExprSignal{"@drive + @drive"}, {}}};
  ImageSource image;
  image.scroll = std::array<Param, 2>{Ref{"drive"}, 0.0f};
  recipe.sources = {{"pattern", image}};
  recipe.curves = {{"tone", "x"}};
  recipe.masks = {{"coverage", "@pattern * @drive"}};
  SurfaceOutput output = DefaultOutput(Surface::kMaterial, Slot::kEmissive);
  Layer layer = DefaultLayer();
  layer.source = Ref{"pattern"};
  layer.opacity = Ref{"drive"};
  layer.mask = Ref{"coverage"};
  layer.curve = CurveRef{"@tone"};
  output.stack = {layer};
  recipe.outputs = {output, output};
  recipe.shell.alpha = Ref{"drive"};
  Variant variant;
  variant.overrides.emplace("drive", 0.0f);
  recipe.variants.push_back(std::move(variant));
  return recipe;
}

void CheckRelationships() {
  Recipe recipe = RelationshipRecipe();
  const Recipe before = recipe;
  const auto links = RelationshipsOf(recipe);
  const auto contains = [&](const PropertyLocation &location,
                            const ResourceRef &driver) {
    return std::ranges::find(links, Relationship{location, driver}) !=
           links.end();
  };
  Check(contains({ResourceRef{ResourceKind::kSource, "pattern"}, "scroll", 0},
                 {ResourceKind::kSignal, "drive"}),
        "source vector operand keeps its component and owning resource");
  Check(contains({LayerOwner{1, 0}, "opacity", {}},
                 {ResourceKind::kSignal, "drive"}),
        "same-slot outputs retain exact owner indices");
  Check(
      contains({ResourceRef{ResourceKind::kMask, "coverage"}, "expression", {}},
               {ResourceKind::kSource, "pattern"}),
      "mask links resolve images ahead of same-name signals");
  Check(!contains(
            {ResourceRef{ResourceKind::kMask, "coverage"}, "expression", {}},
            {ResourceKind::kSignal, "pattern"}),
        "shadowed signal is not shown as a mask driver");
  Check(
      contains({LayerOwner{0, 0}, "curve", {}}, {ResourceKind::kCurve, "tone"}),
      "named curve connections retain their layer owner");
  Check(contains({ShellOwner{}, "alpha", {}}, {ResourceKind::kSignal, "drive"}),
        "shell parameter connections retain their property");
  Check(contains({VariantOwner{0}, "override", {}},
                 {ResourceKind::kSignal, "drive"}),
        "variant uses remain discoverable");
  const ReferenceCounts counts = CountReferences(recipe);
  Check(counts.signals.at("drive") == 7,
        "counts retain expression reference deduplication and direct uses");
  Check(counts.images.at("pattern") == 3 && counts.signals.at("pattern") == 1,
        "counts preserve conservative mask deletion protection without phantom "
        "links");
  Check(recipe == before, "relationship inspection does not mutate the recipe");
  Check(!Apply(recipe, RenameSignal{"drive", "energy"}),
        "enriched walker retains signal renaming");
  const auto renamed = RelationshipsOf(recipe);
  Check(std::ranges::none_of(
            renamed,
            [](const Relationship &link) {
              return link.driver == ResourceRef{ResourceKind::kSignal, "drive"};
            }),
        "renaming reaches every previously reported signal relationship");
}
}

int main() {
  CheckRelationships();
  const Recipe &base = Canonical();
  Check(!base.outputs.empty(), "canonical recipe has outputs");

  {
    const auto *m = MaterialAt(base, 0);
    const auto *strength =
        m ? ScalarOf(m->scalars, ScalarField::kStrength) : nullptr;
    Check(strength && strength->has_value(),
          "output 0 emissive has a strength scalar");
    if (strength && *strength) {
      Undoes(base, SetScalar{0, ScalarField::kStrength, Param{3.0f}},
             SetScalar{0, ScalarField::kStrength, **strength},
             "SetScalar strength");
    }
  }

  {
    const auto *m = MaterialAt(base, 1);
    Check(m && m->scalars.color.has_value(),
          "output 1 fuzz has a color scalar");
    if (m && m->scalars.color) {
      Undoes(
          base,
          SetColorScalar{1, Vec3Param{std::array<Param, 3>{0.2f, 0.4f, 0.6f}}},
          SetColorScalar{1, *m->scalars.color}, "SetColorScalar");
    }
  }

  {
    const auto *m = MaterialAt(base, 2);
    Check(m != nullptr, "output 2 is a material output");
    if (m) {
      const std::size_t top = m->stack.size();
      Undoes(base, AddLayer{2, DefaultLayer(), std::nullopt},
             RemoveLayer{2, top}, "AddLayer then RemoveLayer");
    }
  }

  Undoes(base, MoveLayer{0, 0, 2}, MoveLayer{0, 2, 0}, "MoveLayer round trip");

  {
    const auto *m = MaterialAt(base, 2);
    const Layer *layer = m && m->stack.size() > 1 ? &m->stack[1] : nullptr;
    Check(layer != nullptr, "output 2 has a second layer");
    if (layer) {
      Undoes(base, SetLayerOpacity{2, 1, Param{0.5f}},
             SetLayerOpacity{2, 1, layer->opacity}, "SetLayerOpacity");
    }
  }

  Undoes(base, AddSignal{"probeSignal"}, RemoveSignal{"probeSignal"},
         "AddSignal then RemoveSignal");
  Undoes(base, AddCurve{"probeCurve"}, RemoveCurve{"probeCurve"},
         "AddCurve then RemoveCurve");
  Undoes(base, AddMask{"probeMask"}, RemoveMask{"probeMask"},
         "AddMask then RemoveMask");
  Undoes(base, AddSource{"probeSource", MaterialClustersSource{}},
         RemoveSource{"probeSource"}, "AddSource clusters then RemoveSource");

  Undoes(base, RenameSignal{"glowHue", "glowTint"},
         RenameSignal{"glowTint", "glowHue"}, "RenameSignal and back");

  {
    const auto *rest = base.FindCurve("rest");
    Check(rest != nullptr, "curve rest exists");
    if (rest) {
      Undoes(base, SetCurve{"rest", "x * 0.5"}, SetCurve{"rest", rest->text},
             "SetCurve");
    }
  }

  {
    const auto *light = LightAt(base, 4);
    Check(light != nullptr, "output 4 is a light");
    if (light) {
      Undoes(base, SetLightParam{4, LightParam::kIntensity, Param{2.5f}},
             SetLightParam{4, LightParam::kIntensity, light->intensity},
             "SetLightParam intensity");
      Undoes(base, SetLightShadow{4, !light->shadow},
             SetLightShadow{4, light->shadow}, "SetLightShadow");
      Undoes(base, SetLightReplace{4, !light->replace},
             SetLightReplace{4, light->replace}, "SetLightReplace");
    }
  }

  Undoes(base, SetShellParam{ShellParam::kAlpha, Param{0.5f}},
         SetShellParam{ShellParam::kAlpha, base.shell.alpha},
         "SetShellParam alpha");
  Undoes(base, SetShellBlend{ShellBlend::kAlpha},
         SetShellBlend{base.shell.blend}, "SetShellBlend");
  Undoes(base, SetShellAlphaTest{0.5f}, SetShellAlphaTest{base.shell.alphaTest},
         "SetShellAlphaTest");

  Undoes(base, SetPriority{7}, SetPriority{base.priority}, "SetPriority");
  Undoes(base, SetClockSpeed{2.0f}, SetClockSpeed{base.clock.speed},
         "SetClockSpeed");

  {
    RecipeKey key;
    key.kind = KeyKind::kMaterial;
    key.operand = std::string{"*fire*"};
    Undoes(base, AddKey{key}, RemoveKey{key}, "AddKey then RemoveKey");
    Refused(base, AddKey{base.keys.front()}, "AddKey duplicate");
    Refused(base, RemoveKey{base.keys.front()}, "RemoveKey the only key");
  }

  MaterialClustersSource bad;
  bad.clusters = 0;
  Refused(base, AddSource{"badClusters", bad},
          "AddSource with zero clusters (loader parity)");

  Refused(base,
          SetScalar{0, ScalarField::kStrength, Param{Ref{"noSuchSignal"}}},
          "SetScalar reading an unknown signal");
  Refused(base, SetScalar{99, ScalarField::kStrength, Param{1.0f}},
          "SetScalar on an out-of-range output");
  Refused(base, SetLightParam{2, LightParam::kIntensity, Param{1.0f}},
          "SetLightParam on a material output");
  Refused(base, SetShellPoint{ShellPoint::kSpinAxis, Vec3{0.0f, 0.0f, 0.0f}},
          "SetShellPoint with a zero spin axis");
  Refused(base, SetShellAlphaTest{2.0f}, "SetShellAlphaTest out of range");
  Refused(base, SetShellMaterial{ShellMaterial::kVanilla},
          "SetShellMaterial vanilla while a non-emissive shell slot is bound");
  Refused(base, RemoveSignal{"glowHue"}, "RemoveSignal that is referenced");

  {
    const auto counts = CountReferences(base);
    const auto found = counts.signals.find("glowHue");
    Check(found != counts.signals.end() && found->second > 0,
          "CountReferences finds glowHue in use");
  }

  {
    Check(!Describe(RecipeEdit{AddLight{}}).empty(),
          "Describe AddLight is text");
    Check(!Describe(RecipeEdit{SetPriority{3}}).empty(),
          "Describe SetPriority is text");
    EditBatch batch;
    batch.edits.push_back(AddSignal{"a"});
    batch.edits.push_back(AddSignal{"b"});
    const auto text = Describe(batch);
    Check(text.find("; ") != std::string::npos,
          "Describe(batch) joins edits with a separator");
  }

  {
    Recipe copy = base;
    EditBatch good;
    good.edits.push_back(AddSignal{"batchOne"});
    good.edits.push_back(RenameSignal{"batchOne", "batchTwo"});
    const Recipe before = copy;
    const auto prepared = PrepareEdits(copy, good);
    Check(prepared && prepared->FindSignal("batchTwo") &&
              !prepared->FindSignal("batchOne"),
          "preparation evaluates edits in sequence against the candidate");
    Check(copy == before, "preparation leaves the live recipe unchanged");
    Check(!Apply(copy, good), "a valid batch applies");
    Check(prepared && copy == *prepared,
          "applying a batch commits the same candidate as preparation");
    Check(RoundTrips(copy), "the batched recipe round-trips");

    Recipe rollback = base;
    EditBatch bad;
    bad.edits.push_back(AddSignal{"batchThree"});
    bad.edits.push_back(RemoveSignal{"noSuchSignal"});
    const auto refused = PrepareEdits(rollback, bad);
    Check(!refused && rollback == base, "failed preparation discards earlier "
                                        "edits without touching the original");
    if (!refused) {
      Check(refused.error().severity == Severity::kError &&
                refused.error().where == "signal noSuchSignal",
            "preparation preserves the refusing edit's diagnostic location");
    }
    Check(Apply(rollback, bad).has_value(),
          "a batch with a bad edit is refused");
    Check(rollback == base, "a refused batch leaves the recipe unchanged");

    const auto empty = PrepareEdits(base, EditBatch{});
    Check(empty && *empty == base,
          "an empty batch yields an unchanged candidate");
    const EditBatch cancelled{
        {AddSignal{"temporary"}, RemoveSignal{"temporary"}}};
    const auto unchanged = PrepareEdits(base, cancelled);
    Check(unchanged && *unchanged == base,
          "edits that cancel each other can be recognized before retiring "
          "actors");
  }

  {
    Recipe recipe;
    recipe.id = "sourceRefs";
    recipe.signals.push_back(Signal{"drive", ConstantSignal{1.0f}, {}});
    ImageSource image;
    image.path = "a.dds";
    image.scroll = Vec2Param{Ref{"drive"}};
    image.tile = Vec2Param{std::array<Param, 2>{Param{Ref{"drive"}}, 1.0f}};
    recipe.sources.push_back(Source{"pattern", image});
    RippleSource ripple;
    ripple.trigger = Ref{"drive"};
    ripple.speed = Ref{"drive"};
    ripple.width = Ref{"drive"};
    ripple.decay = Ref{"drive"};
    recipe.sources.push_back(Source{"wave", ripple});
    recipe.sources.push_back(Source{"rough", MaterialSource{}});
    recipe.sources.push_back(Source{"pos", BakeSource{}});
    recipe.sources.push_back(Source{"u", UvSource{}});
    recipe.sources.push_back(Source{"far", DistanceSource{}});
    recipe.sources.push_back(Source{"bands", MaterialClustersSource{}});
    Check(!Apply(recipe, RenameSignal{"drive", "energy"}),
          "renaming a signal read by every referencing source kind");
    const auto *renamedImage = Get<ImageSource>(recipe.sources[0].kind);
    const auto *renamedRipple = Get<RippleSource>(recipe.sources[1].kind);
    Check(renamedImage && renamedImage->scroll == Vec2Param{Ref{"energy"}} &&
              renamedImage->tile ==
                  Vec2Param{std::array<Param, 2>{Param{Ref{"energy"}}, 1.0f}},
          "image scroll and tile follow the rename");
    Check(renamedRipple && renamedRipple->trigger == Ref{"energy"} &&
              renamedRipple->speed == Param{Ref{"energy"}} &&
              renamedRipple->width == Param{Ref{"energy"}} &&
              renamedRipple->decay == Param{Ref{"energy"}},
          "ripple trigger, speed, width and decay follow the rename");
    Check(recipe.sources.size() == 7 &&
              Is<MaterialSource>(recipe.sources[2].kind) &&
              Is<BakeSource>(recipe.sources[3].kind) &&
              Is<UvSource>(recipe.sources[4].kind) &&
              Is<DistanceSource>(recipe.sources[5].kind) &&
              Is<MaterialClustersSource>(recipe.sources[6].kind),
          "sources without parameters are visited and left unchanged");
  }

  return test::Finish("studio_edits");
}
