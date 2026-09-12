#pragma once

#include "render/RuntimeTextures.h"

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
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
  void RenderPreviews();
  void ClearPreviews();
  void InvalidatePreviews() noexcept;

private:
  TextureLab &renderer_;
  using PreviewKey = std::pair<RE::NiSourceTexture *, ShaderChannel>;
  struct PreviewEntry {
    RE::NiPointer<RE::NiSourceTexture> source;
    std::shared_ptr<RenderTarget> target;
    std::uint64_t generation = 0;
    bool dynamic = false;
    bool wanted = false;
  };
  std::mutex previewLock_;
  std::map<PreviewKey, PreviewEntry> previews_;
  std::atomic<std::uint64_t> previewGeneration_{1};
  std::uint64_t previewSeen_ = 1;
  std::vector<std::pair<std::shared_ptr<RenderTarget>, std::uint64_t>>
      previewGraveyard_;
  std::uint64_t previewTick_ = 0;
};
}
