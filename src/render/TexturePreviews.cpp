#include "render/TexturePreviews.h"

#include <algorithm>
#include "recipe/Recipe.h"

namespace BetterEnchantmentEffects {
TexturePreviews::TexturePreviews(TextureLab &a_renderer)
    : renderer_(a_renderer) {}

std::shared_ptr<TextureLab::RenderTarget>
TexturePreviews::Preview(RE::NiSourceTexture *a_source, ShaderChannel a_channel,
                         bool a_dynamic) {
  if (!a_source || !renderer_.Available()) {
    return nullptr;
  }
  TextureRef retained{a_source};
  if (!retained) {
    return nullptr;
  }
  std::scoped_lock lock{previewLock_};
  auto &entry = previews_[{a_source, a_channel, {}}];
  entry.source = std::move(retained);
  entry.dynamic = entry.dynamic || a_dynamic;
  entry.wanted = true;
  return entry.ready ? entry.target : nullptr;
}

std::shared_ptr<TextureLab::RenderTarget>
TexturePreviews::SampledPreview(std::string a_context, RE::NiSourceTexture *a_source,
                                const TextureLab::LayerInput &a_sampling,
                                float a_normalize, bool a_dynamic) {
  if (!a_source || a_context.empty() || !renderer_.Available()) {
    return nullptr;
  }
  TextureRef retained{a_source};
  if (!retained) {
    return nullptr;
  }
  std::scoped_lock lock{previewLock_};
  auto &entry = previews_[{a_source, a_sampling.channel, std::move(a_context)}];
  const Sampling sampling{a_sampling, a_normalize};
  entry.dirty = entry.dirty || entry.sampling != sampling;
  entry.sampling = sampling;
  entry.source = std::move(retained);
  entry.dynamic = a_dynamic;
  entry.wanted = true;
  return entry.ready ? entry.target : nullptr;
}

TextureLab::PreviewDraw *
TexturePreviews::RetainDraw(std::shared_ptr<RenderTarget> a_target) {
  auto *ticket = draws_.Retain(std::move(a_target));
  if (!ticket && !drawPressure_.exchange(true)) {
    logger::warn("TextureLab: preview draw retention limit reached");
  } else if (ticket) {
    drawPressure_ = false;
  }
  return ticket;
}
void TexturePreviews::CollectDraws() { draws_.Collect(); }

void TexturePreviews::ExpireUnused(std::uint64_t a_generation) {
  if (a_generation == previewSeen_)
    return;
  std::erase_if(previews_, [&](const auto &a_pair) {
    return !a_pair.second.wanted && a_pair.second.generation < previewSeen_;
  });
  previewSeen_ = a_generation;
}

std::optional<TexturePreviews::PreviewWork>
TexturePreviews::PrepareRequest(const PreviewKey &a_key, PreviewEntry &a_entry,
                                std::uint64_t a_generation) {
  if (!a_entry.wanted)
    return std::nullopt;
  a_entry.wanted = false;
  if (a_entry.target && a_entry.generation == a_generation && !a_entry.dynamic &&
      !a_entry.dirty && a_entry.ready)
    return std::nullopt;
  if (!a_entry.target)
    a_entry.target = renderer_.Acquire(TextureSize(128));
  if (!a_entry.target)
    return std::nullopt;
  if (a_entry.generation != a_generation) {
    Trace::Safely([&] {
      Trace::Emit(
          Trace::Event::kPreview,
          {{"action", "render_request"},
           {"generation", std::to_string(a_generation)},
           {"source", Trace::Pointer(a_entry.source.get())},
           {"source_generation", std::to_string(a_entry.source.Generation())},
           {"source_renderer",
            Trace::Pointer(a_entry.source ? a_entry.source->rendererTexture
                                          : nullptr)},
           {"target", Trace::Pointer(a_entry.target.get())},
           {"channel", std::to_string(static_cast<unsigned>(std::get<1>(a_key)))}});
    });
  }
  a_entry.generation = a_generation;
  a_entry.dirty = false;
  return PreviewWork{a_key, a_entry.source, std::get<1>(a_key), a_entry.target,
                     a_entry.sampling};
}

void TexturePreviews::RenderPreviews() {
  if (!renderer_.Init())
    return;
  CollectDraws();
  const auto generation = previewGeneration_.load(std::memory_order_relaxed);
  std::vector<PreviewWork> work;
  {
    std::scoped_lock lock{previewLock_};
    ExpireUnused(generation);
    for (auto &[key, entry] : previews_) {
      if (auto request = PrepareRequest(key, entry, generation)) {
        work.push_back(std::move(*request));
      }
    }
  }
  for (const auto &entry : work) {
    TextureLab::LayerParams p;
    if (entry.sampling) {
      p.mode = TextureLab::Mode::kLayer;
      p.layer.source = entry.source.get();
      p.layer.input = entry.sampling->input;
      p.layer.normalize = entry.sampling->normalize;
      p.layer.blend = BlendShaderMode(Blend::kReplace);
    } else {
      p.mode = TextureLab::Mode::kChannel;
      p.map = {entry.source.get(), TextureLab::MapReading::kRmaos};
      p.channel.channel = entry.channel;
    }
    const bool rendered = renderer_.Render(*entry.target, nullptr, p);
    std::scoped_lock lock{previewLock_};
    const auto found = previews_.find(entry.key);
    if (found != previews_.end() && found->second.target == entry.target) {
      found->second.ready = rendered;
      found->second.dirty = found->second.dirty || !rendered;
    }
  }
}

void TexturePreviews::ClearPreviews() {
  std::scoped_lock lock{previewLock_};
  previews_.clear();
}

void TexturePreviews::InvalidatePreviews() noexcept {
  previewGeneration_.fetch_add(1, std::memory_order_relaxed);
}
}
