// GPL-3.0-only with the additional permission in COPYING.md.
#include "studio/MenuState.h"
#include "studio/PaintSession.h"
#include "studio/PreviewWindows.h"
#include "test_support.h"

#include <string>
#include <vector>

using namespace BetterEnchantmentEffects;
using namespace BetterEnchantmentEffects::Studio;
using test::Check;

namespace {
constexpr PieceRef kWearer{1, 2, false};

Renumbering Removing(std::optional<LayerPlace> a_layer,
                     std::optional<std::size_t> a_output = std::nullopt) {
  Renumbering renumbering;
  for (std::size_t output = 0; output < 6; ++output) {
    OutputRenumbering entry;
    if (output != a_output) {
      entry.to = a_output && output > *a_output ? output - 1 : output;
    }
    for (std::size_t layer = 0; layer < 4; ++layer) {
      if (!a_layer || a_layer->output != output || layer < a_layer->layer) {
        entry.layers.emplace_back(layer);
      } else if (layer == a_layer->layer) {
        entry.layers.emplace_back(std::nullopt);
      } else {
        entry.layers.emplace_back(layer - 1);
      }
    }
    renumbering.outputs.push_back(std::move(entry));
  }
  return renumbering;
}

GeometryRow Armor() {
  GeometryRow geometry;
  geometry.name = "armor";
  PictureRow pattern;
  pattern.name = "pattern";
  pattern.texture = TextureHandle{11};
  pattern.channel = ShaderChannel::kR;
  geometry.sources.push_back(pattern);
  PictureRow spine;
  spine.name = "spine";
  spine.texture = TextureHandle{12};
  geometry.masks.push_back(spine);
  OutputRow output;
  output.index = 3;
  output.surface = Surface::kMaterial;
  output.slot = Slot::kEmissive;
  output.texture = TextureHandle{13};
  output.animated = true;
  output.layers.resize(2);
  output.layers[1].texture = TextureHandle{14};
  geometry.outputs.push_back(output);
  OutputRow light;
  light.index = 5;
  light.target = Target::kLight;
  geometry.outputs.push_back(light);
  return geometry;
}

Snapshot Live() {
  RecipeRow recipe;
  recipe.id = "glow";
  recipe.geometries.push_back(Armor());
  GeometryRow boots;
  boots.name = "boots";
  boots.sources.push_back(recipe.geometries.front().sources.front());
  recipe.geometries.push_back(boots);
  PieceRow piece;
  piece.ref = kWearer;
  piece.recipes.push_back(recipe);
  Snapshot snapshot;
  snapshot.pieces.push_back(piece);
  return snapshot;
}

PreviewTarget Target(PreviewSubject a_subject) {
  return PreviewTarget{kWearer, "glow", "armor", std::move(a_subject)};
}

const PreviewPicture *
PictureOf(const std::optional<PreviewContent> &a_content) {
  return a_content ? Get<PreviewPicture>(*a_content) : nullptr;
}
const PreviewPicture *PictureOf(std::optional<PreviewContent> &&) = delete;

void PinsToggleAndStopAtTheLimit() {
  PreviewWindows windows;
  const PreviewTarget source = Target(SourceSubject{"pattern"});
  Check(TogglePin(windows, source) == PinOutcome::kPinned &&
            IsPinned(windows, source),
        "pinning a source opens a window for it");
  PreviewTarget elsewhere = source;
  elsewhere.geometry = "boots";
  Check(IsPinned(windows, elsewhere),
        "a window keeps its pin when its geometry is changed");
  Check(TogglePin(windows, source) == PinOutcome::kUnpinned &&
            windows.open.empty(),
        "pinning a pinned subject closes its window");
  for (std::size_t i = 0; i < kMaxPreviewWindows; ++i) {
    static_cast<void>(
        TogglePin(windows, Target(MaskSubject{std::to_string(i)})));
  }
  Check(IsFull(windows) && windows.open.size() == kMaxPreviewWindows,
        "eight windows fill the preview limit");
  Check(TogglePin(windows, source) == PinOutcome::kRefusedFull &&
            !IsPinned(windows, source) &&
            windows.open.size() == kMaxPreviewWindows,
        "a ninth pin is refused and changes nothing");
  Check(TogglePin(windows, Target(MaskSubject{"0"})) == PinOutcome::kUnpinned,
        "a full set still unpins");
  Check(windows.open.back().id > windows.open.front().id,
        "window ids are never reused");
  const std::uint64_t closing = windows.open.front().id;
  ClosePreview(windows, closing);
  Check(!FindPreview(windows, closing) &&
            windows.open.size() == kMaxPreviewWindows - 2,
        "closing a window removes only that window");
}

void TargetsNeedALiveGeometry() {
  Selection selection;
  selection.piece = kWearer;
  RecipeRow recipe;
  recipe.id = "glow";
  const GeometryRow geometry = Armor();
  Check(!PreviewTargetFor(selection, &recipe, nullptr, SourceSubject{"a"}),
        "nothing is pinnable without an applied geometry");
  const std::optional<PreviewTarget> target =
      PreviewTargetFor(selection, &recipe, &geometry, OutputSubject{3});
  Check(target && target->recipeID == "glow" && target->geometry == "armor" &&
            target->piece == kWearer,
        "a pin names the wearer, recipe and geometry");
  selection.document = true;
  Check(!PreviewTargetFor(selection, &recipe, &geometry, OutputSubject{3}),
        "an unapplied document is not pinnable");
}

void ResolvesEachSubject() {
  const Snapshot snapshot = Live();
  const std::optional<PreviewContent> sourceContent =
      ResolvePreview(snapshot, Target(SourceSubject{"pattern"}));
  const PreviewPicture *source = PictureOf(sourceContent);
  Check(source && source->texture == TextureHandle{11} &&
            source->channel == ShaderChannel::kR,
        "a source shows its texture in its own channel");
  const std::optional<PreviewContent> outputContent =
      ResolvePreview(snapshot, Target(OutputSubject{3}));
  const PreviewPicture *output = PictureOf(outputContent);
  Check(output && output->texture == TextureHandle{13} && output->animated &&
            output->channel == ShaderChannel::kRgb,
        "an output shows its composite in colour");
  const std::optional<PreviewContent> layerContent =
      ResolvePreview(snapshot, Target(LayerSubject{3, 1}));
  const PreviewPicture *layer = PictureOf(layerContent);
  Check(layer && layer->texture == TextureHandle{14} && layer->animated,
        "a layer shows its own texture and follows its output's animation");
  Check(!ResolvePreview(snapshot, Target(LayerSubject{3, 2})),
        "a layer past the end of the stack resolves to nothing");
  Check(!ResolvePreview(snapshot, Target(OutputSubject{5})),
        "a light has no texture to preview");
  Check(!ResolvePreview(snapshot, Target(MaskSubject{"gone"})),
        "a removed mask resolves to nothing");
  Check(PreviewLabel(snapshot, Target(LayerSubject{3, 1})) ==
            "glow: material / emissive / layer 2",
        "a layer window names its recipe, output and layer");
  PreviewTarget draft{kWearer, std::string{kPaintRecipe}, "armor",
                      MaskSubject{std::string{kScratchMask}}};
  Check(PreviewLabel(snapshot, draft) == "mask draft",
        "the paint draft is named as a draft");
}

void OffersGeometriesThatCarryTheSubject() {
  const Snapshot snapshot = Live();
  Check(PreviewGeometries(snapshot, Target(SourceSubject{"pattern"})) ==
            std::vector<std::string>{"armor", "boots"},
        "a source is offered on every geometry that carries it");
  Check(PreviewGeometries(snapshot, Target(OutputSubject{3})) ==
            std::vector<std::string>{"armor"},
        "an output is offered only where it is applied");
}

void KeepsWindowsOfPiecesOutOfView() {
  Snapshot snapshot = Live();
  snapshot.pieces.front().recipes.front().geometries.clear();
  PreviewWindows windows;
  static_cast<void>(TogglePin(windows, Target(SourceSubject{"pattern"})));
  const std::optional<PreviewContent> content =
      ResolvePreview(snapshot, windows.open.front().target);
  Check(content && Is<PieceNotViewed>(*content),
        "a piece published without geometry waits instead of closing");
  DropVanishedPreviews(windows, snapshot);
  Check(windows.open.size() == 1, "a waiting window stays open");
  snapshot.pieces.clear();
  DropVanishedPreviews(windows, snapshot);
  Check(windows.open.empty(), "a window closes when its piece is gone");
}

void DropsVanishedSubjects() {
  const Snapshot snapshot = Live();
  PreviewWindows windows;
  static_cast<void>(TogglePin(windows, Target(SourceSubject{"pattern"})));
  static_cast<void>(TogglePin(windows, Target(SourceSubject{"gone"})));
  PreviewTarget otherGeometry = Target(OutputSubject{3});
  otherGeometry.geometry = "gloves";
  static_cast<void>(TogglePin(windows, otherGeometry));
  PreviewTarget otherWearer = Target(MaskSubject{"spine"});
  otherWearer.piece.actorID = 99;
  static_cast<void>(TogglePin(windows, otherWearer));
  DropVanishedPreviews(windows, snapshot);
  Check(windows.open.size() == 1 &&
            Is<SourceSubject>(windows.open.front().target.subject),
        "windows whose resource, geometry or wearer vanished are closed");
}

void RenumbersWindowsWithTheirLayers() {
  PreviewWindows windows;
  static_cast<void>(TogglePin(windows, Target(SourceSubject{"pattern"})));
  static_cast<void>(TogglePin(windows, Target(OutputSubject{3})));
  static_cast<void>(TogglePin(windows, Target(LayerSubject{3, 1})));
  static_cast<void>(TogglePin(windows, Target(LayerSubject{4, 0})));
  RenumberPreviews(windows, "other", Removing(LayerPlace{3, 0}));
  Check(IsPinned(windows, Target(LayerSubject{3, 1})),
        "another recipe's edit moves nothing");
  RenumberPreviews(windows, "glow", Removing(LayerPlace{3, 0}));
  Check(windows.open.size() == 4 &&
            IsPinned(windows, Target(LayerSubject{3, 0})) &&
            IsPinned(windows, Target(LayerSubject{4, 0})) &&
            IsPinned(windows, Target(OutputSubject{3})),
        "a layer removal moves the later layer's window down");
  RenumberPreviews(windows, "glow", Removing(LayerPlace{3, 0}));
  Check(windows.open.size() == 3 &&
            !IsPinned(windows, Target(LayerSubject{3, 0})),
        "the removed layer's window closes");
  RenumberPreviews(windows, "glow", Removing(std::nullopt, 3));
  Check(windows.open.size() == 2 &&
            IsPinned(windows, Target(SourceSubject{"pattern"})) &&
            IsPinned(windows, Target(LayerSubject{3, 0})),
        "an output removal closes its windows and renumbers later outputs");
  DropAllIndexedPreviews(windows);
  Check(windows.open.size() == 1, "a reload closes every indexed window");
}

void MenuStateKeepsWindowsAcrossSelection() {
  MenuState state;
  static_cast<void>(TogglePin(state.previews, Target(OutputSubject{3})));
  Reduce(state, PickRecipe{"other", false});
  Reduce(state, PickPiece{PieceRef{7, 8, false}});
  Check(state.previews.open.size() == 1,
        "pinned windows survive a change of recipe or piece");
  Reduce(state, EditRecipe{"glow", {AddLayer{3, {}, std::nullopt}}});
  Check(state.previews.open.size() == 1,
        "adding a layer keeps the output's window");
  Reduce(state, Undo{"glow"});
  Check(state.previews.open.size() == 1,
        "undo keeps the output window until the editor answers");
  state.pendingIndexedEdit = PendingIndexedEdit{3, "glow"};
  Snapshot answered;
  answered.editResults.push_back(
      {3, "glow", std::nullopt, Removing(std::nullopt, 3)});
  AcknowledgeEditorOperations(state, answered);
  Check(state.previews.open.empty(),
        "an undo that removes the output closes its window");
}

void TiersAndChannels() {
  Check(PixelsOf(PreviewTier::kSmall) == 128 &&
            PixelsOf(PreviewTier::kMedium) == 256 &&
            PixelsOf(PreviewTier::kLarge) == 512,
        "the size tiers are 128, 256 and 512 pixels");
  Check(PreviewChannelName(ShaderChannel::kA) == "a" &&
            PreviewChannelName(ShaderChannel::kRgb) == "rgb",
        "channels are named for the picker");
  Check(PreviewWindow{}.tier == PreviewTier::kMedium &&
            !PreviewWindow{}.channel && !PreviewWindow{}.position,
        "a new window is medium, in its subject's channel, placed by the menu");
}
}

int main() {
  PinsToggleAndStopAtTheLimit();
  TargetsNeedALiveGeometry();
  ResolvesEachSubject();
  OffersGeometriesThatCarryTheSubject();
  KeepsWindowsOfPiecesOutOfView();
  DropsVanishedSubjects();
  RenumbersWindowsWithTheirLayers();
  MenuStateKeepsWindowsAcrossSelection();
  TiersAndChannels();
  return test::Finish("studio_previewwindows");
}
