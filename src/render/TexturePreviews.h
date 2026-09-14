#pragma once

#include "render/TextureRef.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects {
class TexturePreviews {
public:
  using RenderTarget = TextureLab::RenderTarget;

  explicit TexturePreviews(TextureLab &a_renderer);
  [[nodiscard]] std::shared_ptr<RenderTarget>
  Preview(RE::NiSourceTexture *a_source, ShaderChannel a_channel,
          bool a_dynamic);
  [[nodiscard]] std::shared_ptr<RenderTarget>
  SampledPreview(std::string a_context, RE::NiSourceTexture *a_source,
                 const TextureLab::LayerInput &a_sampling, float a_normalize,
                 bool a_dynamic);
  [[nodiscard]] TextureLab::PreviewDraw *
  RetainDraw(std::shared_ptr<RenderTarget> a_target);
  void CollectDraws();
  void RenderPreviews();
  void ClearPreviews();
  void InvalidatePreviews() noexcept;

private:
  TextureLab &renderer_;
  using PreviewKey =
      std::tuple<RE::NiSourceTexture *, ShaderChannel, std::string>;
  struct Sampling {
    TextureLab::LayerInput input;
    float normalize = 1.0f;
    [[nodiscard]] bool operator==(const Sampling &) const = default;
  };
  struct PreviewEntry {
    TextureRef source;
    std::shared_ptr<RenderTarget> target;
    std::uint64_t generation = 0;
    bool dynamic = false;
    bool wanted = false;
    bool ready = false;
    bool dirty = false;
    std::optional<Sampling> sampling;
  };
  struct PreviewWork {
    PreviewKey key;
    TextureRef source;
    ShaderChannel channel;
    std::shared_ptr<RenderTarget> target;
    std::optional<Sampling> sampling;
  };
  [[nodiscard]] PreviewEntry *FindOrAdd(const PreviewKey &a_key);
  void ExpireUnused(std::uint64_t a_generation);
  [[nodiscard]] std::optional<PreviewWork>
  PrepareRequest(const PreviewKey &a_key, PreviewEntry &a_entry,
                 std::uint64_t a_generation);
  std::mutex previewLock_;
  std::map<PreviewKey, PreviewEntry> previews_;
  std::atomic<std::uint64_t> previewGeneration_{1};
  std::uint64_t previewSeen_ = 1;
  ConsumptionLeases<RenderTarget> draws_{2048};
  std::atomic<bool> drawPressure_{false};
};
}
