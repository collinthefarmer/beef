// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/TextureLab.h"

#include "diagnostics/Metrics.h"
#include "render/D3DResult.h"

#include <REX/W32/COMPTR.h>

#include <cstring>
#include <utility>

namespace BetterEnchantmentEffects {
using namespace REX::W32;

namespace {
class ReadbackMeter {
public:
  explicit ReadbackMeter(std::string_view a_op) noexcept : op_(a_op) {}
  ~ReadbackMeter() {
    const std::uint64_t micros = watch_.Micros();
    Metrics::CountReadback(micros);
    Trace::EmitSafely(Trace::Event::kMetrics,
                      {{"action", "readback"},
                       {"op", std::string{op_}},
                       {"us", std::to_string(micros)},
                       {"lock_wait_us", std::to_string(lockWaitMicros_)},
                       {"lock_held_us", std::to_string(lockHeldMicros_)},
                       {"map_us", std::to_string(mapMicros_)},
                       {"map_attempted", mapAttempted_ ? "true" : "false"},
                       {"map_succeeded", mapSucceeded_ ? "true" : "false"},
                       {"success", success_ ? "true" : "false"},
                       {"bytes", std::to_string(bytes_)}});
  }
  ReadbackMeter(const ReadbackMeter &) = delete;
  ReadbackMeter &operator=(const ReadbackMeter &) = delete;
  ReadbackMeter(ReadbackMeter &&) = delete;
  ReadbackMeter &operator=(ReadbackMeter &&) = delete;

  void Locked(std::uint64_t a_waitMicros) noexcept {
    lockWaitMicros_ = a_waitMicros;
  }
  void Unlocked(std::uint64_t a_heldMicros) noexcept {
    lockHeldMicros_ = a_heldMicros;
  }
  void Mapped(std::uint64_t a_micros, bool a_success) noexcept {
    mapMicros_ = a_micros;
    mapAttempted_ = true;
    mapSucceeded_ = a_success;
  }
  void Succeeded(std::uint64_t a_bytes) noexcept {
    bytes_ = a_bytes;
    success_ = true;
  }

private:
  std::uint64_t lockWaitMicros_ = 0;
  std::uint64_t lockHeldMicros_ = 0;
  std::uint64_t mapMicros_ = 0;
  std::uint64_t bytes_ = 0;
  bool mapAttempted_ = false;
  bool mapSucceeded_ = false;
  bool success_ = false;
  Metrics::Stopwatch watch_;
  std::string_view op_;
};

class RendererLock {
public:
  explicit RendererLock(ReadbackMeter *a_meter = nullptr)
      : renderer_(RE::BSGraphics::Renderer::GetSingleton()), meter_(a_meter) {
    if (renderer_) {
      const Metrics::Stopwatch wait;
      renderer_->Lock();
      lockedAtMicros_ = watch_.Micros();
      if (meter_) {
        meter_->Locked(wait.Micros());
      }
    }
  }
  ~RendererLock() {
    if (renderer_) {
      const std::uint64_t heldMicros = watch_.Micros() - lockedAtMicros_;
      renderer_->Unlock();
      if (meter_) {
        meter_->Unlocked(heldMicros);
      }
    }
  }
  RendererLock(const RendererLock &) = delete;
  RendererLock &operator=(const RendererLock &) = delete;
  RendererLock(RendererLock &&) = delete;
  RendererLock &operator=(RendererLock &&) = delete;

private:
  RE::BSGraphics::Renderer *renderer_ = nullptr;
  ReadbackMeter *meter_ = nullptr;
  Metrics::Stopwatch watch_;
  std::uint64_t lockedAtMicros_ = 0;
};

class UnconditionalReadback {
public:
  explicit UnconditionalReadback(ID3D11DeviceContext *a_context)
      : borrowedContext_(a_context) {
    borrowedContext_->GetPredication(predicate_.GetAddressOf(), &value_);
    borrowedContext_->SetPredication(nullptr, false);
  }
  ~UnconditionalReadback() {
    borrowedContext_->SetPredication(predicate_.Get(), value_);
  }
  UnconditionalReadback(const UnconditionalReadback &) = delete;
  UnconditionalReadback &operator=(const UnconditionalReadback &) = delete;
  UnconditionalReadback(UnconditionalReadback &&) = delete;
  UnconditionalReadback &operator=(UnconditionalReadback &&) = delete;

private:
  ID3D11DeviceContext *borrowedContext_;
  ComPtr<ID3D11Predicate> predicate_;
  REX::W32::BOOL value_ = false;
};

class ReadMapping {
public:
  ReadMapping(ID3D11DeviceContext *a_context, ID3D11Resource *a_resource,
              ReadbackMeter &a_meter, std::uint32_t a_flags = 0)
      : borrowedContext_(a_context), resource_(a_resource) {
    const Metrics::Stopwatch watch;
    active_ = !Failed(
        borrowedContext_->Map(resource_, 0, D3D11_MAP_READ, a_flags, &mapped_));
    a_meter.Mapped(watch.Micros(), active_);
  }
  ~ReadMapping() {
    if (active_) {
      borrowedContext_->Unmap(resource_, 0);
    }
  }
  ReadMapping(const ReadMapping &) = delete;
  ReadMapping &operator=(const ReadMapping &) = delete;
  ReadMapping(ReadMapping &&) = delete;
  ReadMapping &operator=(ReadMapping &&) = delete;

  [[nodiscard]] const std::uint8_t *Data() const noexcept {
    return active_ ? static_cast<const std::uint8_t *>(mapped_.data) : nullptr;
  }
  [[nodiscard]] std::uint32_t RowPitch() const noexcept {
    return mapped_.rowPitch;
  }

private:
  ID3D11DeviceContext *borrowedContext_;
  ID3D11Resource *resource_;
  D3D11_MAPPED_SUBRESOURCE mapped_{};
  bool active_ = false;
};
}

std::vector<std::uint8_t>
TextureLab::ReadBuffer(REX::W32::ID3D11Buffer *a_buffer,
                       std::uint32_t a_bytes) {
  ReadbackMeter meter{"buffer"};
  const RendererLock rendererLock{&meter};
  std::vector<std::uint8_t> out;
  if (!a_buffer || a_bytes == 0 || !Init()) {
    return out;
  }
  D3D11_BUFFER_DESC desc{};
  a_buffer->GetDesc(&desc);
  if (a_bytes > desc.byteWidth) {
    return out;
  }
  desc = {};
  desc.byteWidth = a_bytes;
  desc.usage = D3D11_USAGE_STAGING;
  desc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<ID3D11Buffer> staging;
  if (Failed(borrowedDevice_->CreateBuffer(&desc, nullptr,
                                           staging.GetAddressOf()))) {
    return out;
  }
  const D3D11_BOX box{0, 0, 0, a_bytes, 1, 1};
  const UnconditionalReadback unconditional{borrowedContext_};
  borrowedContext_->CopySubresourceRegion(
      reinterpret_cast<REX::W32::ID3D11Resource *>(staging.Get()), 0, 0, 0, 0,
      reinterpret_cast<REX::W32::ID3D11Resource *>(a_buffer), 0, &box);
  const ReadMapping mapped{borrowedContext_,
                           reinterpret_cast<ID3D11Resource *>(staging.Get()),
                           meter};
  if (const auto *data = mapped.Data()) {
    out.assign(data, data + a_bytes);
    meter.Succeeded(out.size());
  }
  return out;
}

bool TextureLab::EnsureReductionStaging(ReductionReadback &a_readback) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.width = 1;
  desc.height = 1;
  desc.mipLevels = 1;
  desc.arraySize = 1;
  desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.sampleDesc.count = 1;
  desc.usage = D3D11_USAGE_STAGING;
  desc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
  for (auto &staging : a_readback.staging)
    if (!staging.Get() && Failed(borrowedDevice_->CreateTexture2D(
                              &desc, nullptr, staging.GetAddressOf())))
      return false;
  return true;
}

bool TextureLab::SubmitReduction(RenderTarget &a_field, ReductionKind a_kind,
                                 ValueType a_type,
                                 ReductionReadback &a_readback) {
  if (!EnsureReductionStaging(a_readback))
    return false;
  const std::size_t slot = ReserveReadback(a_readback.ring);
  if (DrawReduction(a_field, a_kind, a_type, *a_readback.staging[slot].Get()))
    return true;
  DropReadback(a_readback.ring, slot);
  return false;
}

std::optional<ReductionResult>
TextureLab::CollectReduction(ReductionKind a_kind, ValueType a_type,
                             ReductionExtent a_extent,
                             ReductionReadback &a_readback) {
  const auto pending = PendingNewestFirst(a_readback.ring);
  if (pending.empty())
    return std::nullopt;
  ReadbackMeter meter{"reduction"};
  const RendererLock rendererLock{&meter};
  if (!available_ || !borrowedContext_)
    return ReductionResult{
        std::unexpected("reduction readback is unavailable")};
  for (const std::size_t slot : pending) {
    if (!a_readback.staging[slot].Get()) {
      DropReadback(a_readback.ring, slot);
      continue;
    }
    const ReadMapping mapped{
        borrowedContext_,
        reinterpret_cast<ID3D11Resource *>(a_readback.staging[slot].Get()),
        meter, static_cast<std::uint32_t>(D3D11_MAP_FLAG_DO_NOT_WAIT)};
    const auto *data = mapped.Data();
    if (!data)
      continue;
    ReducedTexel texel{};
    std::memcpy(texel.data(), data, sizeof(texel));
    meter.Succeeded(sizeof(texel));
    if (!AcceptReadback(a_readback.ring, slot))
      return std::nullopt;
    return DecodeReduction(a_kind, a_type, texel, a_extent);
  }
  return std::nullopt;
}

std::optional<float> TextureLab::ReadBackMean(RenderTarget &a_target) {
  ReadbackMeter meter{"mean"};
  const RendererLock rendererLock{&meter};
  std::optional<float> result;
  if (!a_target.texture.Get() || !available_) {
    return result;
  }
  D3D11_TEXTURE2D_DESC desc{};
  a_target.texture->GetDesc(&desc);
  if (desc.mipLevels == 0 || desc.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
    return result;
  }
  const auto lastMip = desc.mipLevels - 1;
  D3D11_TEXTURE2D_DESC stagingDesc{};
  stagingDesc.width = 1;
  stagingDesc.height = 1;
  stagingDesc.mipLevels = 1;
  stagingDesc.arraySize = 1;
  stagingDesc.format = desc.format;
  stagingDesc.sampleDesc.count = 1;
  stagingDesc.usage = D3D11_USAGE_STAGING;
  stagingDesc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
  ComPtr<REX::W32::ID3D11Texture2D> staging;
  if (!Failed(borrowedDevice_->CreateTexture2D(&stagingDesc, nullptr,
                                               staging.GetAddressOf()))) {
    const UnconditionalReadback unconditional{borrowedContext_};
    borrowedContext_->CopySubresourceRegion(
        staging.Get(), 0, 0, 0, 0, a_target.texture.Get(), lastMip, nullptr);
    const ReadMapping mapped{borrowedContext_, staging.Get(), meter};
    if (const auto *px = mapped.Data(); px && mapped.RowPitch() >= 4) {
      result = (0.299f * px[0] + 0.587f * px[1] + 0.114f * px[2]) / 255.0f;
      meter.Succeeded(4);
    } else {
      logger::warn("TextureLab: staging map failed; mean readback unavailable");
    }
  } else {
    logger::warn("TextureLab: staging texture creation failed; mean readback "
                 "unavailable");
  }
  return result;
}

bool TextureLab::EnsureSampleStaging(MaterialReadback &a_readback) {
  D3D11_TEXTURE2D_DESC desc{};
  desc.width = kSampleSide;
  desc.height = kSampleSide;
  desc.mipLevels = 1;
  desc.arraySize = 1;
  desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.sampleDesc.count = 1;
  desc.usage = D3D11_USAGE_STAGING;
  desc.cpuAccessFlags = D3D11_CPU_ACCESS_READ;
  for (auto *staging : {&a_readback.rmaos, &a_readback.diffuse})
    if (!staging->Get() && Failed(borrowedDevice_->CreateTexture2D(
                               &desc, nullptr, staging->GetAddressOf())))
      return false;
  return true;
}

bool TextureLab::CopyToStaging(RenderTarget &a_target,
                               REX::W32::ID3D11Texture2D &a_staging) {
  const RendererLock rendererLock;
  if (!available_ || !borrowedContext_ || !a_target.texture.Get())
    return false;
  const UnconditionalReadback unconditional{borrowedContext_};
  borrowedContext_->CopySubresourceRegion(&a_staging, 0, 0, 0, 0,
                                          a_target.texture.Get(), 0, nullptr);
  return true;
}

std::optional<TextureLab::MaterialSampleResult>
TextureLab::ReadMaterialSample(MaterialReadback &a_readback, bool a_wait) {
  if (!a_readback.pending)
    return std::nullopt;
  ReadbackMeter meter{"pixels"};
  const RendererLock rendererLock{&meter};
  if (!available_ || !borrowedContext_ || !a_readback.rmaos.Get() ||
      !a_readback.diffuse.Get()) {
    a_readback.pending = false;
    return MaterialSampleResult{
        std::unexpected("material sample readback is unavailable")};
  }
  const std::uint32_t flags =
      a_wait ? 0u : static_cast<std::uint32_t>(D3D11_MAP_FLAG_DO_NOT_WAIT);
  const ReadMapping rmaos{
      borrowedContext_,
      reinterpret_cast<ID3D11Resource *>(a_readback.rmaos.Get()), meter, flags};
  const ReadMapping diffuse{
      borrowedContext_,
      reinterpret_cast<ID3D11Resource *>(a_readback.diffuse.Get()), meter,
      flags};
  const std::size_t rowBytes = static_cast<std::size_t>(kSampleSide) * 4;
  if (!rmaos.Data() || !diffuse.Data() || rmaos.RowPitch() < rowBytes ||
      diffuse.RowPitch() < rowBytes) {
    if (!a_wait)
      return std::nullopt;
    a_readback.pending = false;
    return MaterialSampleResult{
        std::unexpected("the material maps could not be read back")};
  }
  MaterialSample sample;
  sample.width = kSampleSide;
  sample.height = kSampleSide;
  sample.texels.reserve(static_cast<std::size_t>(kSampleSide) * kSampleSide);
  constexpr float scale = 1.0f / 255.0f;
  for (std::uint32_t y = 0; y < kSampleSide; ++y) {
    const std::uint8_t *mrow = rmaos.Data() + y * rmaos.RowPitch();
    const std::uint8_t *drow = diffuse.Data() + y * diffuse.RowPitch();
    for (std::uint32_t x = 0; x < kSampleSide; ++x) {
      const std::uint8_t *m = mrow + x * 4;
      const std::uint8_t *d = drow + x * 4;
      MaterialTexel texel;
      texel.roughness = m[0] * scale;
      texel.metallic = m[1] * scale;
      texel.occlusion = m[2] * scale;
      texel.reflectance = m[3] * scale;
      texel.luma = (0.2126f * d[0] + 0.7152f * d[1] + 0.0722f * d[2]) * scale;
      texel.diffuse = Vec3{d[0] * scale, d[1] * scale, d[2] * scale};
      sample.texels.push_back(texel);
    }
  }
  meter.Succeeded(rowBytes * kSampleSide * 2);
  a_readback.pending = false;
  return MaterialSampleResult{std::move(sample)};
}

std::shared_ptr<TextureLab::Lookup>
TextureLab::CreateLookup(std::span<const float, 256> a_values) {
  if (!Init()) {
    return nullptr;
  }
  auto lookup = std::make_shared<Lookup>(Lookup::ConstructionKey{});
  D3D11_TEXTURE2D_DESC desc{};
  desc.width = 256;
  desc.height = 1;
  desc.mipLevels = 1;
  desc.arraySize = 1;
  desc.format = DXGI_FORMAT_R32_FLOAT;
  desc.sampleDesc.count = 1;
  desc.usage = D3D11_USAGE_IMMUTABLE;
  desc.bindFlags = D3D11_BIND_SHADER_RESOURCE;
  D3D11_SUBRESOURCE_DATA data{a_values.data(), 256 * sizeof(float), 0};
  if (Failed(borrowedDevice_->CreateTexture2D(
          &desc, &data, lookup->texture.GetAddressOf())) ||
      Failed(borrowedDevice_->CreateShaderResourceView(
          lookup->texture.Get(), nullptr, lookup->srv.GetAddressOf()))) {
    logger::error("TextureLab: could not create a curve lookup");
    return nullptr;
  }
  return lookup;
}

std::optional<TextureLab::Extent>
TextureLab::ExtentOf(RE::NiSourceTexture *a_source) {
  const RendererLock rendererLock;
  const auto *data = DataOf(a_source);
  if (!data || !data->resourceView) {
    return std::nullopt;
  }
  auto *srv = reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
      data->resourceView);
  ComPtr<ID3D11Resource> resource;
  srv->GetResource(resource.GetAddressOf());
  if (!resource.Get()) {
    return std::nullopt;
  }
  ComPtr<REX::W32::ID3D11Texture2D> texture;
  resource->QueryInterface(IID_ID3D11Texture2D,
                           reinterpret_cast<void **>(texture.GetAddressOf()));
  std::optional<Extent> extent;
  if (texture.Get()) {
    D3D11_TEXTURE2D_DESC desc{};
    texture->GetDesc(&desc);
    extent = Extent{desc.width, desc.height};
  }
  return extent;
}

float TextureLab::MeanLuminance(RE::NiSourceTexture *a_source) {
  if (const auto it = luminance_.find(a_source); it != luminance_.end()) {
    return it->second;
  }
  float result = 0.5f;
  auto target = Acquire(TextureSize(64), "mean");
  if (target && Render(*target, a_source, LayerParams{})) {
    result = ReadBackMean(*target).value_or(0.5f);
  } else {
    logger::warn("TextureLab: mean luminance render failed; using 0.5");
  }
  luminance_[a_source] = result;
  return result;
}

float TextureLab::MeanChannel(RE::NiSourceTexture *a_source,
                              ShaderChannel a_channel) {
  const auto key = std::make_pair(a_source, a_channel);
  if (const auto it = channelMeans_.find(key); it != channelMeans_.end()) {
    return it->second;
  }
  float result = 0.5f;
  auto target = Acquire(TextureSize(64), "mean");
  if (target) {
    LayerParams p;
    p.mode = Mode::kChannel;
    p.map = {a_source, MapReading::kRmaos};
    p.channel.channel = a_channel;
    if (Render(*target, nullptr, p)) {
      result = ReadBackMean(*target).value_or(0.5f);
    } else {
      logger::warn("TextureLab: channel mean render failed; using 0.5");
    }
  } else {
    logger::warn("TextureLab: no target for channel mean; using 0.5");
  }
  channelMeans_[key] = result;
  return result;
}

std::optional<float> TextureLab::MaxDifference(RenderTarget &a_first,
                                               RenderTarget &a_second,
                                               ShaderChannel a_channel) {
  const TextureSize size{a_first.size};
  const auto field = Acquire(size, "fusion difference",
                             TextureFormat::kRgba32Float, MipPolicy::kNone);
  if (!field)
    return std::nullopt;
  ProgramBindings bindings;
  bindings.inputCount = 2;
  bindings.textureCount = 2;
  LayerInput sampling;
  sampling.channel = a_channel;
  sampling.meshSpace = true;
  bindings.textures[0] = {a_first.Texture(), sampling, 1.0f};
  bindings.textures[1] = {a_second.Texture(), sampling, 1.0f};
  const auto difference = FieldProgram::AbsoluteDifference();
  if (!DrawProgramPass(*field, difference.Instructions(), difference.Inputs(),
                       difference.ResultType(), bindings))
    return std::nullopt;
  ReductionReadback readback;
  if (!EnsureReductionStaging(readback) ||
      !DrawReduction(*field, ReductionKind::kMaximum, ValueType::kVec3,
                     *readback.staging[0].Get()))
    return std::nullopt;
  ReadbackMeter meter{"fusion check"};
  const RendererLock rendererLock{&meter};
  const ReadMapping mapped{
      borrowedContext_,
      reinterpret_cast<ID3D11Resource *>(readback.staging[0].Get()), meter};
  const auto *data = mapped.Data();
  if (!data)
    return std::nullopt;
  ReducedTexel texel{};
  std::memcpy(texel.data(), data, sizeof(texel));
  const auto decoded =
      DecodeReduction(ReductionKind::kMaximum, ValueType::kVec3, texel,
                      {size.Pixels(), size.Pixels()});
  if (!decoded)
    return std::nullopt;
  const auto v = AsVec3(*decoded);
  return std::max({v.x, v.y, v.z});
}

namespace {
bool Ready(HRESULT a_result) { return a_result == 0; }
}

TextureLab::TimedSpan::TimedSpan(TextureLab &a_lab, std::string a_key)
    : lab_(&a_lab) {
  span_ = GpuTiming::OpenSpan(lab_->timingRing_, lab_->timingTotals_,
                              std::move(a_key));
  if (span_)
    lab_->Stamp(*span_, false);
}

TextureLab::TimedSpan::~TimedSpan() {
  if (span_ && lab_->timingRing_.recording)
    lab_->Stamp(*span_, true);
}

bool TextureLab::Timing() const noexcept {
  return timingRing_.recording.has_value();
}

ID3D11Query *TextureLab::TimestampQuery(std::size_t a_slot,
                                        std::size_t a_index) {
  if (a_slot >= timingQueries_.size() || !borrowedDevice_)
    return nullptr;
  auto &stamps = timingQueries_[a_slot].stamps;
  if (stamps.size() <= a_index)
    stamps.resize(a_index + 1);
  if (!stamps[a_index].Get()) {
    const D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP, 0};
    if (Failed(borrowedDevice_->CreateQuery(&desc,
                                            stamps[a_index].GetAddressOf())))
      return nullptr;
  }
  return stamps[a_index].Get();
}

void TextureLab::Stamp(std::size_t a_span, bool a_end) {
  if (!timingRing_.recording || !borrowedContext_)
    return;
  auto *query = TimestampQuery(*timingRing_.recording, a_span * 2 + a_end);
  if (!query)
    return;
  const RendererLock rendererLock;
  borrowedContext_->End(query);
}

void TextureLab::BeginTimedTick(bool a_enabled) {
  if (!a_enabled || !available_ || !borrowedDevice_ || !borrowedContext_)
    return;
  const auto slot = GpuTiming::BeginTick(timingRing_, timingTotals_);
  if (!slot)
    return;
  auto &disjoint = timingQueries_[*slot].disjoint;
  const D3D11_QUERY_DESC desc{D3D11_QUERY_TIMESTAMP_DISJOINT, 0};
  if (!disjoint.Get() &&
      Failed(borrowedDevice_->CreateQuery(&desc, disjoint.GetAddressOf()))) {
    GpuTiming::EndTick(timingRing_);
    GpuTiming::Resolve(timingRing_, timingTotals_, *slot, {true, 0, {}});
    return;
  }
  {
    const RendererLock rendererLock;
    borrowedContext_->Begin(disjoint.Get());
  }
  tickSpan_ = GpuTiming::OpenSpan(timingRing_, timingTotals_, "RenderTick");
  if (tickSpan_)
    Stamp(*tickSpan_, false);
}

void TextureLab::EndTimedTick() {
  if (!timingRing_.recording || !borrowedContext_)
    return;
  if (tickSpan_)
    Stamp(*tickSpan_, true);
  tickSpan_.reset();
  if (auto *disjoint = timingQueries_[*timingRing_.recording].disjoint.Get()) {
    const RendererLock rendererLock;
    borrowedContext_->End(disjoint);
  }
  GpuTiming::EndTick(timingRing_);
}

void TextureLab::CollectTimings() {
  if (!borrowedContext_)
    return;
  const RendererLock rendererLock;
  for (const std::size_t slot : GpuTiming::PendingOldestFirst(timingRing_)) {
    auto &queries = timingQueries_[slot];
    D3D11_QUERY_DATA_TIMESTAMP_DISJOINT data{};
    if (!queries.disjoint.Get() ||
        !Ready(borrowedContext_->GetData(queries.disjoint.Get(), &data,
                                         sizeof(data),
                                         D3D11_ASYNC_GETDATA_DONOTFLUSH)))
      break;
    GpuTiming::ResolvedTick tick{data.disjoint != 0, data.frequency, {}};
    const std::size_t spans = timingRing_.slots[slot].spans.size();
    bool complete = true;
    for (std::size_t i = 0; i < spans && complete; ++i) {
      GpuTiming::Stamps stamps{};
      for (const bool end : {false, true}) {
        auto *query = i * 2 + end < queries.stamps.size()
                          ? queries.stamps[i * 2 + end].Get()
                          : nullptr;
        std::uint64_t value = 0;
        complete =
            complete && query &&
            Ready(borrowedContext_->GetData(query, &value, sizeof(value),
                                            D3D11_ASYNC_GETDATA_DONOTFLUSH));
        (end ? stamps.end : stamps.begin) = value;
      }
      tick.spans.push_back(stamps);
    }
    if (!complete)
      tick = {true, 0, {}};
    GpuTiming::Resolve(timingRing_, timingTotals_, slot, tick);
  }
}

GpuTiming::Totals TextureLab::DrainTimings() {
  return GpuTiming::Drain(timingTotals_);
}
}
