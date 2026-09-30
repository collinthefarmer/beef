// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/TextureLab.h"

#include "render/D3DResult.h"
#include "render/ShaderConstants.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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

bool TextureLab::Render(RenderTarget &a_target, RE::NiSourceTexture *a_source,
                        const LayerParams &a_params) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get()) {
    return false;
  }
  const RenderPass pass{*renderer, *borrowedContext_};
  const bool layerPass = a_params.mode == Mode::kLayer;
  auto *sourceData = DataOf(layerPass ? a_params.layer.source : a_source);
  if (!layerPass && (!sourceData || !sourceData->resourceView)) {
    sourceData = DataOf(a_params.map.texture);
    if (!sourceData || !sourceData->resourceView) {
      return false;
    }
  }
  const bool haveSource = sourceData && sourceData->resourceView;

  const auto &sc = layerPass ? a_params.layer.input.transform : a_params.scroll;
  LayerConstants constants{};
  constants.offsetScale[0] = sc.uOffset;
  constants.offsetScale[1] = sc.vOffset;
  constants.offsetScale[2] = sc.tileU;
  constants.offsetScale[3] = sc.tileV;
  constants.flags[0] = sc.mirrorU ? 1.0f : 0.0f;
  constants.flags[1] = sc.mirrorV ? 1.0f : 0.0f;
  constants.flags[2] = sc.transpose ? 1.0f : 0.0f;
  constants.flags[3] = static_cast<float>(a_params.mode);
  constants.extra[0] = sc.sourceMip;
  auto *mapData =
      DataOf(layerPass ? a_params.layer.mask : a_params.map.texture);
  const bool haveMap = mapData && mapData->resourceView &&
                       (layerPass || a_params.map.reading != MapReading::kNone);
  constants.extra[1] =
      haveMap && !layerPass ? static_cast<float>(a_params.map.reading) : 0.0f;
  auto *prevData = layerPass ? DataOf(a_params.layer.previous) : nullptr;
  const bool havePrev = prevData && prevData->resourceView;
  if (layerPass) {
    const auto &lp = a_params.layer;
    constants.layer[0] =
        static_cast<float>(std::to_underlying(lp.input.channel));
    constants.layer[1] = lp.input.meshSpace ? 1.0f : 0.0f;
    constants.layer[2] = static_cast<float>(lp.blend);
    constants.layer[3] = lp.opacity;
    constants.layerColor[0] = lp.color[0];
    constants.layerColor[1] = lp.color[1];
    constants.layerColor[2] = lp.color[2];
    constants.layerColor[3] = lp.normalize;
    constants.layerMask[0] =
        haveMap ? static_cast<float>(std::to_underlying(lp.maskChannel))
                : -1.0f;
    constants.layerMask[1] = static_cast<float>(lp.channels);
    constants.layerMask[2] = havePrev ? 1.0f : 0.0f;
    constants.layerMask[3] = haveSource ? 1.0f : 0.0f;
    constants.layerCurve[0] = lp.curve && lp.curve->srv.Get() ? 1.0f : 0.0f;
  }
  if (a_params.mode == Mode::kChannel) {
    constants.extra[2] =
        static_cast<float>(std::to_underlying(a_params.channel.channel));
    constants.extra[3] = a_params.channel.slope ? 1.0f : 0.0f;
  }

  pass.Context().UpdateSubresource(gpu_->constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  REX::W32::ID3D11ShaderResourceView *srvs[4]{
      haveSource ? reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
                       sourceData->resourceView)
                 : nullptr,
      haveMap ? reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
                    mapData->resourceView)
              : nullptr,
      havePrev ? reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
                     prevData->resourceView)
               : nullptr,
      layerPass && a_params.layer.curve ? a_params.layer.curve->srv.Get()
                                        : nullptr};
  REX::W32::ID3D11Buffer *cbs[1]{gpu_->constants.Get()};
  DrawFullScreen(pass, a_target, {gpu_->pixel.Get(), srvs, cbs});
  return true;
}

bool TextureLab::RenderProgram(RenderTarget &a_target,
                               const InterpreterProgram &a_program,
                               const InterpreterBindings &a_pass) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_->program.has_value() ||
      a_pass.inputCount != a_program.Inputs().size() ||
      a_pass.textureCount != a_program.TextureCount() ||
      a_pass.lookupCount != a_program.FunctionLookups().size()) {
    return false;
  }
  const PixelPipeline &pipeline = *gpu_->program;
  const RenderPass pass{*renderer, *borrowedContext_};
  auto constants = std::make_unique<ProgramConstants>();
  std::memset(constants.get(), 0, sizeof(ProgramConstants));
  for (std::size_t k = 0; k < a_program.Instructions().size(); ++k) {
    constants->code[k][0] =
        static_cast<float>(a_program.Instructions()[k].opcode);
    constants->code[k][1] = a_program.Instructions()[k].number;
    constants->code[k][2] =
        static_cast<float>(a_program.Instructions()[k].index);
    constants->code[k][3] =
        static_cast<float>(a_program.Instructions()[k].components);
  }
  for (std::size_t r = 0; r < a_program.Inputs().size(); ++r) {
    const auto *texture = Get<InterpreterTextureInput>(a_program.Inputs()[r]);
    constants->refs[r][0] = texture ? 1.0f : 0.0f;
    constants->refs[r][1] = texture ? static_cast<float>(texture->slot) : 0.0f;
    constants->refValues[r][0] = a_pass.values[r].x;
    constants->refValues[r][1] = a_pass.values[r].y;
    constants->refValues[r][2] = a_pass.values[r].z;
  }
  REX::W32::ID3D11ShaderResourceView *srvs[kPassSrvs]{};
  for (std::size_t t = 0; t < a_pass.textureCount; ++t) {
    const auto &tex = a_pass.textures[t];
    const auto *data = DataOf(tex.texture);
    srvs[t] = data ? reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
                         data->resourceView)
                   : nullptr;
    const auto &sc = tex.sampling.transform;
    constants->texParams[t][0] =
        static_cast<float>(std::to_underlying(tex.sampling.channel));
    constants->texParams[t][1] = tex.sampling.meshSpace ? 1.0f : 0.0f;
    constants->texParams[t][2] = tex.normalize;
    constants->texParams[t][3] = sc.sourceMip;
    constants->texTransform[t][0] = sc.uOffset;
    constants->texTransform[t][1] = sc.vOffset;
    constants->texTransform[t][2] = sc.tileU;
    constants->texTransform[t][3] = sc.tileV;
    constants->texFlags[t][0] = sc.mirrorU ? 1.0f : 0.0f;
    constants->texFlags[t][1] = sc.mirrorV ? 1.0f : 0.0f;
    constants->texFlags[t][2] = sc.transpose ? 1.0f : 0.0f;
    constants->texFlags[t][3] = tex.sampling.nearest ? 1.0f : 0.0f;
  }
  for (std::size_t c = 0; c < a_pass.lookupCount; ++c) {
    srvs[kProgramTextures + c] =
        a_pass.lookups[c] ? a_pass.lookups[c]->srv.Get() : nullptr;
  }
  constants->misc[1] = static_cast<float>(a_program.Instructions().size());
  constants->misc[2] =
      a_program.ResultType() != ValueType::kScalar ? 1.0f : 0.0f;

  pass.Context().UpdateSubresource(pipeline.constants.Get(), 0, nullptr,
                                   constants.get(), 0, 0);
  REX::W32::ID3D11Buffer *cbs[2]{gpu_->constants.Get(),
                                 pipeline.constants.Get()};
  DrawFullScreen(pass, a_target, {pipeline.shader.Get(), srvs, cbs});
  return true;
}

bool TextureLab::BakeMesh(RenderTarget &a_target, const BakeBuffers &a_bake) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !borrowedDevice_ ||
      !a_target.rtv.Get() || !gpu_->bake.has_value() ||
      a_bake.vertices.empty() || a_bake.indices.empty()) {
    return false;
  }
  const BakePipeline &pipeline = *gpu_->bake;
  if (a_bake.vertices.size() >
          std::numeric_limits<std::uint32_t>::max() / sizeof(BakeVertex) ||
      a_bake.indices.size() >
          std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint32_t)) {
    logger::error("TextureLab: bake buffers exceed the D3D11 byte range");
    return false;
  }
  const RenderPass pass{*renderer, *borrowedContext_};
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
  ComPtr<ID3D11Buffer> vb;
  ComPtr<ID3D11Buffer> ib;
  if (Failed(
          borrowedDevice_->CreateBuffer(&vbDesc, &vbData, vb.GetAddressOf())) ||
      Failed(
          borrowedDevice_->CreateBuffer(&ibDesc, &ibData, ib.GetAddressOf()))) {
    logger::error("TextureLab: bake buffers could not be created");
    return false;
  }

  REX::W32::ID3D11RenderTargetView *rtv = a_target.rtv.Get();
  const float empty[4]{0.0f, 0.0f, 0.0f, 0.0f};
  pass.Context().ClearRenderTargetView(rtv, empty);
  BindTarget(pass, a_target);
  const std::uint32_t stride = sizeof(BakeVertex);
  const std::uint32_t offset = 0;
  pass.Context().IASetInputLayout(pipeline.layout.Get());
  pass.Context().IASetVertexBuffers(0, 1, vb.GetAddressOf(), &stride, &offset);
  pass.Context().IASetIndexBuffer(ib.Get(), DXGI_FORMAT_R32_UINT, 0);
  pass.Context().IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  pass.Context().VSSetShader(pipeline.vertex.Get(), nullptr, 0);
  pass.Context().PSSetShader(pipeline.pixel.Get(), nullptr, 0);
  REX::W32::ID3D11ShaderResourceView *noSrvs[kPassSrvs]{};
  pass.Context().PSSetShaderResources(0, kPassSrvs, noSrvs);
  pass.Context().DrawIndexed(static_cast<std::uint32_t>(a_bake.indices.size()),
                             0, 0);
  UnbindTarget(pass, kPassSrvs);
  const PixelPipeline *dilate =
      gpu_->dilate.has_value() ? &gpu_->dilate.value() : nullptr;
  RenderTarget *gutter = dilate ? Scratch(TextureSize{a_target.size},
                                          a_target.format, MipPolicy::kNone)
                                : nullptr;
  if (dilate && gutter && gutter->rtv.Get() && gutter->size == a_target.size) {
    REX::W32::ID3D11ShaderResourceView *fromTarget[]{a_target.srv.Get()};
    DrawFullScreen(pass, *gutter, {dilate->shader.Get(), fromTarget, {}});
    REX::W32::ID3D11ShaderResourceView *fromGutter[]{gutter->srv.Get()};
    DrawFullScreen(pass, a_target, {dilate->shader.Get(), fromGutter, {}});
  } else if (a_target.mips == MipPolicy::kGenerate) {
    GenerateMips(pass, a_target);
  }
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
