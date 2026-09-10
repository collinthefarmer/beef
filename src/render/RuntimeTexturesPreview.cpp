#include "RuntimeTextures.h"

namespace BetterEnchantmentEffects {
std::shared_ptr<TextureLab::RenderTarget>
TextureLab::Preview(RE::NiSourceTexture *a_source, ShaderChannel a_channel,
                    bool a_dynamic) {
  if (!a_source || !available_) {
    return nullptr;
  }
  std::scoped_lock lock{previewLock_};
  auto &entry = previews_[{a_source, a_channel}];
  entry.dynamic = entry.dynamic || a_dynamic;
  entry.wanted = true;
  return entry.target;
}

void TextureLab::RenderPreviews() {
  if (!Init()) {
    return;
  }
  ++previewTick_;
  const auto generation = previewGeneration_.load(std::memory_order_relaxed);
  std::vector<std::pair<PreviewKey, std::shared_ptr<RenderTarget>>> work;
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
        entry.target = Acquire(TextureSize(128));
        if (!entry.target) {
          continue;
        }
      }
      entry.generation = generation;
      work.emplace_back(key, entry.target);
    }
    std::erase_if(previewGraveyard_, [&](const auto &a_dead) {
      return previewTick_ - a_dead.second > 8;
    });
  }
  for (const auto &[key, target] : work) {
    LayerParams p;
    p.mode = Mode::kChannel;
    p.map = {key.first, MapReading::kRmaos};
    p.channel.channel = key.second;
    Render(*target, nullptr, p);
  }
}

void TextureLab::ClearPreviews() {
  std::scoped_lock lock{previewLock_};
  for (auto &[key, entry] : previews_) {
    if (entry.target) {
      previewGraveyard_.emplace_back(std::move(entry.target), previewTick_);
    }
  }
  previews_.clear();
}

void TextureLab::InvalidatePreviews() noexcept {
  previewGeneration_.fetch_add(1, std::memory_order_relaxed);
}
}
