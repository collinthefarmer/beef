// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/TextureLab.h"

#include "render/D3DResult.h"
#include "render/ShaderConstants.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <ranges>
#include <string_view>

namespace BetterEnchantmentEffects {
using namespace REX::W32;

namespace {
template <class T> void Release(T *&a_ptr) {
  if (a_ptr) {
    a_ptr->Release();
    a_ptr = nullptr;
  }
}

class RendererLock {
public:
  explicit RendererLock(RE::BSGraphics::Renderer &a_renderer)
      : renderer_(a_renderer) {
    renderer_.Lock();
  }
  ~RendererLock() { renderer_.Unlock(); }
  RendererLock(const RendererLock &) = delete;
  RendererLock &operator=(const RendererLock &) = delete;
  RendererLock(RendererLock &&) = delete;
  RendererLock &operator=(RendererLock &&) = delete;

private:
  RE::BSGraphics::Renderer &renderer_;
};

REX::W32::ID3D11ShaderResourceView *ViewOf(RE::NiSourceTexture *a_texture) {
  auto *data = DataOf(a_texture);
  return data && data->resourceView
             ? reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
                   data->resourceView)
             : nullptr;
}

constexpr std::size_t kStackBaseSlot = 12;
static_assert(kStackBaseSlot == TextureLab::kPassSrvs);
constexpr std::size_t kStackSourceSlot = kStackBaseSlot + 1;
constexpr std::size_t kStackMaskSlot = kStackSourceSlot + kMaxStackLayers;
constexpr std::size_t kStackSrvs = kStackMaskSlot + kMaxStackLayers;

void BindSlot(std::span<REX::W32::ID3D11ShaderResourceView *> srvs,
              std::size_t slot, REX::W32::ID3D11ShaderResourceView *view) {
  if (slot < srvs.size())
    srvs[slot] = view;
}

template <class Constants>
void UploadConstants(ID3D11DeviceContext &context, ID3D11Buffer *buffer,
                     const Constants &constants) {
  if (buffer)
    context.UpdateSubresource(buffer, 0, nullptr, &constants, 0, 0);
}

std::unique_ptr<ProgramConstants> EmptyProgramConstants() {
  auto constants = std::make_unique<ProgramConstants>();
  std::memset(constants.get(), 0, sizeof(ProgramConstants));
  return constants;
}

bool ProgramFits(const ProgramCode &code,
                 const TextureLab::ProgramBindings &bindings) {
  return code.instructions.size() <= kProgramInstructions &&
         code.inputs.size() <= kProgramInputs &&
         bindings.textureCount <= kProgramTextures &&
         bindings.lookupCount <= kProgramLookups;
}

ProgramCode PackedCode(const ProgramPack &pack) {
  return {pack.code, pack.inputs, ValueType::kVec3};
}

void FillInstructionRow(ProgramConstants &constants, std::size_t row,
                        const ProgramInstruction &instruction) {
  if (row >= std::size(constants.code))
    return;
  constants.code[row][0] = static_cast<float>(instruction.opcode);
  constants.code[row][1] = instruction.number;
  constants.code[row][2] = static_cast<float>(instruction.index);
  constants.code[row][3] =
      static_cast<float>(std::min<std::uint32_t>(instruction.components, 3) +
                         4 * OpcodePops(instruction.opcode));
}

std::size_t
FillProgramInstructions(ProgramConstants &constants,
                        std::span<const ProgramInstruction> instructions) {
  const std::size_t count =
      std::min<std::size_t>(instructions.size(), std::size(constants.code));
  for (std::size_t row = 0; row < count; ++row)
    FillInstructionRow(constants, row, instructions[row]);
  return count;
}

void FillProgramInputs(ProgramConstants &constants,
                       std::span<const ProgramInput> inputs,
                       const TextureLab::ProgramBindings &bindings) {
  const std::size_t count =
      std::min({inputs.size(), kProgramInputs, std::size(constants.inputs),
                std::size(constants.inputValues)});
  for (std::size_t row = 0; row < count; ++row) {
    const auto *texture = Get<ProgramTextureInput>(inputs[row]);
    constants.inputs[row][0] = texture ? 1.0f : 0.0f;
    constants.inputs[row][1] =
        texture ? static_cast<float>(texture->slot) : 0.0f;
    constants.inputValues[row][0] = bindings.values[row].x;
    constants.inputValues[row][1] = bindings.values[row].y;
    constants.inputValues[row][2] = bindings.values[row].z;
  }
}

void FillProgramTextureRow(ProgramConstants &constants, std::size_t row,
                           const TextureLab::ProgramTexture &texture) {
  if (row >= std::size(constants.texParams))
    return;
  const TextureLab::Scroll &placement = texture.sampling.transform;
  constants.texParams[row][0] =
      static_cast<float>(std::to_underlying(texture.sampling.channel));
  constants.texParams[row][1] = texture.sampling.meshSpace ? 1.0f : 0.0f;
  constants.texParams[row][2] = texture.normalize;
  constants.texParams[row][3] = placement.sourceMip;
  constants.texTransform[row][0] = placement.uOffset;
  constants.texTransform[row][1] = placement.vOffset;
  constants.texTransform[row][2] = placement.tileU;
  constants.texTransform[row][3] = placement.tileV;
  constants.texFlags[row][0] = placement.mirrorU ? 1.0f : 0.0f;
  constants.texFlags[row][1] = placement.mirrorV ? 1.0f : 0.0f;
  constants.texFlags[row][2] = placement.transpose ? 1.0f : 0.0f;
  constants.texFlags[row][3] = texture.sampling.nearest ? 1.0f : 0.0f;
}

std::size_t BoundTextureCount(const TextureLab::ProgramBindings &bindings) {
  return std::min<std::size_t>(bindings.textureCount, kProgramTextures);
}

void FillProgramTextures(ProgramConstants &constants,
                         const TextureLab::ProgramBindings &bindings) {
  for (std::size_t row = 0; row < BoundTextureCount(bindings); ++row)
    FillProgramTextureRow(constants, row, bindings.textures[row]);
}

void BindProgramTextures(std::span<REX::W32::ID3D11ShaderResourceView *> srvs,
                         const TextureLab::ProgramBindings &bindings) {
  for (std::size_t slot = 0; slot < BoundTextureCount(bindings); ++slot)
    BindSlot(srvs, slot, ViewOf(bindings.textures[slot].texture));
}

void FillProgramConstants(ProgramConstants &constants,
                          std::span<REX::W32::ID3D11ShaderResourceView *> srvs,
                          const ProgramCode &code,
                          const TextureLab::ProgramBindings &bindings) {
  const std::size_t instructions =
      FillProgramInstructions(constants, code.instructions);
  FillProgramInputs(constants, code.inputs, bindings);
  FillProgramTextures(constants, bindings);
  BindProgramTextures(srvs, bindings);
  constants.misc[1] = static_cast<float>(instructions);
}

bool FieldsBound(const TextureLab::BoundLayerFields &a_fields) {
  const auto &pack = a_fields.pack;
  const auto &bindings = a_fields.bindings;
  return pack.inputs.size() == bindings.inputCount &&
         pack.textureCount == bindings.textureCount &&
         pack.lookupCount == bindings.lookupCount &&
         pack.code.size() <= kProgramInstructions &&
         std::ranges::all_of(
             std::views::iota(std::uint32_t{0},
                              static_cast<std::uint32_t>(pack.segments.size())),
             [&](std::uint32_t segment) {
               return SegmentAt(pack.segments, pack.code.size(), segment)
                   .has_value();
             });
}

StackShape StackShapeOf(bool a_base,
                        std::span<const TextureLab::LayerPass> a_layers,
                        const TextureLab::BoundLayerFields &a_fields) {
  StackShape shape;
  shape.base = a_base;
  shape.code = CodeWithoutNumbers(a_fields.pack.code);
  shape.slots = InputTextureSlots(a_fields.pack.inputs);
  shape.segments = a_fields.pack.segments;
  for (const auto &layer : a_layers) {
    LayerShape out;
    if (layer.sourceSegment)
      out.source = SegmentRead{*layer.sourceSegment};
    else if (ViewOf(layer.source))
      out.source = SourceTexture{layer.input.meshSpace};
    if (layer.maskSegment)
      out.mask = SegmentRead{*layer.maskSegment};
    else if (ViewOf(layer.mask))
      out.mask = MaskTexture{};
    out.channel = std::to_underlying(layer.input.channel);
    out.blend = layer.blend;
    out.channels = layer.channels;
    out.maskChannel = std::to_underlying(layer.maskChannel);
    shape.layers.push_back(out);
  }
  return shape;
}

void FillStackCounts(StackConstants &constants, const StackShape &shape) {
  constants.misc[0] = static_cast<float>(shape.layers.size());
  constants.misc[1] = shape.base ? 1.0f : 0.0f;
}

void FillLayerConstants(StackConstants &constants, std::size_t index,
                        const TextureLab::LayerPass &layer,
                        const LayerShape &shape) {
  if (index >= std::size(constants.layer))
    return;
  const TextureLab::Scroll &placement = layer.input.transform;
  constants.offsetScale[index][0] = placement.uOffset;
  constants.offsetScale[index][1] = placement.vOffset;
  constants.offsetScale[index][2] = placement.tileU;
  constants.offsetScale[index][3] = placement.tileV;
  constants.flags[index][0] = placement.mirrorU ? 1.0f : 0.0f;
  constants.flags[index][1] = placement.mirrorV ? 1.0f : 0.0f;
  constants.flags[index][2] = placement.transpose ? 1.0f : 0.0f;
  constants.flags[index][3] = placement.sourceMip;
  constants.layer[index][0] = static_cast<float>(shape.channel);
  constants.layer[index][1] = layer.input.meshSpace ? 1.0f : 0.0f;
  constants.layer[index][2] = static_cast<float>(shape.blend);
  constants.layer[index][3] = layer.opacity;
  constants.color[index][0] = layer.color[0];
  constants.color[index][1] = layer.color[1];
  constants.color[index][2] = layer.color[2];
  constants.color[index][3] = layer.normalize;
  constants.mask[index][0] =
      HasMask(shape) ? static_cast<float>(shape.maskChannel) : -1.0f;
  constants.mask[index][1] = static_cast<float>(shape.channels);
  constants.mask[index][3] = HasSource(shape) ? 1.0f : 0.0f;
}

void FillLayerSegments(StackConstants &constants, const StackShape &shape) {
  const std::size_t count =
      std::min(shape.layers.size(), std::size(constants.field));
  for (std::size_t index = 0; index < count; ++index) {
    const LayerShape &layer = shape.layers[index];
    const ProgramSegment source = SourceSegment(shape, layer);
    const ProgramSegment mask = MaskSegment(shape, layer);
    constants.field[index][0] = static_cast<float>(source.first);
    constants.field[index][1] = static_cast<float>(source.count);
    constants.field[index][2] = static_cast<float>(mask.first);
    constants.field[index][3] = static_cast<float>(mask.count);
  }
}

void BindLayerTextures(std::span<REX::W32::ID3D11ShaderResourceView *> srvs,
                       RE::NiSourceTexture *base,
                       std::span<const TextureLab::LayerPass> layers) {
  BindSlot(srvs, kStackBaseSlot, ViewOf(base));
  const std::size_t count = std::min(layers.size(), kMaxStackLayers);
  for (std::size_t index = 0; index < count; ++index) {
    BindSlot(srvs, kStackSourceSlot + index, ViewOf(layers[index].source));
    BindSlot(srvs, kStackMaskSlot + index, ViewOf(layers[index].mask));
  }
}

float MipThatFits(const TextureLab::Extent &a_extent,
                  std::uint32_t a_side) noexcept {
  std::uint32_t side = (std::max)(a_extent.width, a_extent.height);
  std::uint32_t mip = 0;
  while (mip < 16 && (side >> (mip + 1)) >= a_side) {
    ++mip;
  }
  return static_cast<float>(mip);
}

template <class Shader> struct SavedStage {
  using GetShader = void (ID3D11DeviceContext::*)(Shader **,
                                                  ID3D11ClassInstance **,
                                                  std::uint32_t *);
  using SetShader = void (ID3D11DeviceContext::*)(Shader *,
                                                  ID3D11ClassInstance *const *,
                                                  std::uint32_t);
  using GetResources = void (ID3D11DeviceContext::*)(
      std::uint32_t, std::uint32_t, REX::W32::ID3D11ShaderResourceView **);
  using SetResources = void (ID3D11DeviceContext::*)(
      std::uint32_t, std::uint32_t,
      REX::W32::ID3D11ShaderResourceView *const *);

  SavedStage() = default;
  ~SavedStage() {
    Release(shader);
    for (auto &instance : instances) {
      Release(instance);
    }
    for (auto &resource : resources) {
      Release(resource);
    }
  }
  SavedStage(const SavedStage &) = delete;
  SavedStage &operator=(const SavedStage &) = delete;
  SavedStage(SavedStage &&) = delete;
  SavedStage &operator=(SavedStage &&) = delete;

  void Capture(ID3D11DeviceContext *a_context, GetShader a_shader,
               GetResources a_resources) {
    (a_context->*a_shader)(&shader, instances, &instanceCount);
    (a_context->*a_resources)(0, std::size(resources), resources);
  }

  void Restore(ID3D11DeviceContext *a_context, SetShader a_shader,
               SetResources a_resources) {
    (a_context->*a_shader)(shader, instances, instanceCount);
    (a_context->*a_resources)(0, std::size(resources), resources);
  }

  Shader *shader = nullptr;
  ID3D11ClassInstance *instances[D3D11_SHADER_MAX_INTERFACES]{};
  std::uint32_t instanceCount = std::size(instances);
  REX::W32::ID3D11ShaderResourceView
      *resources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
};
}

struct TextureLab::RenderPass {
  RenderPass(RE::BSGraphics::Renderer &a_renderer,
             ID3D11DeviceContext &a_context)
      : rendererLock_(a_renderer), context(&a_context) {
    Capture(context);
    context->GSSetShader(nullptr, nullptr, 0);
    context->HSSetShader(nullptr, nullptr, 0);
    context->DSSetShader(nullptr, nullptr, 0);
    context->SetPredication(nullptr, false);
  }
  ~RenderPass() { Restore(context); }
  RenderPass(const RenderPass &) = delete;
  RenderPass &operator=(const RenderPass &) = delete;
  RenderPass(RenderPass &&) = delete;
  RenderPass &operator=(RenderPass &&) = delete;

  [[nodiscard]] ID3D11DeviceContext &Context() const { return *context; }

private:
  RendererLock rendererLock_;
  ID3D11DeviceContext *context;
  REX::W32::ID3D11RenderTargetView
      *rtvs[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT]{};
  std::uint32_t renderTargetCount = std::size(rtvs);
  REX::W32::ID3D11UnorderedAccessView *firstUav = nullptr;
  ID3D11Predicate *predicate = nullptr;
  REX::W32::BOOL predicateValue = false;
  REX::W32::ID3D11DepthStencilView *dsv = nullptr;
  D3D11_VIEWPORT
  viewports[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE]{};
  std::uint32_t viewportCount = std::size(viewports);
  SavedStage<ID3D11VertexShader> vertex;
  SavedStage<ID3D11PixelShader> pixel;
  SavedStage<ID3D11GeometryShader> geometry;
  SavedStage<ID3D11HullShader> hull;
  SavedStage<ID3D11DomainShader> domain;
  REX::W32::ID3D11ShaderResourceView
      *computeResources[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT]{};
  REX::W32::ID3D11SamplerState *sampler = nullptr;
  REX::W32::ID3D11Buffer *cbs[4]{};
  REX::W32::ID3D11InputLayout *layout = nullptr;
  REX::W32::ID3D11Buffer *vertexBuffer = nullptr;
  std::uint32_t vertexStride = 0;
  std::uint32_t vertexOffset = 0;
  REX::W32::ID3D11Buffer *indexBuffer = nullptr;
  DXGI_FORMAT indexFormat{};
  std::uint32_t indexOffset = 0;
  D3D11_PRIMITIVE_TOPOLOGY topology{};
  REX::W32::ID3D11BlendState *blend = nullptr;
  float blendFactor[4]{};
  std::uint32_t sampleMask = 0;
  REX::W32::ID3D11DepthStencilState *depth = nullptr;
  std::uint32_t stencilRef = 0;
  REX::W32::ID3D11RasterizerState *raster = nullptr;

  void CaptureOutputs(ID3D11DeviceContext *a_ctx) {
    a_ctx->OMGetRenderTargets(std::size(rtvs), rtvs, &dsv);
    while (renderTargetCount && !rtvs[renderTargetCount - 1]) {
      --renderTargetCount;
    }
    if (renderTargetCount == 0) {
      a_ctx->OMGetRenderTargetsAndUnorderedAccessViews(0, nullptr, nullptr, 0,
                                                       1, &firstUav);
    }
  }

  void RestoreOutputs(ID3D11DeviceContext *a_ctx) {
    a_ctx->OMSetRenderTargets(renderTargetCount, rtvs, dsv);
    if (firstUav) {
      const std::uint32_t keepCounter =
          std::numeric_limits<std::uint32_t>::max();
      a_ctx->OMSetRenderTargetsAndUnorderedAccessViews(
          D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL, nullptr, nullptr, 0, 1,
          &firstUav, &keepCounter);
    }
    for (auto &rtv : rtvs) {
      Release(rtv);
    }
    Release(firstUav);
    Release(dsv);
  }

  void CaptureStages(ID3D11DeviceContext *a_ctx) {
    vertex.Capture(a_ctx, &ID3D11DeviceContext::VSGetShader,
                   &ID3D11DeviceContext::VSGetShaderResources);
    pixel.Capture(a_ctx, &ID3D11DeviceContext::PSGetShader,
                  &ID3D11DeviceContext::PSGetShaderResources);
    geometry.Capture(a_ctx, &ID3D11DeviceContext::GSGetShader,
                     &ID3D11DeviceContext::GSGetShaderResources);
    hull.Capture(a_ctx, &ID3D11DeviceContext::HSGetShader,
                 &ID3D11DeviceContext::HSGetShaderResources);
    domain.Capture(a_ctx, &ID3D11DeviceContext::DSGetShader,
                   &ID3D11DeviceContext::DSGetShaderResources);
    a_ctx->CSGetShaderResources(0, std::size(computeResources),
                                computeResources);
  }

  void RestoreStages(ID3D11DeviceContext *a_ctx) {
    vertex.Restore(a_ctx, &ID3D11DeviceContext::VSSetShader,
                   &ID3D11DeviceContext::VSSetShaderResources);
    pixel.Restore(a_ctx, &ID3D11DeviceContext::PSSetShader,
                  &ID3D11DeviceContext::PSSetShaderResources);
    geometry.Restore(a_ctx, &ID3D11DeviceContext::GSSetShader,
                     &ID3D11DeviceContext::GSSetShaderResources);
    hull.Restore(a_ctx, &ID3D11DeviceContext::HSSetShader,
                 &ID3D11DeviceContext::HSSetShaderResources);
    domain.Restore(a_ctx, &ID3D11DeviceContext::DSSetShader,
                   &ID3D11DeviceContext::DSSetShaderResources);
    a_ctx->CSSetShaderResources(0, std::size(computeResources),
                                computeResources);
    for (auto &resource : computeResources) {
      Release(resource);
    }
  }

  void Capture(REX::W32::ID3D11DeviceContext *a_ctx) {
    CaptureOutputs(a_ctx);
    a_ctx->GetPredication(&predicate, &predicateValue);
    a_ctx->RSGetViewports(&viewportCount, viewports);
    CaptureStages(a_ctx);
    a_ctx->PSGetSamplers(0, 1, &sampler);
    a_ctx->PSGetConstantBuffers(0, 4, cbs);
    a_ctx->IAGetInputLayout(&layout);
    a_ctx->IAGetVertexBuffers(0, 1, &vertexBuffer, &vertexStride,
                              &vertexOffset);
    a_ctx->IAGetIndexBuffer(&indexBuffer, &indexFormat, &indexOffset);
    a_ctx->IAGetPrimitiveTopology(&topology);
    a_ctx->OMGetBlendState(&blend, blendFactor, &sampleMask);
    a_ctx->OMGetDepthStencilState(&depth, &stencilRef);
    a_ctx->RSGetState(&raster);
  }

  void Restore(REX::W32::ID3D11DeviceContext *a_ctx) {
    RestoreOutputs(a_ctx);
    a_ctx->SetPredication(predicate, predicateValue);
    Release(predicate);
    a_ctx->RSSetViewports(viewportCount, viewports);
    RestoreStages(a_ctx);
    a_ctx->PSSetSamplers(0, 1, &sampler);
    a_ctx->PSSetConstantBuffers(0, 4, cbs);
    a_ctx->IASetInputLayout(layout);
    a_ctx->IASetVertexBuffers(0, 1, &vertexBuffer, &vertexStride,
                              &vertexOffset);
    a_ctx->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);
    a_ctx->IASetPrimitiveTopology(topology);
    a_ctx->OMSetBlendState(blend, blendFactor, sampleMask);
    a_ctx->OMSetDepthStencilState(depth, stencilRef);
    a_ctx->RSSetState(raster);
    Release(sampler);
    Release(cbs[0]);
    Release(cbs[1]);
    Release(cbs[2]);
    Release(cbs[3]);
    Release(vertexBuffer);
    Release(indexBuffer);
    Release(layout);
    Release(blend);
    Release(depth);
    Release(raster);
  }
};

void TextureLab::BindTarget(const RenderPass &a_pass, RenderTarget &a_target) {
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv.Get();
  a_pass.Context().OMSetRenderTargets(1, &rtv, nullptr);
  D3D11_VIEWPORT viewport{};
  viewport.width = static_cast<float>(a_target.size);
  viewport.height = static_cast<float>(a_target.size);
  viewport.maxDepth = 1.0f;
  a_pass.Context().RSSetViewports(1, &viewport);
  const float blendFactor[4]{};
  a_pass.Context().OMSetBlendState(gpu_->blend.Get(), blendFactor, 0xFFFFFFFF);
  a_pass.Context().OMSetDepthStencilState(gpu_->depth.Get(), 0);
  a_pass.Context().RSSetState(gpu_->raster.Get());
}

void TextureLab::UnbindTarget(const RenderPass &a_pass,
                              std::uint32_t a_srvCount) {
  REX::W32::ID3D11RenderTargetView *none = nullptr;
  a_pass.Context().OMSetRenderTargets(1, &none, nullptr);
  REX::W32::ID3D11ShaderResourceView *noSrvs[kPassSrvs]{};
  a_pass.Context().PSSetShaderResources(0, (std::min)(a_srvCount, kPassSrvs),
                                        noSrvs);
}

void TextureLab::DrawFullScreen(const RenderPass &a_pass,
                                RenderTarget &a_target,
                                const FullScreenDraw &a_draw) {
  BindTarget(a_pass, a_target);
  a_pass.Context().IASetInputLayout(nullptr);
  a_pass.Context().IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  a_pass.Context().VSSetShader(gpu_->vertex.Get(), nullptr, 0);
  a_pass.Context().PSSetShader(a_draw.shader, nullptr, 0);
  a_pass.Context().PSSetShaderResources(
      0, static_cast<std::uint32_t>(a_draw.srvs.size()), a_draw.srvs.data());
  a_pass.Context().PSSetSamplers(0, 1, gpu_->sampler.GetAddressOf());
  a_pass.Context().PSSetConstantBuffers(
      0, static_cast<std::uint32_t>(a_draw.constants.size()),
      a_draw.constants.data());
  a_pass.Context().Draw(3, 0);
  UnbindTarget(a_pass, static_cast<std::uint32_t>(a_draw.srvs.size()));
  if (a_target.mips == MipPolicy::kGenerate)
    GenerateMips(a_pass, a_target);
}

void TextureLab::GenerateMips(const RenderPass &a_pass,
                              RenderTarget &a_target) {
  std::optional<TimedSpan> mips;
  if (Timing())
    mips.emplace(*this, std::format("GenerateMips {}", a_target.size));
  a_pass.Context().GenerateMips(a_target.srv.Get());
}

void TextureLab::GenerateMipsFor(RenderTarget &a_target) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.srv.Get())
    return;
  const RenderPass pass{*renderer, *borrowedContext_};
  GenerateMips(pass, a_target);
}

namespace {
struct LayerViews {
  REX::W32::ID3D11ShaderResourceView *source = nullptr;
  REX::W32::ID3D11ShaderResourceView *map = nullptr;
  REX::W32::ID3D11ShaderResourceView *previous = nullptr;
  REX::W32::ID3D11ShaderResourceView *curve = nullptr;
};

std::optional<LayerViews>
LayerViewsFor(const TextureLab::LayerParams &a_params,
              RE::NiSourceTexture *a_source,
              REX::W32::ID3D11ShaderResourceView *a_curve) {
  const bool layerPass = a_params.mode == TextureLab::Mode::kLayer;
  LayerViews views;
  views.source = ViewOf(layerPass ? a_params.layer.source : a_source);
  if (!layerPass && !views.source) {
    views.source = ViewOf(a_params.map.texture);
    if (!views.source) {
      return std::nullopt;
    }
  }
  if (layerPass || a_params.map.reading != TextureLab::MapReading::kNone) {
    views.map = ViewOf(layerPass ? a_params.layer.mask : a_params.map.texture);
  }
  views.previous = layerPass ? ViewOf(a_params.layer.previous) : nullptr;
  views.curve = layerPass ? a_curve : nullptr;
  return views;
}

void FillSourceConstants(LayerConstants &a_constants,
                         const TextureLab::LayerParams &a_params,
                         const LayerViews &a_views) {
  const bool layerPass = a_params.mode == TextureLab::Mode::kLayer;
  const TextureLab::Scroll &sc =
      layerPass ? a_params.layer.input.transform : a_params.scroll;
  a_constants.offsetScale[0] = sc.uOffset;
  a_constants.offsetScale[1] = sc.vOffset;
  a_constants.offsetScale[2] = sc.tileU;
  a_constants.offsetScale[3] = sc.tileV;
  a_constants.flags[0] = sc.mirrorU ? 1.0f : 0.0f;
  a_constants.flags[1] = sc.mirrorV ? 1.0f : 0.0f;
  a_constants.flags[2] = sc.transpose ? 1.0f : 0.0f;
  a_constants.flags[3] = static_cast<float>(a_params.mode);
  a_constants.extra[0] = sc.sourceMip;
  a_constants.extra[1] = a_views.map && !layerPass
                             ? static_cast<float>(a_params.map.reading)
                             : 0.0f;
}

void FillLayerPassConstants(LayerConstants &a_constants,
                            const TextureLab::LayerPass &a_layer,
                            const LayerViews &a_views) {
  a_constants.layer[0] =
      static_cast<float>(std::to_underlying(a_layer.input.channel));
  a_constants.layer[1] = a_layer.input.meshSpace ? 1.0f : 0.0f;
  a_constants.layer[2] = static_cast<float>(a_layer.blend);
  a_constants.layer[3] = a_layer.opacity;
  a_constants.layerColor[0] = a_layer.color[0];
  a_constants.layerColor[1] = a_layer.color[1];
  a_constants.layerColor[2] = a_layer.color[2];
  a_constants.layerColor[3] = a_layer.normalize;
  a_constants.layerMask[0] =
      a_views.map ? static_cast<float>(std::to_underlying(a_layer.maskChannel))
                  : -1.0f;
  a_constants.layerMask[1] = static_cast<float>(a_layer.channels);
  a_constants.layerMask[2] = a_views.previous ? 1.0f : 0.0f;
  a_constants.layerMask[3] = a_views.source ? 1.0f : 0.0f;
  a_constants.layerCurve[0] = a_views.curve ? 1.0f : 0.0f;
}

void FillChannelConstants(LayerConstants &a_constants,
                          const TextureLab::ChannelParams &a_channel) {
  a_constants.extra[2] =
      static_cast<float>(std::to_underlying(a_channel.channel));
  a_constants.extra[3] = a_channel.slope ? 1.0f : 0.0f;
}

LayerConstants LayerConstantsFor(const TextureLab::LayerParams &a_params,
                                 const LayerViews &a_views) {
  LayerConstants constants{};
  FillSourceConstants(constants, a_params, a_views);
  if (a_params.mode == TextureLab::Mode::kLayer) {
    FillLayerPassConstants(constants, a_params.layer, a_views);
  }
  if (a_params.mode == TextureLab::Mode::kChannel) {
    FillChannelConstants(constants, a_params.channel);
  }
  return constants;
}
}

bool TextureLab::Render(RenderTarget &a_target, RE::NiSourceTexture *a_source,
                        const LayerParams &a_params) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get()) {
    return false;
  }
  const RenderPass pass{*renderer, *borrowedContext_};
  REX::W32::ID3D11ShaderResourceView *const curve =
      a_params.layer.curve ? a_params.layer.curve->srv.Get() : nullptr;
  const std::optional<LayerViews> views =
      LayerViewsFor(a_params, a_source, curve);
  if (!views) {
    return false;
  }
  const LayerConstants constants = LayerConstantsFor(a_params, *views);
  pass.Context().UpdateSubresource(gpu_->constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  REX::W32::ID3D11ShaderResourceView *srvs[4]{views->source, views->map,
                                              views->previous, views->curve};
  REX::W32::ID3D11Buffer *cbs[1]{gpu_->constants.Get()};
  DrawFullScreen(pass, a_target, {gpu_->pixel.Get(), srvs, cbs});
  return true;
}

bool TextureLab::CanRenderStack(const StackShape &a_shape,
                                std::span<const LayerPass> a_layers,
                                const BoundLayerFields &a_fields) const {
  const bool fields = !a_fields.pack.segments.empty();
  return gpu_ && gpu_->stack.has_value() &&
         (!fields || gpu_->program.has_value()) && FieldsBound(a_fields) &&
         CheckStackShape(a_shape).has_value() &&
         std::ranges::none_of(a_layers, [](const LayerPass &layer) {
           return layer.curve != nullptr;
         });
}

bool TextureLab::RenderStack(RenderTarget &a_target,
                             RE::NiSourceTexture *a_base,
                             std::span<const LayerPass> a_layers,
                             const BoundLayerFields &a_fields) {
  if (DrawStackPass(a_target, a_base, a_layers, a_fields))
    return true;
  const std::optional<LayersWithRenderedFields> drawn =
      RenderFieldsToTargets(a_layers, a_fields, TextureSize{a_target.size});
  return drawn && RenderLayersOneByOne(a_target, a_base, drawn->passes);
}

std::unique_ptr<ProgramConstants> TextureLab::StackProgramConstants(
    std::span<REX::W32::ID3D11ShaderResourceView *> a_srvs,
    const BoundLayerFields &a_fields) {
  if (a_fields.pack.segments.empty() || !gpu_ || !gpu_->program)
    return nullptr;
  auto constants = EmptyProgramConstants();
  const std::span<REX::W32::ID3D11ShaderResourceView *> programSrvs =
      a_srvs.first(std::min<std::size_t>(a_srvs.size(), kPassSrvs));
  FillProgramConstants(*constants, programSrvs, PackedCode(a_fields.pack),
                       a_fields.bindings);
  FillLookups(programSrvs, a_fields.bindings);
  return constants;
}

REX::W32::ID3D11PixelShader *
TextureLab::StackShaderFor(const StackShape &a_shape) {
  if (!gpu_ || !gpu_->stack)
    return nullptr;
  if (generatedShaders_)
    if (auto *generated = generated_.StackFor(borrowedDevice_, a_shape))
      return generated;
  return gpu_->stack->shader.Get();
}

bool TextureLab::DrawStackPass(RenderTarget &a_target,
                               RE::NiSourceTexture *a_base,
                               std::span<const LayerPass> a_layers,
                               const BoundLayerFields &a_fields) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  const StackShape shape =
      StackShapeOf(ViewOf(a_base) != nullptr, a_layers, a_fields);
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      shape.layers.size() != a_layers.size() ||
      !CanRenderStack(shape, a_layers, a_fields))
    return false;
  const PixelPipeline &pipeline = *gpu_->stack;
  const RenderPass pass{*renderer, *borrowedContext_};
  std::array<REX::W32::ID3D11ShaderResourceView *, kStackSrvs> srvs{};
  const std::unique_ptr<ProgramConstants> program =
      StackProgramConstants(srvs, a_fields);
  StackConstants constants{};
  FillStackCounts(constants, shape);
  for (std::size_t index = 0; index < a_layers.size(); ++index)
    FillLayerConstants(constants, index, a_layers[index], shape.layers[index]);
  FillLayerSegments(constants, shape);
  BindLayerTextures(srvs, a_base, a_layers);
  UploadConstants(pass.Context(), pipeline.constants.Get(), constants);
  REX::W32::ID3D11Buffer *programBuffer =
      program && gpu_->program ? gpu_->program->constants.Get() : nullptr;
  if (program)
    UploadConstants(pass.Context(), programBuffer, *program);
  REX::W32::ID3D11Buffer *cbs[6]{nullptr, programBuffer,
                                 nullptr, nullptr,
                                 nullptr, pipeline.constants.Get()};
  auto *shader = StackShaderFor(shape);
  if (!shader)
    return false;
  DrawFullScreen(pass, a_target, {shader, srvs, cbs});
  if (fusionCheck_)
    CheckStack(a_target, a_base, a_layers, a_fields);
  return true;
}

bool TextureLab::RenderLayersOneByOne(RenderTarget &a_target,
                                      RE::NiSourceTexture *a_base,
                                      std::span<const LayerPass> a_layers) {
  auto *alternate = a_layers.size() > 1
                        ? Scratch(TextureSize{a_target.size}, a_target.format,
                                  MipPolicy::kNone)
                        : nullptr;
  if (a_layers.size() > 1 && !alternate)
    return false;
  auto *write = a_layers.size() % 2 ? &a_target : alternate;
  auto *other = a_layers.size() % 2 ? alternate : &a_target;
  RE::NiSourceTexture *previous = a_base;
  for (auto layer : a_layers) {
    LayerParams params;
    params.mode = Mode::kLayer;
    layer.previous = previous;
    params.layer = layer;
    if (!write || !Render(*write, nullptr, params))
      return false;
    previous = write->Texture();
    std::swap(write, other);
  }
  return true;
}

std::optional<TextureLab::LayersWithRenderedFields>
TextureLab::RenderFieldsToTargets(std::span<const LayerPass> a_layers,
                                  const BoundLayerFields &a_fields,
                                  TextureSize a_size) {
  if (!FieldsBound(a_fields))
    return std::nullopt;
  LayersWithRenderedFields result;
  result.passes.assign(a_layers.begin(), a_layers.end());
  const auto draw =
      [&](std::uint32_t a_segment) -> std::optional<RE::NiSourceTexture *> {
    const std::optional<ProgramCode> code =
        SegmentCode(a_fields.pack, a_segment);
    if (!code)
      return std::nullopt;
    auto target =
        Acquire(a_size, "stack field", TextureFormat::kRgba8, MipPolicy::kNone);
    if (!target || !DrawProgramPass(*target, *code, a_fields.bindings))
      return std::nullopt;
    auto *texture = target->Texture();
    result.targets.push_back(std::move(target));
    return texture;
  };
  for (auto &pass : result.passes) {
    if (pass.sourceSegment) {
      const auto texture = draw(*pass.sourceSegment);
      if (!texture)
        return std::nullopt;
      pass.source = *texture;
      pass.sourceSegment.reset();
    }
    if (pass.maskSegment) {
      const auto texture = draw(*pass.maskSegment);
      if (!texture)
        return std::nullopt;
      pass.mask = *texture;
      pass.maskSegment.reset();
    }
  }
  return result;
}

void TextureLab::SetFusionCheck(bool a_enabled) noexcept {
  fusionCheck_ = a_enabled;
}

TextureLab::EquivalenceCheckTotals TextureLab::DrainFusionChecks() {
  return std::exchange(fusionChecks_, {});
}

void TextureLab::CheckStack(RenderTarget &a_fused, RE::NiSourceTexture *a_base,
                            std::span<const LayerPass> a_layers,
                            const BoundLayerFields &a_fields) {
  const TextureSize size{a_fused.size};
  const auto drawn = RenderFieldsToTargets(a_layers, a_fields, size);
  if (!drawn)
    return;
  const auto chain =
      Acquire(size, "fusion check", a_fused.format, MipPolicy::kNone);
  if (!chain || !RenderLayersOneByOne(*chain, a_base, drawn->passes))
    return;
  const auto rgb = MaxDifference(a_fused, *chain, ShaderChannel::kRgb);
  const auto alpha = MaxDifference(a_fused, *chain, ShaderChannel::kA);
  if (!rgb || !alpha)
    return;
  const float difference = std::max(*rgb, *alpha);
  ++fusionChecks_.checks;
  fusionChecks_.maxDifference =
      std::max(fusionChecks_.maxDifference, difference);
  if (difference > 1.5f / 255.0f)
    ++fusionChecks_.overOneStep;
}

bool TextureLab::RenderProgram(RenderTarget &a_target,
                               const FieldProgram &a_program,
                               const ProgramBindings &a_bindings) {
  if (a_bindings.inputCount != a_program.Inputs().size() ||
      a_bindings.textureCount != a_program.TextureCount() ||
      a_bindings.lookupCount != a_program.FunctionLookups().size())
    return false;
  if (generatedShaders_)
    if (auto *shader = generated_.ProgramFor(borrowedDevice_, a_program)) {
      if (!DrawProgramPass(a_target, CodeOf(a_program), a_bindings, shader))
        return false;
      if (fusionCheck_)
        CheckGeneratedProgram(a_target, a_program, a_bindings);
      return true;
    }
  return DrawProgramPass(a_target, CodeOf(a_program), a_bindings);
}

void TextureLab::CheckGeneratedProgram(RenderTarget &a_generated,
                                       const FieldProgram &a_program,
                                       const ProgramBindings &a_bindings) {
  const TextureSize size{a_generated.size};
  const auto interpreted =
      Acquire(size, "program check", a_generated.format, MipPolicy::kNone);
  if (!interpreted ||
      !DrawProgramPass(*interpreted, CodeOf(a_program), a_bindings))
    return;
  const auto rgb =
      MaxDifference(a_generated, *interpreted, ShaderChannel::kRgb);
  const auto alpha =
      MaxDifference(a_generated, *interpreted, ShaderChannel::kA);
  if (!rgb || !alpha)
    return;
  const float difference = std::max(*rgb, *alpha);
  ++programChecks_.checks;
  programChecks_.maxDifference =
      std::max(programChecks_.maxDifference, difference);
  if (difference > 1.5f / 255.0f)
    ++programChecks_.overOneStep;
}

TextureLab::EquivalenceCheckTotals TextureLab::DrainGeneratedProgramChecks() {
  return std::exchange(programChecks_, {});
}

void TextureLab::SetGeneratedShaders(bool a_enabled) noexcept {
  generatedShaders_ = a_enabled;
}

void TextureLab::FillLookups(
    std::span<REX::W32::ID3D11ShaderResourceView *> a_srvs,
    const ProgramBindings &a_bindings) {
  const auto lookups =
      std::min<std::size_t>(a_bindings.lookupCount, kProgramLookups);
  for (std::size_t c = 0; c < lookups; ++c) {
    const auto slot = kProgramTextures + c;
    if (slot < a_srvs.size())
      a_srvs[slot] =
          a_bindings.lookups[c] ? a_bindings.lookups[c]->srv.Get() : nullptr;
  }
}

bool TextureLab::DrawProgramPass(RenderTarget &a_target,
                                 const ProgramCode &a_code,
                                 const ProgramBindings &a_bindings,
                                 REX::W32::ID3D11PixelShader *a_shader) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_ || !gpu_->program.has_value() || !ProgramFits(a_code, a_bindings))
    return false;
  const PixelPipeline &pipeline = *gpu_->program;
  const RenderPass pass{*renderer, *borrowedContext_};
  const std::unique_ptr<ProgramConstants> constants = EmptyProgramConstants();
  REX::W32::ID3D11ShaderResourceView *srvs[kPassSrvs]{};
  FillProgramConstants(*constants, srvs, a_code, a_bindings);
  FillLookups(srvs, a_bindings);
  constants->misc[2] = a_code.result != ValueType::kScalar ? 1.0f : 0.0f;
  UploadConstants(pass.Context(), pipeline.constants.Get(), *constants);
  REX::W32::ID3D11Buffer *cbs[2]{gpu_->constants.Get(),
                                 pipeline.constants.Get()};
  DrawFullScreen(pass, a_target,
                 {a_shader ? a_shader : pipeline.shader.Get(), srvs, cbs});
  return true;
}

struct UploadedBakeBuffers {
  ComPtr<ID3D11Buffer> vertices;
  ComPtr<ID3D11Buffer> indices;
  std::uint32_t indexCount = 0;
};

namespace {
bool BakeBuffersFit(const BakeBuffers &a_bake) {
  return a_bake.vertices.size() <=
             std::numeric_limits<std::uint32_t>::max() / sizeof(BakeVertex) &&
         a_bake.indices.size() <=
             std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint32_t);
}

std::optional<UploadedBakeBuffers>
UploadBakeBuffers(ID3D11Device &a_device, const BakeBuffers &a_bake) {
  if (!BakeBuffersFit(a_bake)) {
    return std::nullopt;
  }
  D3D11_BUFFER_DESC vbDesc{};
  vbDesc.byteWidth =
      static_cast<std::uint32_t>(a_bake.vertices.size() * sizeof(BakeVertex));
  vbDesc.usage = D3D11_USAGE_IMMUTABLE;
  vbDesc.bindFlags = D3D11_BIND_VERTEX_BUFFER;
  D3D11_BUFFER_DESC ibDesc{};
  ibDesc.byteWidth =
      static_cast<std::uint32_t>(a_bake.indices.size() * sizeof(std::uint32_t));
  ibDesc.usage = D3D11_USAGE_IMMUTABLE;
  ibDesc.bindFlags = D3D11_BIND_INDEX_BUFFER;
  D3D11_SUBRESOURCE_DATA vbData{a_bake.vertices.data(), 0, 0};
  D3D11_SUBRESOURCE_DATA ibData{a_bake.indices.data(), 0, 0};
  UploadedBakeBuffers buffers;
  if (Failed(a_device.CreateBuffer(&vbDesc, &vbData,
                                   buffers.vertices.GetAddressOf())) ||
      Failed(a_device.CreateBuffer(&ibDesc, &ibData,
                                   buffers.indices.GetAddressOf()))) {
    logger::error("TextureLab: bake buffers could not be created");
    return std::nullopt;
  }
  buffers.indexCount = static_cast<std::uint32_t>(a_bake.indices.size());
  return buffers;
}
}

bool TextureLab::BakeMesh(RenderTarget &a_target, const BakeBuffers &a_bake) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !borrowedDevice_ ||
      !a_target.rtv.Get() || !gpu_->bake.has_value() ||
      a_bake.vertices.empty() || a_bake.indices.empty()) {
    return false;
  }
  if (!BakeBuffersFit(a_bake)) {
    logger::error("TextureLab: bake buffers exceed the D3D11 byte range");
    return false;
  }
  const RenderPass pass{*renderer, *borrowedContext_};
  const std::optional<UploadedBakeBuffers> buffers =
      UploadBakeBuffers(*borrowedDevice_, a_bake);
  if (!buffers) {
    return false;
  }
  DrawBakeTriangles(pass, a_target, *buffers);
  if (!DrawDilation(pass, a_target) && a_target.mips == MipPolicy::kGenerate) {
    GenerateMips(pass, a_target);
  }
  return true;
}

void TextureLab::DrawBakeTriangles(const RenderPass &a_pass,
                                   RenderTarget &a_target,
                                   const UploadedBakeBuffers &a_buffers) {
  if (!gpu_ || !gpu_->bake.has_value()) {
    return;
  }
  const BakePipeline &pipeline = *gpu_->bake;
  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv.Get();
  const float empty[4]{0.0f, 0.0f, 0.0f, 0.0f};
  a_pass.Context().ClearRenderTargetView(rtv, empty);
  BindTarget(a_pass, a_target);
  const std::uint32_t stride = sizeof(BakeVertex);
  const std::uint32_t offset = 0;
  a_pass.Context().IASetInputLayout(pipeline.layout.Get());
  a_pass.Context().IASetVertexBuffers(0, 1, a_buffers.vertices.GetAddressOf(),
                                      &stride, &offset);
  a_pass.Context().IASetIndexBuffer(a_buffers.indices.Get(),
                                    DXGI_FORMAT_R32_UINT, 0);
  a_pass.Context().IASetPrimitiveTopology(
      D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  a_pass.Context().VSSetShader(pipeline.vertex.Get(), nullptr, 0);
  a_pass.Context().PSSetShader(pipeline.pixel.Get(), nullptr, 0);
  REX::W32::ID3D11ShaderResourceView *noSrvs[kPassSrvs]{};
  a_pass.Context().PSSetShaderResources(0, kPassSrvs, noSrvs);
  a_pass.Context().DrawIndexed(a_buffers.indexCount, 0, 0);
  UnbindTarget(a_pass, kPassSrvs);
}

bool TextureLab::DrawDilation(const RenderPass &a_pass,
                              RenderTarget &a_target) {
  const PixelPipeline *dilate =
      gpu_ && gpu_->dilate.has_value() ? &gpu_->dilate.value() : nullptr;
  RenderTarget *gutter = dilate ? Scratch(TextureSize{a_target.size},
                                          a_target.format, MipPolicy::kNone)
                                : nullptr;
  if (!dilate || !gutter || !gutter->rtv.Get() ||
      gutter->size != a_target.size) {
    return false;
  }
  REX::W32::ID3D11ShaderResourceView *fromTarget[]{a_target.srv.Get()};
  DrawFullScreen(a_pass, *gutter, {dilate->shader.Get(), fromTarget, {}});
  REX::W32::ID3D11ShaderResourceView *fromGutter[]{gutter->srv.Get()};
  DrawFullScreen(a_pass, a_target, {dilate->shader.Get(), fromGutter, {}});
  return true;
}

bool TextureLab::RenderRipple(RenderTarget &a_target,
                              const RipplePass &a_pass) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_->ripple.has_value()) {
    return false;
  }
  const PixelPipeline &pipeline = *gpu_->ripple;
  const RenderPass pass{*renderer, *borrowedContext_};
  const auto *positions = DataOf(a_pass.positions);
  if (!positions || !positions->resourceView) {
    return false;
  }
  RippleConstants constants{};
  const auto count =
      std::min<std::size_t>(a_pass.firingCount, a_pass.firings.size());
  for (std::size_t k = 0; k < count; ++k) {
    constants.firings[k][0] = a_pass.firings[k].origin.x;
    constants.firings[k][1] = a_pass.firings[k].origin.y;
    constants.firings[k][2] = a_pass.firings[k].origin.z;
    constants.firings[k][3] = a_pass.firings[k].age;
  }
  constants.shape[0] = a_pass.speed;
  constants.shape[1] = a_pass.width;
  constants.shape[2] = a_pass.decay;
  constants.shape[3] = a_pass.disc ? 1.0f : 0.0f;
  constants.misc[0] = static_cast<float>(count);
  constants.misc[1] = a_pass.frame;
  constants.direction[0] = a_pass.direction.x;
  constants.direction[1] = a_pass.direction.y;
  constants.direction[2] = a_pass.direction.z;
  constants.direction[3] = a_pass.directional ? 1.0f : 0.0f;

  pass.Context().UpdateSubresource(pipeline.constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  REX::W32::ID3D11ShaderResourceView *srvs[kPassSrvs]{
      reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
          positions->resourceView)};
  REX::W32::ID3D11Buffer *cbs[3]{nullptr, nullptr, pipeline.constants.Get()};
  DrawFullScreen(pass, a_target, {pipeline.shader.Get(), srvs, cbs});
  return true;
}

bool TextureLab::SubmitMaterialSample(RE::NiSourceTexture *a_rmaos,
                                      RE::NiSourceTexture *a_diffuse,
                                      MaterialReadback &a_readback) {
  static_assert(static_cast<std::size_t>(kSampleSide) * kSampleSide <=
                kMaxSampleTexels);
  const auto fail = [&](RE::NiSourceTexture *a_texture,
                        std::string_view a_why) {
    if (sampleWarned_.insert(a_texture).second) {
      logger::warn("TextureLab: material sample: {}", a_why);
    }
    return false;
  };
  if (!Init()) {
    return fail(nullptr, "the lab is unavailable");
  }
  const auto rmaosExtent = ExtentOf(a_rmaos);
  if (!rmaosExtent) {
    return fail(a_rmaos, "the RMAOS map is null or not a resident 2D texture");
  }
  const auto diffuseExtent = ExtentOf(a_diffuse);
  if (!diffuseExtent) {
    return fail(a_diffuse,
                "the diffuse map is null or not a resident 2D texture");
  }
  auto target = Acquire(TextureSize(kSampleSide), "sample");
  if (!target || target->size != kSampleSide) {
    return fail(a_rmaos, "no sample target");
  }
  if (!EnsureSampleStaging(a_readback)) {
    return fail(a_rmaos, "no sample staging textures");
  }
  const auto copy = [&](RE::NiSourceTexture *a_map, const Extent &a_extent,
                        REX::W32::ID3D11Texture2D &a_staging) {
    LayerParams params;
    params.mode = Mode::kCopy;
    params.scroll.sourceMip = MipThatFits(a_extent, kSampleSide);
    return Render(*target, a_map, params) && CopyToStaging(*target, a_staging);
  };
  if (!copy(a_rmaos, *rmaosExtent, *a_readback.rmaos.Get())) {
    return fail(a_rmaos, "the RMAOS map could not be copied");
  }
  if (!copy(a_diffuse, *diffuseExtent, *a_readback.diffuse.Get())) {
    return fail(a_diffuse, "the diffuse map could not be copied");
  }
  a_readback.pending = true;
  return true;
}

std::optional<TextureLab::MaterialSampleResult>
TextureLab::CollectMaterialSample(MaterialReadback &a_readback) {
  return ReadMaterialSample(a_readback, false);
}

std::optional<MaterialSample>
TextureLab::SampleMaterial(RE::NiSourceTexture *a_rmaos,
                           RE::NiSourceTexture *a_diffuse) {
  MaterialReadback readback;
  if (!SubmitMaterialSample(a_rmaos, a_diffuse, readback)) {
    return std::nullopt;
  }
  auto sample = ReadMaterialSample(readback, true);
  if (!sample || !*sample) {
    logger::warn("TextureLab: material sample: {}",
                 sample ? sample->error() : "no readback was pending");
    return std::nullopt;
  }
  return std::move(**sample);
}

bool TextureLab::RenderClusters(RenderTarget &a_target,
                                RE::NiSourceTexture *a_rmaos,
                                RE::NiSourceTexture *a_diffuse,
                                const MaterialAnalysis &a_analysis) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_->clusters.has_value()) {
    return false;
  }
  const PixelPipeline &pipeline = *gpu_->clusters;
  const RenderPass pass{*renderer, *borrowedContext_};
  const auto *rmaosData = DataOf(a_rmaos);
  const auto *diffuseData = DataOf(a_diffuse);
  if (!rmaosData || !rmaosData->resourceView || !diffuseData ||
      !diffuseData->resourceView ||
      a_analysis.clusters.size() > kMaxMaterialClusters) {
    return false;
  }
  const auto scale = [](float a_weight) {
    return std::isfinite(a_weight) && a_weight > 0.0f ? a_weight : 0.0f;
  };
  ClusterConstants constants{};
  for (std::size_t k = 0; k < a_analysis.clusters.size(); ++k) {
    const auto &cluster = a_analysis.clusters[k];
    constants.centroidRmaos[k][0] = cluster.centroid.roughness;
    constants.centroidRmaos[k][1] = cluster.centroid.metallic;
    constants.centroidRmaos[k][2] = cluster.centroid.occlusion;
    constants.centroidRmaos[k][3] = cluster.centroid.reflectance;
    constants.centroidDiffuse[k][0] = cluster.centroid.diffuse.x;
    constants.centroidDiffuse[k][1] = cluster.centroid.diffuse.y;
    constants.centroidDiffuse[k][2] = cluster.centroid.diffuse.z;
    constants.centroidLuma[k][0] = cluster.centroid.luma;
    constants.centroidLuma[k][1] = static_cast<float>(cluster.id);
  }
  const auto &w = a_analysis.settings.weights;
  constants.weights[0] = scale(w.roughness);
  constants.weights[1] = scale(w.metallic);
  constants.weights[2] = scale(w.occlusion);
  constants.weights[3] = scale(w.reflectance);
  constants.misc[0] = scale(w.luma);
  constants.misc[2] = scale(w.color) / 3.0f;
  constants.misc[1] = static_cast<float>(a_analysis.clusters.size());

  pass.Context().UpdateSubresource(pipeline.constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  REX::W32::ID3D11ShaderResourceView *srvs[kPassSrvs]{
      reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
          diffuseData->resourceView),
      reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
          rmaosData->resourceView)};
  REX::W32::ID3D11Buffer *cbs[4]{nullptr, nullptr, nullptr,
                                 pipeline.constants.Get()};
  DrawFullScreen(pass, a_target, {pipeline.shader.Get(), srvs, cbs});
  return true;
}

TextureLab::ReductionLevel *
TextureLab::ReductionLevelFor(ReductionExtent a_extent) {
  const auto key = std::make_pair(a_extent.width, a_extent.height);
  if (const auto found = reductionLevels_.find(key);
      found != reductionLevels_.end())
    return &found->second;
  D3D11_TEXTURE2D_DESC desc{};
  desc.width = a_extent.width;
  desc.height = a_extent.height;
  desc.mipLevels = 1;
  desc.arraySize = 1;
  desc.format = DXGI_FORMAT_R32G32B32A32_FLOAT;
  desc.sampleDesc.count = 1;
  desc.usage = D3D11_USAGE_DEFAULT;
  desc.bindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  ReductionLevel level;
  if (Failed(borrowedDevice_->CreateTexture2D(&desc, nullptr,
                                              level.texture.GetAddressOf())) ||
      Failed(borrowedDevice_->CreateShaderResourceView(
          level.texture.Get(), nullptr, level.srv.GetAddressOf())) ||
      Failed(borrowedDevice_->CreateRenderTargetView(
          level.texture.Get(), nullptr, level.rtv.GetAddressOf()))) {
    logger::error("TextureLab: reduction level {}x{} unavailable",
                  a_extent.width, a_extent.height);
    return nullptr;
  }
  return &reductionLevels_.emplace(key, std::move(level)).first->second;
}

bool TextureLab::DrawReduction(RenderTarget &a_field, ReductionKind a_kind,
                               ValueType a_type,
                               REX::W32::ID3D11Texture2D &a_staging) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ ||
      !gpu_->reduce.has_value() || !a_field.texture.Get() || !a_field.srv.Get())
    return false;
  D3D11_TEXTURE2D_DESC desc{};
  a_field.texture->GetDesc(&desc);
  const ReductionExtent field{desc.width, desc.height};
  const auto extents = ReductionLevels(field);
  if (desc.format != DXGI_FORMAT_R32G32B32A32_FLOAT || extents.empty())
    return false;
  std::vector<ReductionLevel *> levels;
  for (const auto &extent : extents) {
    auto *level = ReductionLevelFor(extent);
    if (!level)
      return false;
    levels.push_back(level);
  }
  const PixelPipeline &pipeline = *gpu_->reduce;
  const RenderPass pass{*renderer, *borrowedContext_};
  auto &context = pass.Context();
  context.IASetInputLayout(nullptr);
  context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  context.VSSetShader(gpu_->vertex.Get(), nullptr, 0);
  context.PSSetShader(pipeline.shader.Get(), nullptr, 0);
  const float blendFactor[4]{};
  context.OMSetBlendState(gpu_->blend.Get(), blendFactor, 0xFFFFFFFF);
  context.OMSetDepthStencilState(gpu_->depth.Get(), 0);
  context.RSSetState(gpu_->raster.Get());
  REX::W32::ID3D11ShaderResourceView *source = a_field.srv.Get();
  ReductionExtent sourceExtent = field;
  for (std::size_t i = 0; i < levels.size(); ++i) {
    const ReductionConstants constants{
        {static_cast<std::uint32_t>(a_kind), ReductionComponents(a_type),
         sourceExtent.width, sourceExtent.height},
        {i == 0 ? 1u : 0u, 0u, 0u, 0u}};
    context.UpdateSubresource(pipeline.constants.Get(), 0, nullptr, &constants,
                              0, 0);
    REX::W32::ID3D11RenderTargetView *rtv = levels[i]->rtv.Get();
    context.OMSetRenderTargets(1, &rtv, nullptr);
    D3D11_VIEWPORT viewport{};
    viewport.width = static_cast<float>(extents[i].width);
    viewport.height = static_cast<float>(extents[i].height);
    viewport.maxDepth = 1.0f;
    context.RSSetViewports(1, &viewport);
    context.PSSetShaderResources(0, 1, &source);
    REX::W32::ID3D11Buffer *buffers[]{pipeline.constants.Get()};
    context.PSSetConstantBuffers(4, 1, buffers);
    context.Draw(3, 0);
    UnbindTarget(pass, 1);
    source = levels[i]->srv.Get();
    sourceExtent = extents[i];
  }
  context.CopyResource(&a_staging, levels.back()->texture.Get());
  return true;
}
}
