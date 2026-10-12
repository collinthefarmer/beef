// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/ActorCatalogs.h"
#include "studio/FieldParsing.h"
#include "studio/Forms.h"
#include "studio/Presets.h"
#include "studio/TermTemplates.h"
#include "test_support.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
[[nodiscard]] std::vector<std::string>
ValuesOf(const GameObjectCatalog &a_catalog) {
  std::vector<std::string> values;
  for (const GameObjectCandidate &candidate : a_catalog.candidates) {
    values.push_back(candidate.value);
  }
  return values;
}

[[nodiscard]] GeometryRow Geometry(std::string a_name,
                                   std::vector<BoneCoverage> a_bones) {
  GeometryRow geometry;
  geometry.name = std::move(a_name);
  geometry.bones = std::move(a_bones);
  return geometry;
}

[[nodiscard]] PieceRow Piece(FormID a_actor,
                             std::vector<GeometryRow> a_geometries) {
  PieceRow piece;
  piece.ref.actorID = a_actor;
  RecipeRow recipe;
  recipe.geometries = std::move(a_geometries);
  piece.recipes.push_back(std::move(recipe));
  return piece;
}

[[nodiscard]] const FormField *FieldNamed(const std::vector<FormField> &a_form,
                                          std::string_view a_name) {
  const auto found = std::ranges::find(a_form, a_name, &FormField::name);
  return found != a_form.end() ? &*found : nullptr;
}

void TestSkinBones() {
  const std::vector<PieceRow> pieces{
      Piece(7, {Geometry("Body", {{"NPC Spine2 [Spn2]", 0.2f},
                                  {"NPC Pelvis [Pelv]", 0.5f}}),
                Geometry("Cape", {{"NPC Spine2 [Spn2]", 0.6f}})}),
      Piece(9, {Geometry("Other", {{"NPC Head [Head]", 1.0f}})}),
  };
  const std::vector<BoneCoverage> skin = SkinBonesOf(pieces, 7);
  Check(skin.size() == 2, "SkinBonesOf merges a bone shared by two geometries");
  Check(!skin.empty() && skin[0].name == "NPC Spine2 [Spn2]" &&
            test::Near(skin[0].coverage, 0.6f),
        "a merged bone keeps its highest coverage and sorts first");
  Check(std::ranges::none_of(skin,
                             [](const BoneCoverage &a_bone) {
                               return a_bone.name == "NPC Head [Head]";
                             }),
        "SkinBonesOf ignores the pieces of another actor");
}

void TestNodeCatalog() {
  const std::vector<BoneCoverage> skin{{"NPC Spine2 [Spn2]", 0.6f},
                                       {"NPC Pelvis [Pelv]", 0.5f}};
  const std::vector<std::string> skeleton{
      "NPC Root [Root]",   "",
      "NPC Pelvis [Pelv]", "Camera3rd [Cam3]",
      "npc L Hand [LHnd]", "NPC Root [Root]"};
  const GameObjectCatalog catalog = NodeCatalogOf(skin, skeleton);
  const std::vector<std::string> expected{
      "NPC Spine2 [Spn2]", "NPC Pelvis [Pelv]", "Camera3rd [Cam3]",
      "npc L Hand [LHnd]", "NPC Root [Root]"};
  Check(catalog.kind == GameObjectKind::kNode,
        "the node catalog is a node kind");
  Check(ValuesOf(catalog) == expected,
        "skin bones come first by coverage, then the rest of the skeleton "
        "alphabetically, without blanks or repeats");
  Check(!catalog.candidates.empty() &&
            catalog.candidates[0].qualifier == "skin 60%",
        "a skin bone names its coverage as the qualifier");
  Check(catalog.candidates.size() == expected.size() &&
            catalog.candidates.back().qualifier == "skeleton",
        "a node outside the skin is qualified as skeleton");
  Check(NodeCatalogOf({}, {}).candidates.empty(),
        "an actor without 3D gives an empty node catalog");
}

void TestTextureCatalog() {
  Check(TexturePathKey("Data/Textures/Armor/Iron.DDS") ==
            "textures\\armor\\iron.dds",
        "TexturePathKey folds case, uses backslashes and drops data\\");
  Check(TexturePathKey("\\textures\\a.dds") == "textures\\a.dds",
        "TexturePathKey drops a leading separator");

  WornTextures worn;
  worn.materials = {
      {"Body", Slot::kDiffuse, "textures\\armor\\iron.dds"},
      {"Body", Slot::kNormal, "Textures/Armor/Iron_n.dds"},
      {"Cape", Slot::kDiffuse, "TEXTURES\\ARMOR\\IRON.DDS"},
      {"Cape", Slot::kEmissive, "(none)"},
      {"Cape", Slot::kRmaos, ""},
  };
  worn.shaders = {{"effects\\fxglow.dds", ""},
                  {"effects\\fxglow.dds", "effects\\gradient.dds"}};
  const GameObjectCatalog catalog = TextureCatalogOf(worn);
  const std::vector<std::string> expected{
      "textures\\armor\\iron.dds", "textures\\armor\\iron_n.dds",
      "effects\\fxglow.dds", "effects\\gradient.dds"};
  Check(catalog.kind == GameObjectKind::kTexture,
        "the texture catalog is a texture kind");
  Check(ValuesOf(catalog) == expected,
        "TextureCatalogOf lists each normalised path once, skipping "
        "placeholders, in material then effect shader order");
  Check(catalog.candidates.size() == expected.size() &&
            catalog.candidates[0].qualifier == "Body diffuse" &&
            catalog.candidates[2].qualifier == "effect shader fill" &&
            catalog.candidates[3].qualifier == "effect shader membrane",
        "each texture names the geometry slot or effect shader texture");
}

void TestGeometryNames() {
  RecipeRow recipe;
  recipe.geometries = {Geometry("Body", {}), Geometry("Cape", {}),
                       Geometry("Body", {}), Geometry("", {})};
  Check(GeometryNamesOf(recipe) == std::vector<std::string>{"Body", "Cape"},
        "GeometryNamesOf lists each named geometry once in order");

  const FormField geometry = SelectorClauseField(
      SelectorClauseRow{SelectorKind::kGeometry, "Bo*"}, {"Body", "Cape"});
  Check(geometry.names == std::vector<std::string>{"Body", "Cape"} &&
            !geometry.catalog && geometry.text == "Bo*",
        "a geometry clause offers the geometry names and keeps its glob");
  const FormField texture = SelectorClauseField(
      SelectorClauseRow{SelectorKind::kTexture, "*.dds"}, {"Body"});
  Check(texture.catalog == GameObjectKind::kTexture && texture.names.empty(),
        "a texture clause offers the texture catalog");
  const FormField addon = SelectorClauseField(
      SelectorClauseRow{SelectorKind::kAddon, "0x1~Skyrim.esm", true},
      {"Body"});
  Check(!addon.catalog && addon.names.empty(),
        "an addon clause stays a typed form reference");
}

void TestListAppend() {
  Check(AppendName("", "NPC Head [Head]") == "NPC Head [Head]",
        "appending to an empty list gives the one name");
  Check(AppendName("NPC Spine2 [Spn2],NPC Head [Head] ", "NPC Pelvis [Pelv]") ==
            "NPC Spine2 [Spn2], NPC Head [Head], NPC Pelvis [Pelv]",
        "appending adds the name at the end of the comma list");
  Check(AppendName("A, B", "A") == "A, B",
        "appending a name already listed leaves the list as it is");

  FormField replace{.name = "anchorNode", .text = "A"};
  Check(PickedText(replace, "B") == "B", "a replace field takes the pick");
  FormField append{.name = "bones", .text = "A"};
  append.catalogPick = CatalogPick::kAppend;
  Check(PickedText(append, "B") == "A, B", "an append field adds the pick");
}

void TestFieldsUseCatalogs() {
  SourceRow bake{.name = "weights",
                 .kind = BakeSourceRow{.bake = "boneWeight", .bones = "A"}};
  const std::vector<FormField> bakeForm = SourceForm(bake, SignalNames{});
  const FormField *bones = FieldNamed(bakeForm, "bones");
  Check(bones && bones->catalog == GameObjectKind::kNode &&
            bones->catalogPick == CatalogPick::kAppend,
        "a boneWeight bake picks bones from the node catalog by appending");

  SourceRow image{
      .name = "glow",
      .kind = ImageSourceRow{
          .path = "a.dds", .channel = "rgb", .space = "mesh", .mip = "0"}};
  const std::vector<FormField> imageForm = SourceForm(image, SignalNames{});
  const FormField *path = FieldNamed(imageForm, "path");
  Check(path && path->catalog == GameObjectKind::kTexture &&
            path->catalogPick == CatalogPick::kReplace,
        "an image path picks from the texture catalog");

  const std::vector<TermField> term =
      TermForm(BoneTerm{{"A"}}, MaskPresets{}, GeometryRow{});
  Check(!term.empty() && term[0].field.catalog == GameObjectKind::kNode &&
            term[0].field.catalogPick == CatalogPick::kAppend,
        "the bone mask term picks bones from the node catalog by appending");
}
}

int main() {
  TestSkinBones();
  TestNodeCatalog();
  TestTextureCatalog();
  TestGeometryNames();
  TestListAppend();
  TestFieldsUseCatalogs();
  return test::Finish("studio_actorcatalogs");
}
