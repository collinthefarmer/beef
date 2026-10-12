// GPL-3.0-only with the additional permission in COPYING.md.
#include "SettingsPublication.h"
#include "engine/TextFile.h"
#include "test_support.h"

#include <atomic>
#include <barrier>
#include <limits>
#include <thread>
#include <type_traits>
#include <vector>

using namespace BetterEnchantmentEffects;
using test::Check;

static_assert(!std::is_copy_constructible_v<SettingsPublication>);
static_assert(!std::is_reference_v<decltype(SettingsPublication{}.Read())>);

int main() {
  SettingsPublication publication;
  const Settings initial = publication.Read();
  Settings draft = initial;
  draft.animationFPS = 15;
  draft.animationSpeed = 2.0f;
  draft.playerOnly = true;
  publication.Publish(draft);
  const Settings operation = publication.Read();
  draft.animationFPS = 30;
  Check(operation.animationFPS == 15, "publication copies the draft");
  Check(initial.animationFPS == 60 && !initial.playerOnly,
        "old snapshots survive publication");
  publication.Publish(draft);
  Check(operation.animationFPS == 15 && operation.animationSpeed == 2.0f,
        "an operation retains its original settings");
  Check(publication.Read().animationFPS == 30,
        "a later operation sees the new settings");

  draft.animationFPS = 0;
  draft.animationSpeed = -1.0f;
  draft.textureScale = static_cast<TextureScale>(-1);
  publication.Publish(draft);
  const Settings low = publication.Read();
  Check(low.animationFPS == kMinAnimationFPS, "publication bounds zero FPS");
  Check(low.animationSpeed == kMinAnimationSpeed,
        "publication bounds low speed");
  Check(low.textureScale == TextureScale::kFull,
        "publication repairs invalid scale");
  Check(draft.TickIntervalMS() == 1000u / kMinAnimationFPS,
        "unpublished zero FPS never divides by zero");
  draft.animationFPS = std::numeric_limits<std::uint32_t>::max();
  draft.animationSpeed = std::numeric_limits<float>::max();
  publication.Publish(draft);
  Check(publication.Read().animationFPS == kMaxAnimationFPS,
        "publication bounds excessive FPS");
  Check(publication.Read().animationSpeed == kMaxAnimationSpeed,
        "publication bounds excessive speed");
  for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                              std::numeric_limits<float>::infinity(),
                              -std::numeric_limits<float>::infinity()}) {
    draft.animationSpeed = invalid;
    publication.Publish(draft);
    Check(publication.Read().animationSpeed == Settings{}.animationSpeed,
          "non-finite speed uses default");
  }
  for (const std::string_view invalid : {"nan", "inf", "-inf", "1e999"}) {
    const Settings parsed =
        Settings::Parse("AnimationFPS=" + std::string{invalid} +
                        "\nAnimationSpeed=" + std::string{invalid});
    Check(parsed.animationFPS == Settings{}.animationFPS &&
              parsed.animationSpeed == Settings{}.animationSpeed,
          "non-finite INI values keep defaults before integer conversion");
  }
  const Settings parsed = Settings::Parse("AnimationFPS=0\nAnimationSpeed=999");
  Check(parsed.animationFPS == kMinAnimationFPS &&
            parsed.animationSpeed == kMaxAnimationSpeed,
        "parser and publication share numeric bounds");
  Check(Settings{}.unpublishedDrawBudgetMs == 4,
        "the unpublished draw budget defaults to 4 ms");
  Check(Settings::Parse("UnpublishedDrawBudgetMs=0").unpublishedDrawBudgetMs ==
                kMinUnpublishedDrawBudgetMs &&
            Settings::Parse("UnpublishedDrawBudgetMs=99")
                    .unpublishedDrawBudgetMs == kMaxUnpublishedDrawBudgetMs &&
            Settings::Parse("UnpublishedDrawBudgetMs=8")
                    .unpublishedDrawBudgetMs == 8,
        "the parser bounds the unpublished draw budget to 1 to 16 ms");
  Settings unbounded;
  unbounded.unpublishedDrawBudgetMs = 0;
  publication.Publish(unbounded);
  Check(publication.Read().unpublishedDrawBudgetMs ==
            kMinUnpublishedDrawBudgetMs,
        "publication bounds a zero unpublished draw budget");
  unbounded.unpublishedDrawBudgetMs = 1000;
  publication.Publish(unbounded);
  Check(publication.Read().unpublishedDrawBudgetMs ==
            kMaxUnpublishedDrawBudgetMs,
        "publication bounds an excessive unpublished draw budget");
  Check(!SettingsDiffer(parsed, Settings::Parse(parsed.Serialize())),
        "checked settings round trip through INI");
  Settings speedOnly = parsed;
  speedOnly.animationSpeed = parsed.animationSpeed * 0.5f + 0.25f;
  Check(SettingsDiffer(parsed, speedOnly) && !ReapplyDiffers(parsed, speedOnly),
        "a setting read per tick needs no reapply");
  Settings playerOnly = parsed;
  playerOnly.playerOnly = !parsed.playerOnly;
  Check(ReapplyDiffers(parsed, playerOnly),
        "a setting read at apply time needs a reapply");
  Check(ReadText("BetterEnchantmentEffects.ini").value_or("") ==
            Settings{}.Serialize(),
        "the shipped INI is the serialized defaults, every row read");

  Settings first;
  first.animationFPS = 15;
  first.animationSpeed = 0.5f;
  first.playerOnly = false;
  first.textureScale = TextureScale::kQuarter;
  Settings second;
  second.animationFPS = 60;
  second.animationSpeed = 4.0f;
  second.playerOnly = true;
  second.textureScale = TextureScale::kFull;
  publication.Publish(first);
  std::barrier start{4};
  std::atomic<bool> coherent{true};
  std::vector<std::jthread> workers;
  for (int writer = 0; writer < 2; ++writer) {
    workers.emplace_back([&, writer] {
      start.arrive_and_wait();
      for (int i = 0; i < 20000; ++i) {
        publication.Publish(writer == 0 ? first : second);
      }
    });
  }
  for (int reader = 0; reader < 2; ++reader) {
    workers.emplace_back([&] {
      start.arrive_and_wait();
      for (int i = 0; i < 20000; ++i) {
        const Settings held = publication.Read();
        const Settings &expected = held.playerOnly ? second : first;
        if (held.animationFPS != expected.animationFPS ||
            held.animationSpeed != expected.animationSpeed ||
            held.textureScale != expected.textureScale) {
          coherent.store(false);
        }
      }
    });
  }
  workers.clear();
  Check(coherent.load(), "concurrent readers see complete publications");
  return test::Finish("settings publication");
}
