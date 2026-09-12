#include "render/TexturePreviews.h"

#include <algorithm>

namespace BetterEnchantmentEffects {
TexturePreviews::TexturePreviews(TextureLab &a_renderer)
    : renderer_(a_renderer) {}

std::shared_ptr<TextureLab::RenderTarget>
TexturePreviews::Preview(RE::NiSourceTexture *a_source, ShaderChannel a_channel,
                         bool a_dynamic) {
  if (!a_source || !renderer_.Available()) {
    return nullptr;
  }
  std::scoped_lock lock{previewLock_};
  auto &entry = previews_[{a_source, a_channel}];
  entry.source = RE::NiPointer<RE::NiSourceTexture>{a_source};
  entry.dynamic = entry.dynamic || a_dynamic;
  entry.wanted = true;
  return entry.target;
}

void TexturePreviews::RenderPreviews() {
  if (!renderer_.Init()) {
    return;
  }
  ++previewTick_;
  const auto generation = previewGeneration_.load(std::memory_order_relaxed);
  struct PreviewWork {
    RE::NiPointer<RE::NiSourceTexture> source;
    ShaderChannel channel;
    std::shared_ptr<RenderTarget> target;
  };
  std::vector<PreviewWork> work;
  {
    std::scoped_lock lock{previewLock_};
    if (generation != previewSeen_) {
      for (auto it = previews_.begin(); it != previews_.end();) {
        if (!it->second.wanted && it->second.generation < previewSeen_) {
          if (it->second.target) {
            previewGraveyard_.emplace_back(std::move(it->second.target),
                                           previewTick_);
          }
          it = previews_.erase(it);
        } else {
          ++it;
        }
      }
      previewSeen_ = generation;
    }
    for (auto &[key, entry] : previews_) {
      if (!entry.wanted) {
        continue;
      }
      entry.wanted = false;
      const bool stale =
          !entry.target || entry.generation != generation || entry.dynamic;
      if (!stale) {
        continue;
      }
      if (!entry.target) {
        entry.target = renderer_.Acquire(TextureSize(128));
        if (!entry.target) {
          continue;
        }
      }
      entry.generation = generation;
      work.push_back({entry.source, key.second, entry.target});
    }
    std::erase_if(previewGraveyard_, [&](const auto &a_dead) {
      return previewTick_ - a_dead.second > 8;
    });
  }
  for (const auto &entry : work) {
    TextureLab::LayerParams p;
    p.mode = TextureLab::Mode::kChannel;
    p.map = {entry.source.get(), TextureLab::MapReading::kRmaos};
    p.channel.channel = entry.channel;
    renderer_.Render(*entry.target, nullptr, p);
  }
}

void TexturePreviews::ClearPreviews() {
  std::scoped_lock lock{previewLock_};
  for (auto &[key, entry] : previews_) {
    if (entry.target) {
      previewGraveyard_.emplace_back(std::move(entry.target), previewTick_);
    }
  }
  previews_.clear();
}

void TexturePreviews::InvalidatePreviews() noexcept {
  previewGeneration_.fetch_add(1, std::memory_order_relaxed);
}
}
