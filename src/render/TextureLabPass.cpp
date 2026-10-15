// GPL-3.0-only with the additional permission in COPYING.md.
#include "render/TextureLab.h"

#include "mesh/FieldFilter.h"
#include "planners/ShaderConstants.h"
#include "render/D3DResult.h"

#include <algorithm>
#include <bit>
#include <cmath>
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

TextureExtent ExtentOrEmpty(RE::NiSourceTexture *a_texture) {
  return TextureLab::ExtentOf(a_texture).value_or(TextureExtent{});
}

Tiling TilingOf(const TextureLab::LayerInput &a_input) {
  if (a_input.meshSpace)
    return {};
  return {a_input.transform.tileU, a_input.transform.tileV};
}

float ReadMip(RE::NiSourceTexture *a_texture,
              const TextureLab::LayerInput &a_input,
              std::uint32_t a_targetSide) {
  if (a_input.nearest)
    return a_input.transform.sourceMip;
  return SampleMip(ExtentOrEmpty(a_texture), TilingOf(a_input), a_targetSide,
                   a_input.transform.sourceMip);
}

float WholeReadMip(RE::NiSourceTexture *a_texture, std::uint32_t a_targetSide) {
  return FootprintMip(ExtentOrEmpty(a_texture), {}, a_targetSide);
}

NormalDetail NormalDetailOf(RE::NiSourceTexture *a_normal) {
  const TextureExtent extent = ExtentOrEmpty(a_normal);
  if (extent.width == 0 || extent.height == 0 || extent.mips == 0) {
    return {};
  }
  const NormalDetailScale scale = NormalDetailScaleOf(extent);
  return {scale.fineMip, scale.coarseMip, scale.footprint, ShaderBool::kYes};
}

REX::W32::ID3D11ShaderResourceView *NormalView(RE::NiSourceTexture *a_normal) {
  const auto *data = DataOf(a_normal);
  return data ? reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
                    data->resourceView)
              : nullptr;
}

static_assert(kStackBaseSlot == TextureLab::kPassSrvs);
constexpr std::size_t kStackSrvs = kStackSlotEnd;

void BindSlot(std::span<REX::W32::ID3D11ShaderResourceView *> srvs,
              std::size_t slot, REX::W32::ID3D11ShaderResourceView *view) {
  if (slot < srvs.size())
    srvs[slot] = view;
}

using ConstantBindings = std::array<ID3D11Buffer *, kConstantSlotCount>;

struct ConstantBinding {
  ConstantSlot slot = ConstantSlot::kPass;
  ID3D11Buffer *buffer = nullptr;
};

ConstantBindings BindingsOf(std::initializer_list<ConstantBinding> a_bound) {
  ConstantBindings bindings{};
  for (const ConstantBinding &bound : a_bound)
    if (SlotOf(bound.slot) < bindings.size())
      bindings[SlotOf(bound.slot)] = bound.buffer;
  return bindings;
}

void SetLane(std::span<Float4> a_rows, std::size_t a_at, float a_value) {
  if (a_at / 4 < a_rows.size())
    a_rows[a_at / 4][a_at % 4] = a_value;
}

std::uint32_t PlacementOptions(const TextureLab::Scroll &a_placement) {
  return (a_placement.mirrorU ? kReadMirrorU : 0u) |
         (a_placement.mirrorV ? kReadMirrorV : 0u) |
         (a_placement.transpose ? kReadTranspose : 0u);
}

TextureRead PlacedRead(const TextureLab::Scroll &a_placement, float a_mip) {
  return {.offset = {a_placement.uOffset, a_placement.vOffset},
          .tile = {a_placement.tileU, a_placement.tileV},
          .mip = a_mip,
          .options = PlacementOptions(a_placement)};
}

TextureRead InputRead(const TextureLab::LayerInput &a_input, float a_mip,
                      float a_normalize) {
  TextureRead read = PlacedRead(a_input.transform, a_mip);
  read.normalize = a_normalize;
  read.channel = std::to_underlying(a_input.channel);
  read.options |= (a_input.meshSpace ? kReadMeshSpace : 0u) |
                  (a_input.nearest ? kReadNearest : 0u);
  return read;
}

LayerMix MixOf(const TextureLab::LayerPass &a_layer, std::uint32_t a_options) {
  return {.color = {a_layer.color[0], a_layer.color[1], a_layer.color[2]},
          .opacity = a_layer.opacity,
          .blend = a_layer.blend,
          .writeChannels = a_layer.channels,
          .maskChannel = std::to_underlying(a_layer.maskChannel),
          .options = a_options};
}

template <class Constants>
void UploadConstants(ID3D11DeviceContext &context, ID3D11Buffer *buffer,
                     const Constants &constants) {
  if (buffer)
    context.UpdateSubresource(buffer, 0, nullptr, &constants, 0, 0);
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

void StoreInstruction(ProgramConstants &constants, std::size_t k,
                      const ProgramInstruction &instruction) {
  const std::size_t row = k / kInstructionsPerRow;
  if (row >= std::size(constants.programCode))
    return;
  Uint4 &pair = constants.programCode[row];
  const std::uint32_t word = InstructionWord(instruction);
  const auto number = std::bit_cast<std::uint32_t>(instruction.number);
  if (k % kInstructionsPerRow == 0) {
    pair.x = word;
    pair.y = number;
    return;
  }
  pair.z = word;
  pair.w = number;
}

std::size_t
FillProgramInstructions(ProgramConstants &constants,
                        std::span<const ProgramInstruction> instructions) {
  const std::size_t count =
      std::min<std::size_t>(instructions.size(), kProgramInstructions);
  for (std::size_t k = 0; k < count; ++k)
    StoreInstruction(constants, k, instructions[k]);
  return count;
}

void FillProgramInputs(ProgramConstants &constants,
                       std::span<const ProgramInput> inputs,
                       const TextureLab::ProgramBindings &bindings) {
  const std::size_t count = std::min({inputs.size(), bindings.values.size(),
                                      std::size(constants.programInputs)});
  for (std::size_t row = 0; row < count; ++row) {
    const auto *texture = Get<ProgramTextureInput>(inputs[row]);
    const Vec3 &value = bindings.values[row];
    constants.programInputs[row] = {{value.x, value.y, value.z},
                                    texture ? texture->slot : kNoTextureSlot};
  }
}

void FillProgramTextureRow(ProgramConstants &constants, std::size_t row,
                           const TextureLab::ProgramTexture &texture,
                           std::uint32_t targetSide) {
  if (row >= std::size(constants.programTextures))
    return;
  constants.programTextures[row] = InputRead(
      texture.sampling, ReadMip(texture.texture, texture.sampling, targetSide),
      texture.normalize);
}

std::size_t BoundTextureCount(const TextureLab::ProgramBindings &bindings) {
  return std::min<std::size_t>(bindings.textureCount, kProgramTextures);
}

void FillProgramTextures(ProgramConstants &constants,
                         const TextureLab::ProgramBindings &bindings,
                         std::uint32_t targetSide) {
  for (std::size_t row = 0; row < BoundTextureCount(bindings); ++row)
    FillProgramTextureRow(constants, row, bindings.textures[row], targetSide);
}

void BindProgramTextures(std::span<REX::W32::ID3D11ShaderResourceView *> srvs,
                         const TextureLab::ProgramBindings &bindings) {
  for (std::size_t slot = 0; slot < BoundTextureCount(bindings); ++slot)
    BindSlot(srvs, kProgramTextureSlot + slot,
             ViewOf(bindings.textures[slot].texture));
}

void FillProgramConstants(ProgramConstants &constants,
                          std::span<REX::W32::ID3D11ShaderResourceView *> srvs,
                          const ProgramCode &code,
                          const TextureLab::ProgramBindings &bindings,
                          std::uint32_t targetSide) {
  const std::size_t instructions =
      FillProgramInstructions(constants, code.instructions);
  FillProgramInputs(constants, code.inputs, bindings);
  FillProgramTextures(constants, bindings, targetSide);
  BindProgramTextures(srvs, bindings);
  constants.programCount = static_cast<std::uint32_t>(instructions);
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

std::uint32_t LayerOptions(const LayerShape &a_shape) {
  return (HasSource(a_shape) ? kLayerHasSource : 0u) |
         (HasMask(a_shape) ? kLayerHasMask : 0u);
}

StackLayer StackLayerOf(const TextureLab::LayerPass &a_layer,
                        const StackShape &a_stack, const LayerShape &a_shape,
                        std::uint32_t a_targetSide) {
  const ProgramSegment source = SourceSegment(a_stack, a_shape);
  const ProgramSegment mask = MaskSegment(a_stack, a_shape);
  const float mip = ReadMip(a_layer.source, a_layer.input, a_targetSide);
  return {.source = InputRead(a_layer.input, mip, a_layer.normalize),
          .mix = MixOf(a_layer, LayerOptions(a_shape)),
          .sourceCode = {source.first, source.count},
          .maskCode = {mask.first, mask.count}};
}

StackConstants StackConstantsOf(const StackShape &a_shape,
                                RE::NiSourceTexture *a_base,
                                std::span<const TextureLab::LayerPass> a_layers,
                                std::uint32_t a_targetSide) {
  StackConstants constants;
  constants.stackLayerCount = static_cast<std::uint32_t>(a_shape.layers.size());
  constants.stackHasBase = ShaderBoolOf(a_shape.base);
  constants.stackBaseMip = WholeReadMip(a_base, a_targetSide);
  const std::size_t count = std::min({a_layers.size(), a_shape.layers.size(),
                                      std::size(constants.stackLayers)});
  for (std::size_t index = 0; index < count; ++index)
    constants.stackLayers[index] = StackLayerOf(
        a_layers[index], a_shape, a_shape.layers[index], a_targetSide);
  return constants;
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

namespace {
void PrepareContext(ID3D11DeviceContext &a_context) {
  a_context.GSSetShader(nullptr, nullptr, 0);
  a_context.HSSetShader(nullptr, nullptr, 0);
  a_context.DSSetShader(nullptr, nullptr, 0);
  a_context.SetPredication(nullptr, false);
}
}

struct TextureLab::SavedPipeline {
  explicit SavedPipeline(ID3D11DeviceContext &a_context) {
    Capture(&a_context);
  }
  ~SavedPipeline() = default;
  SavedPipeline(const SavedPipeline &) = delete;
  SavedPipeline &operator=(const SavedPipeline &) = delete;
  SavedPipeline(SavedPipeline &&) = delete;
  SavedPipeline &operator=(SavedPipeline &&) = delete;

  void RestoreTo(ID3D11DeviceContext &a_context) { Restore(&a_context); }

private:
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
  REX::W32::ID3D11Buffer *cbs[kConstantSlotCount]{};
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
    a_ctx->PSGetConstantBuffers(0, kConstantSlotCount, cbs);
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
    a_ctx->PSSetConstantBuffers(0, kConstantSlotCount, cbs);
    a_ctx->IASetInputLayout(layout);
    a_ctx->IASetVertexBuffers(0, 1, &vertexBuffer, &vertexStride,
                              &vertexOffset);
    a_ctx->IASetIndexBuffer(indexBuffer, indexFormat, indexOffset);
    a_ctx->IASetPrimitiveTopology(topology);
    a_ctx->OMSetBlendState(blend, blendFactor, sampleMask);
    a_ctx->OMSetDepthStencilState(depth, stencilRef);
    a_ctx->RSSetState(raster);
    Release(sampler);
    for (auto &buffer : cbs) {
      Release(buffer);
    }
    Release(vertexBuffer);
    Release(indexBuffer);
    Release(layout);
    Release(blend);
    Release(depth);
    Release(raster);
  }
};

struct TextureLab::RenderPass {
  RenderPass(RE::BSGraphics::Renderer &a_renderer,
             ID3D11DeviceContext &a_context, bool a_batched)
      : rendererLock_(a_renderer), context(&a_context) {
    if (!a_batched) {
      saved_ = std::make_unique<SavedPipeline>(a_context);
      PrepareContext(a_context);
    }
  }
  ~RenderPass() {
    if (saved_) {
      saved_->RestoreTo(*context);
      return;
    }
    REX::W32::ID3D11RenderTargetView *noTarget = nullptr;
    context->OMSetRenderTargets(1, &noTarget, nullptr);
    REX::W32::ID3D11ShaderResourceView *noViews[kStackSrvs]{};
    context->PSSetShaderResources(0, kStackSrvs, noViews);
  }
  RenderPass(const RenderPass &) = delete;
  RenderPass &operator=(const RenderPass &) = delete;
  RenderPass(RenderPass &&) = delete;
  RenderPass &operator=(RenderPass &&) = delete;

  [[nodiscard]] ID3D11DeviceContext &Context() const { return *context; }

private:
  RendererLock rendererLock_;
  ID3D11DeviceContext *context;
  std::unique_ptr<SavedPipeline> saved_;
};

struct TextureLab::PassBatch::Scope {
  Scope(RE::BSGraphics::Renderer &a_renderer, ID3D11DeviceContext &a_context)
      : rendererLock_(a_renderer), context_(&a_context), saved_(a_context) {
    PrepareContext(a_context);
  }
  ~Scope() { saved_.RestoreTo(*context_); }
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;
  Scope(Scope &&) = delete;
  Scope &operator=(Scope &&) = delete;

private:
  RendererLock rendererLock_;
  ID3D11DeviceContext *context_;
  SavedPipeline saved_;
};

TextureLab::PassBatch::PassBatch(TextureLab &a_lab) : lab_(&a_lab) {
  if (!lab_->outerBatch_)
    lab_->outerBatch_ = this;
}

TextureLab::PassBatch::~PassBatch() {
  if (lab_->outerBatch_ != this)
    return;
  scope_.reset();
  lab_->outerBatch_ = nullptr;
}

bool TextureLab::PassBatch::Capture() {
  if (scope_)
    return true;
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!lab_->available_ || !renderer || !lab_->borrowedContext_)
    return false;
  scope_ = std::make_unique<Scope>(*renderer, *lab_->borrowedContext_);
  return true;
}

bool TextureLab::CaptureBatch() {
  return outerBatch_ && outerBatch_->Capture();
}

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
  if (a_target.mips == TargetMips::kGeneratedOnDraw)
    GenerateMips(a_pass, a_target);
}

void TextureLab::GenerateMips(const RenderPass &a_pass,
                              RenderTarget &a_target) {
  if (!HasChain(a_target.mips))
    return;
  std::optional<TimedSpan> mips;
  if (Timing())
    mips.emplace(*this, std::format("GenerateMips {}", a_target.size));
  a_pass.Context().GenerateMips(a_target.srv.Get());
}

void TextureLab::GenerateMipsFor(RenderTarget &a_target) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.srv.Get())
    return;
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
  GenerateMips(pass, a_target);
}

REX::W32::ID3D11ShaderResourceView *
TextureLab::RangeViewOf(const RenderTarget &a_target,
                        std::uint32_t a_mostDetailed, std::uint32_t a_levels) {
  for (const RenderTarget::RangeView &range : a_target.rangeViews_)
    if (range.mostDetailed == a_mostDetailed && range.levels == a_levels)
      return range.view.Get();
  if (!borrowedDevice_ || !a_target.texture.Get())
    return nullptr;
  D3D11_TEXTURE2D_DESC texture{};
  a_target.texture->GetDesc(&texture);
  D3D11_SHADER_RESOURCE_VIEW_DESC desc{};
  desc.format = texture.format;
  desc.viewDimension = D3D_SRV_DIMENSION_TEXTURE2D;
  desc.texture2D.mostDetailedMip = a_mostDetailed;
  desc.texture2D.mipLevels = a_levels;
  RenderTarget::RangeView range{a_mostDetailed, a_levels, {}};
  if (Failed(borrowedDevice_->CreateShaderResourceView(
          a_target.texture.Get(), &desc, range.view.GetAddressOf())))
    return nullptr;
  a_target.rangeViews_.push_back(std::move(range));
  return a_target.rangeViews_.back().view.Get();
}

bool TextureLab::GenerateMipRange(const RenderTarget &a_target,
                                  MipLevelRange a_levels) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ ||
      !HasChain(a_target.mips) || a_levels.first == 0)
    return false;
  const auto chain = static_cast<std::uint32_t>(std::bit_width(a_target.size));
  if (a_levels.first >= chain)
    return true;
  const std::uint32_t source = a_levels.first - 1;
  const std::uint32_t available = chain - source;
  const std::uint32_t levels =
      a_levels.count ? std::min(*a_levels.count + 1, available) : available;
  REX::W32::ID3D11ShaderResourceView *view =
      RangeViewOf(a_target, source, levels);
  if (!view)
    return false;
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
  pass.Context().GenerateMips(view);
  return true;
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

std::uint32_t PassLayerOptions(const LayerViews &a_views) {
  return (a_views.source ? kLayerHasSource : 0u) |
         (a_views.map ? kLayerHasMask : 0u) |
         (a_views.previous ? kLayerHasBelow : 0u) |
         (a_views.curve ? kLayerHasCurve : 0u);
}

MapView MapViewOf(const TextureLab::ChannelParams &a_channel) {
  if (a_channel.slope)
    return MapView::kSlope;
  if (a_channel.ids)
    return MapView::kIds;
  return MapView::kChannel;
}

void FillLayerPass(PassConstants &a_constants,
                   const TextureLab::LayerPass &a_layer,
                   const LayerViews &a_views) {
  a_constants.passSource = InputRead(
      a_layer.input, a_layer.input.transform.sourceMip, a_layer.normalize);
  a_constants.passLayer = MixOf(a_layer, PassLayerOptions(a_views));
  a_constants.passBelowMip = a_layer.previousMip;
}

void FillMapView(PassConstants &a_constants,
                 const TextureLab::ChannelParams &a_channel) {
  a_constants.passMapChannel = std::to_underlying(a_channel.channel);
  a_constants.passMapView = MapViewOf(a_channel);
  a_constants.passMapMip = a_channel.mapMip;
}

PassConstants PassConstantsFor(const TextureLab::LayerParams &a_params,
                               const LayerViews &a_views) {
  PassConstants constants;
  constants.passMode = std::to_underlying(a_params.mode);
  constants.passSource = PlacedRead(a_params.scroll, a_params.scroll.sourceMip);
  if (a_params.mode == TextureLab::Mode::kLayer) {
    FillLayerPass(constants, a_params.layer, a_views);
  }
  if (a_params.mode == TextureLab::Mode::kChannel) {
    FillMapView(constants, a_params.channel);
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
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
  REX::W32::ID3D11ShaderResourceView *const curve =
      a_params.layer.curve ? a_params.layer.curve->srv.Get() : nullptr;
  const std::optional<LayerViews> views =
      LayerViewsFor(a_params, a_source, curve);
  if (!views) {
    return false;
  }
  const PassConstants constants = PassConstantsFor(a_params, *views);
  pass.Context().UpdateSubresource(gpu_->constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  std::array<REX::W32::ID3D11ShaderResourceView *, kPassCurveSlot + 1> srvs{};
  BindSlot(srvs, kPassSourceSlot, views->source);
  BindSlot(srvs, kPassMapSlot, views->map);
  BindSlot(srvs, kPassBelowSlot, views->previous);
  BindSlot(srvs, kPassCurveSlot, views->curve);
  const ConstantBindings cbs =
      BindingsOf({{ConstantSlot::kPass, gpu_->constants.Get()}});
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

ProgramConstants &TextureLab::FreshProgramConstants() {
  if (!programScratch_)
    programScratch_ = std::make_unique<ProgramConstants>();
  *programScratch_ = ProgramConstants{};
  return *programScratch_;
}

const ProgramConstants *TextureLab::StackProgramConstants(
    std::span<REX::W32::ID3D11ShaderResourceView *> a_srvs,
    const BoundLayerFields &a_fields, std::uint32_t a_targetSide) {
  if (a_fields.pack.segments.empty() || !gpu_ || !gpu_->program)
    return nullptr;
  ProgramConstants &constants = FreshProgramConstants();
  const std::span<REX::W32::ID3D11ShaderResourceView *> programSrvs =
      a_srvs.first(std::min<std::size_t>(a_srvs.size(), kPassSrvs));
  FillProgramConstants(constants, programSrvs, PackedCode(a_fields.pack),
                       a_fields.bindings, a_targetSide);
  FillLookups(programSrvs, a_fields.bindings);
  return &constants;
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
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
  std::array<REX::W32::ID3D11ShaderResourceView *, kStackSrvs> srvs{};
  const ProgramConstants *program =
      StackProgramConstants(srvs, a_fields, a_target.size);
  const StackConstants constants =
      StackConstantsOf(shape, a_base, a_layers, a_target.size);
  BindLayerTextures(srvs, a_base, a_layers);
  UploadConstants(pass.Context(), pipeline.constants.Get(), constants);
  REX::W32::ID3D11Buffer *programBuffer =
      program && gpu_->program ? gpu_->program->constants.Get() : nullptr;
  if (program)
    UploadConstants(pass.Context(), programBuffer, *program);
  const ConstantBindings cbs =
      BindingsOf({{ConstantSlot::kProgram, programBuffer},
                  {ConstantSlot::kStack, pipeline.constants.Get()}});
  auto *shader = StackShaderFor(shape);
  if (!shader)
    return false;
  DrawFullScreen(pass, a_target, {shader, srvs, cbs});
  return true;
}

bool TextureLab::RenderLayersOneByOne(RenderTarget &a_target,
                                      RE::NiSourceTexture *a_base,
                                      std::span<const LayerPass> a_layers) {
  const std::shared_ptr<RenderTarget> alternate =
      a_layers.size() > 1 ? Acquire(TextureSize{a_target.size}, "layer swap",
                                    a_target.format, TargetMips::kNone)
                          : nullptr;
  if (a_layers.size() > 1 && !alternate)
    return false;
  auto *write = a_layers.size() % 2 ? &a_target : alternate.get();
  auto *other = a_layers.size() % 2 ? alternate.get() : &a_target;
  RE::NiSourceTexture *previous = a_base;
  for (auto layer : a_layers) {
    LayerParams params;
    params.mode = Mode::kLayer;
    layer.previous = previous;
    layer.previousMip = WholeReadMip(previous, a_target.size);
    layer.input.transform.sourceMip =
        ReadMip(layer.source, layer.input, a_target.size);
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
    auto target = Acquire(a_size, "stack field", TextureFormat::kRgba8,
                          TargetMips::kNone);
    if (!target || !DrawProgramPass(*target, *code, a_fields.bindings,
                                    GeneratedFor(*code)))
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

bool TextureLab::RenderProgram(RenderTarget &a_target,
                               const FieldProgram &a_program,
                               const ProgramBindings &a_bindings) {
  if (a_bindings.inputCount != a_program.Inputs().size() ||
      a_bindings.textureCount != a_program.TextureCount() ||
      a_bindings.lookupCount != a_program.FunctionLookups().size())
    return false;
  const ProgramCode code = CodeOf(a_program);
  return DrawProgramPass(a_target, code, a_bindings, GeneratedFor(code));
}

REX::W32::ID3D11PixelShader *
TextureLab::GeneratedFor(const ProgramCode &a_code) {
  return generatedShaders_ ? generated_.ProgramFor(borrowedDevice_, a_code)
                           : nullptr;
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
    const auto slot = kProgramLookupSlot + c;
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
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
  ProgramConstants &constants = FreshProgramConstants();
  REX::W32::ID3D11ShaderResourceView *srvs[kPassSrvs]{};
  FillProgramConstants(constants, srvs, a_code, a_bindings, a_target.size);
  FillLookups(srvs, a_bindings);
  constants.programVectorResult =
      ShaderBoolOf(a_code.result != ValueType::kScalar);
  UploadConstants(pass.Context(), pipeline.constants.Get(), constants);
  const ConstantBindings cbs =
      BindingsOf({{ConstantSlot::kProgram, pipeline.constants.Get()}});
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
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
  const std::optional<UploadedBakeBuffers> buffers =
      UploadBakeBuffers(*borrowedDevice_, a_bake);
  if (!buffers) {
    return false;
  }
  const std::shared_ptr<RenderTarget> coverage =
      HoldsCoverage(a_target.format)
          ? nullptr
          : Acquire(TextureSize{a_target.size}, "bake coverage",
                    FormatFor(false, a_target.format), TargetMips::kNone);
  RenderTarget &drawn = coverage ? *coverage : a_target;
  if (a_bake.noise &&
      (!gpu_->bake->noisePixel.Get() || !gpu_->bake->noiseConstants.Get())) {
    return false;
  }
  DrawBakeTriangles(pass, drawn, *buffers, a_bake.noise);
  if (DilateInto(pass, drawn, a_target)) {
    return true;
  }
  if (coverage) {
    DrawBakeTriangles(pass, a_target, *buffers, a_bake.noise);
  }
  if (a_target.mips == TargetMips::kGeneratedOnDraw) {
    GenerateMips(pass, a_target);
  }
  return true;
}

void TextureLab::DrawBakeTriangles(const RenderPass &a_pass,
                                   RenderTarget &a_target,
                                   const UploadedBakeBuffers &a_buffers,
                                   const std::optional<NoiseShading> &a_noise) {
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
  if (a_noise && pipeline.noisePixel.Get() && pipeline.noiseConstants.Get()) {
    const BakeNoiseConstants constants{.noiseOctaves = a_noise->octaves,
                                       .noiseSeed = a_noise->seed,
                                       .noiseStyle =
                                           std::to_underlying(a_noise->style)};
    a_pass.Context().UpdateSubresource(pipeline.noiseConstants.Get(), 0,
                                       nullptr, &constants, 0, 0);
    REX::W32::ID3D11Buffer *noiseBuffer = pipeline.noiseConstants.Get();
    a_pass.Context().PSSetConstantBuffers(SlotOf(ConstantSlot::kBakeNoise), 1,
                                          &noiseBuffer);
    a_pass.Context().PSSetShader(pipeline.noisePixel.Get(), nullptr, 0);
  } else {
    a_pass.Context().PSSetShader(pipeline.pixel.Get(), nullptr, 0);
  }
  REX::W32::ID3D11ShaderResourceView *noSrvs[kPassSrvs]{};
  a_pass.Context().PSSetShaderResources(0, kPassSrvs, noSrvs);
  a_pass.Context().DrawIndexed(a_buffers.indexCount, 0, 0);
  UnbindTarget(a_pass, kPassSrvs);
}

bool TextureLab::DilateInto(const RenderPass &a_pass, RenderTarget &a_source,
                            RenderTarget &a_destination) {
  const PixelPipeline *dilate =
      gpu_ && gpu_->dilate.has_value() ? &gpu_->dilate.value() : nullptr;
  const std::shared_ptr<RenderTarget> gutter =
      dilate ? Acquire(TextureSize{a_source.size}, "dilation gutter",
                       a_source.format, TargetMips::kNone)
             : nullptr;
  if (!dilate || !gutter || !gutter->rtv.Get() ||
      gutter->size != a_source.size || a_destination.size != a_source.size) {
    return false;
  }
  REX::W32::ID3D11ShaderResourceView *fromSource[]{a_source.srv.Get()};
  DrawFullScreen(a_pass, *gutter, {dilate->shader.Get(), fromSource, {}});
  REX::W32::ID3D11ShaderResourceView *fromGutter[]{gutter->srv.Get()};
  DrawFullScreen(a_pass, a_destination, {dilate->shader.Get(), fromGutter, {}});
  return true;
}

static_assert(TextureLab::kRippleFirings == kRippleFiringRows);

namespace {
constexpr float kFaintestRipple = 1.0f / 512.0f;

RippleConstants RippleConstantsOf(const TextureLab::RipplePass &a_pass) {
  RippleConstants constants;
  const std::size_t given =
      std::min<std::size_t>(a_pass.firingCount, a_pass.firings.size());
  std::size_t count = 0;
  for (std::size_t k = 0; k < given; ++k) {
    const TextureLab::RippleFiring &firing = a_pass.firings[k];
    const float strength = std::exp(-a_pass.decay * firing.age);
    if (!(strength >= kFaintestRipple) ||
        count >= std::size(constants.rippleFirings))
      continue;
    constants.rippleFirings[count] = {
        {firing.origin.x, firing.origin.y, firing.origin.z},
        firing.age * a_pass.speed};
    SetLane(constants.rippleStrengths, count, strength);
    ++count;
  }
  constants.rippleDirection = {a_pass.direction.x, a_pass.direction.y,
                               a_pass.direction.z};
  constants.rippleFiringCount = static_cast<std::uint32_t>(count);
  constants.rippleWidth = a_pass.width;
  constants.rippleFrame = a_pass.frame;
  constants.rippleDisc = ShaderBoolOf(a_pass.disc);
  constants.rippleDirectional = ShaderBoolOf(a_pass.directional);
  return constants;
}
}

bool TextureLab::RenderRipple(RenderTarget &a_target,
                              const RipplePass &a_pass) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_->ripple.has_value()) {
    return false;
  }
  const PixelPipeline &pipeline = *gpu_->ripple;
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
  const auto *positions = DataOf(a_pass.positions);
  if (!positions || !positions->resourceView) {
    return false;
  }
  const RippleConstants constants = RippleConstantsOf(a_pass);

  pass.Context().UpdateSubresource(pipeline.constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  std::array<REX::W32::ID3D11ShaderResourceView *, kPassSrvs> srvs{};
  BindSlot(srvs, kSingleSourceSlot,
           reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
               positions->resourceView));
  const ConstantBindings cbs =
      BindingsOf({{ConstantSlot::kRipple, pipeline.constants.Get()}});
  DrawFullScreen(pass, a_target, {pipeline.shader.Get(), srvs, cbs});
  return true;
}

namespace {
FilterConstants FilterConstantsOf(const TextureLab::FilterPass &a_pass,
                                  std::uint32_t a_targetSide,
                                  std::uint32_t a_fromSide, bool a_vertical,
                                  bool a_keepSeams) {
  const std::uint32_t taps =
      std::min(a_pass.taps, static_cast<std::uint32_t>(kMaxFilterTaps));
  FilterConstants constants{
      .filterOperation =
          static_cast<std::uint32_t>(std::to_underlying(a_pass.operation)),
      .filterTaps = taps,
      .filterVertical = ShaderBoolOf(a_vertical),
      .filterKeepSeams = ShaderBoolOf(a_keepSeams),
      .filterTargetSize = {a_targetSide, a_targetSide},
      .filterInputSize = {a_fromSide, a_fromSide},
      .filterResamples = ShaderBoolOf(a_fromSide != a_targetSide)};
  for (std::uint32_t k = 0; k <= taps; ++k)
    SetLane(constants.filterWeights, k,
            FilterWeight(static_cast<std::int32_t>(k),
                         static_cast<std::int32_t>(taps)));
  return constants;
}
}

bool TextureLab::RenderFilter(RenderTarget &a_target,
                              const FilterPass &a_pass) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_->filter.has_value() || !a_pass.input || !a_pass.input->srv.Get() ||
      (a_pass.charts &&
       (!a_pass.charts->srv.Get() || a_pass.charts->size != a_target.size))) {
    return false;
  }
  const std::shared_ptr<RenderTarget> across =
      Acquire(TextureSize{a_target.size}, "filter pass",
              TextureFormat::kRgba16Float, TargetMips::kNone);
  if (!across || !across->rtv.Get() || across->size != a_target.size) {
    return false;
  }
  const PixelPipeline &pipeline = *gpu_->filter;
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
  REX::W32::ID3D11ShaderResourceView *charts =
      a_pass.charts ? a_pass.charts->srv.Get() : nullptr;
  const ConstantBindings cbs =
      BindingsOf({{ConstantSlot::kFilter, pipeline.constants.Get()}});
  const auto draw = [&](RenderTarget &a_into, RenderTarget &a_from,
                        bool a_vertical) {
    const FilterConstants constants = FilterConstantsOf(
        a_pass, a_target.size, a_from.size, a_vertical, charts != nullptr);
    pass.Context().UpdateSubresource(pipeline.constants.Get(), 0, nullptr,
                                     &constants, 0, 0);
    std::array<REX::W32::ID3D11ShaderResourceView *, kFilterChartsSlot + 1>
        srvs{};
    BindSlot(srvs, kFilterInputSlot, a_from.srv.Get());
    BindSlot(srvs, kFilterChartsSlot, charts);
    DrawFullScreen(pass, a_into, {pipeline.shader.Get(), srvs, cbs});
  };
  draw(*across, *a_pass.input, false);
  draw(a_target, *across, true);
  return true;
}

bool TextureLab::SubmitMaterialSample(const MaterialMaps &a_maps,
                                      MaterialReadback &a_readback) {
  RE::NiSourceTexture *a_rmaos = a_maps.rmaos;
  RE::NiSourceTexture *a_diffuse = a_maps.diffuse;
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
  auto target = Acquire(TextureSize(kSampleSide), "sample",
                        TextureFormat::kRgba8, TargetMips::kNone);
  if (!target || target->size != kSampleSide) {
    return fail(a_rmaos, "no sample target");
  }
  if (!EnsureSampleStaging(a_readback)) {
    return fail(a_rmaos, "no sample staging textures");
  }
  if (!CopyMap(*target, a_rmaos, MipThatFits(*rmaosExtent, kSampleSide),
               *a_readback.rmaos.Get())) {
    return fail(a_rmaos, "the RMAOS map could not be copied");
  }
  if (!DrawDiffuseDetail(*target, a_maps,
                         MipThatFits(*diffuseExtent, kSampleSide)) ||
      !CopyToStaging(*target, *a_readback.diffuse.Get())) {
    return fail(a_diffuse, "the diffuse map could not be copied");
  }
  a_readback.pending = true;
  return true;
}

bool TextureLab::CopyMap(RenderTarget &a_target, RE::NiSourceTexture *a_map,
                         float a_mip, REX::W32::ID3D11Texture2D &a_staging) {
  LayerParams params;
  params.mode = Mode::kCopy;
  params.scroll.sourceMip = a_mip;
  return Render(a_target, a_map, params) && CopyToStaging(a_target, a_staging);
}

std::optional<TextureLab::MaterialSampleResult>
TextureLab::CollectMaterialSample(MaterialReadback &a_readback) {
  return ReadMaterialSample(a_readback, false);
}

bool TextureLab::DrawDiffuseDetail(RenderTarget &a_target,
                                   const MaterialMaps &a_maps,
                                   float a_diffuseMip) {
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  const auto *diffuseData = DataOf(a_maps.diffuse);
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_->diffuseDetail.has_value() || !diffuseData ||
      !diffuseData->resourceView) {
    return false;
  }
  const PixelPipeline &pipeline = *gpu_->diffuseDetail;
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
  const DiffuseDetailConstants constants{.diffuseDetailNormal =
                                             NormalDetailOf(a_maps.normal),
                                         .diffuseDetailMip = a_diffuseMip};
  pass.Context().UpdateSubresource(pipeline.constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  std::array<REX::W32::ID3D11ShaderResourceView *, kPassSrvs> srvs{};
  BindSlot(srvs, kMaterialDiffuseSlot,
           reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
               diffuseData->resourceView));
  BindSlot(srvs, kMaterialNormalSlot, NormalView(a_maps.normal));
  const ConstantBindings cbs =
      BindingsOf({{ConstantSlot::kDiffuseDetail, pipeline.constants.Get()}});
  DrawFullScreen(pass, a_target, {pipeline.shader.Get(), srvs, cbs});
  return true;
}

std::optional<MaterialSample>
TextureLab::SampleMaterial(const MaterialMaps &a_maps) {
  MaterialReadback readback;
  if (!SubmitMaterialSample(a_maps, readback)) {
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

TextureLab::MapPixelsResult
TextureLab::CopyMapPixels(RE::NiSourceTexture *a_map, std::uint32_t a_maxSide) {
  if (!Init()) {
    return std::unexpected("the texture lab is unavailable");
  }
  const std::optional<Extent> extent = ExtentOf(a_map);
  if (!extent || extent->width == 0 || extent->height == 0) {
    return std::unexpected("the map is null or not a resident 2D texture");
  }
  const std::uint32_t wanted =
      (std::min)(a_maxSide, (std::max)(extent->width, extent->height));
  auto target = Acquire(TextureSize(wanted), "dump", TextureFormat::kRgba8,
                        TargetMips::kNone);
  if (!target || target->Side() == 0) {
    return std::unexpected("no render target for the copy");
  }
  const std::uint32_t side = target->Side();
  REX::W32::ComPtr<REX::W32::ID3D11Texture2D> staging;
  if (!EnsureStaging(staging, side)) {
    return std::unexpected("no staging texture for the copy");
  }
  MapPixels out;
  out.mip = MipThatFits(*extent, side);
  out.source = *extent;
  if (!CopyMap(*target, a_map, out.mip, *staging.Get())) {
    return std::unexpected("the map could not be copied");
  }
  std::optional<std::vector<std::uint8_t>> pixels =
      ReadStagingRgba(*staging.Get(), side);
  if (!pixels) {
    return std::unexpected("the copy could not be read back");
  }
  out.image = RgbaImage{side, side, std::move(*pixels)};
  return out;
}

namespace {
GroupScore GroupScoreOf(const GroupScoring &a_group, float a_oddsWeight) {
  GroupScore score;
  for (std::size_t axis = 0; axis < kTexelAxes; ++axis)
    SetLane(score.mean, axis, a_group.mean[axis]);
  for (std::size_t w = 0; w < kWhiteningFloats; ++w)
    SetLane(score.whitening, w, a_group.whitening[w]);
  score.id = static_cast<std::uint32_t>(std::max<int>(a_group.id, 0));
  score.logScale =
      a_group.logScale - (a_oddsWeight > 0.0f ? a_group.logShare : 0.0f);
  return score;
}

GroupDraw GroupDrawOf(const TextureLab::ClusterDraw &a_draw) {
  if (a_draw.palette)
    return GroupDraw::kPalette;
  if (a_draw.picked)
    return GroupDraw::kPicked;
  return GroupDraw::kIds;
}

GroupConstants GroupConstantsOf(const MaterialGrouping &a_grouping,
                                const TextureLab::ClusterDraw &a_draw,
                                const NormalDetail &a_detail) {
  GroupConstants constants;
  const Standardisation &standard = a_grouping.groups.standardisation;
  for (std::size_t axis = 0; axis < kTexelAxes; ++axis) {
    SetLane(constants.groupAxisCentre, axis,
            static_cast<float>(standard.centre[axis]));
    SetLane(constants.groupAxisSpread, axis,
            static_cast<float>(standard.spread[axis]));
  }
  const float oddsWeight = OddsWeightOf(a_grouping.settings);
  const std::vector<GroupScoring> &scoring = a_grouping.scoring;
  const std::size_t count =
      std::min(scoring.size(), std::size(constants.groups));
  for (std::size_t k = 0; k < count; ++k)
    constants.groups[k] = GroupScoreOf(scoring[k], oddsWeight);
  constants.groupCount = static_cast<std::uint32_t>(count);
  constants.groupDraw = GroupDrawOf(a_draw);
  constants.groupPicked = a_draw.picked.value_or(0);
  constants.groupOddsWeight = oddsWeight;
  constants.groupDetail = a_detail;
  return constants;
}

std::array<Uint4, kLitChartRows>
LitChartRows(const std::array<bool, kChartSlots> &a_lit) {
  std::array<std::uint32_t, kLitChartRows * 4> words{};
  for (std::size_t chart = 0; chart < kChartSlots; ++chart)
    if (a_lit[chart])
      words[chart / 32] |= 1u << (chart % 32);
  std::array<Uint4, kLitChartRows> rows{};
  for (std::size_t row = 0; row < kLitChartRows; ++row)
    rows[row] = {words[row * 4], words[row * 4 + 1], words[row * 4 + 2],
                 words[row * 4 + 3]};
  return rows;
}
}

bool TextureLab::RenderClusters(
    RenderTarget &a_target, const MaterialMaps &a_maps,
    const std::shared_ptr<const MaterialGrouping> &a_owner,
    const ClusterDraw &a_draw) {
  if (!a_owner) {
    return false;
  }
  const MaterialGrouping &a_grouping = *a_owner;
  auto *renderer = RE::BSGraphics::Renderer::GetSingleton();
  if (!available_ || !renderer || !borrowedContext_ || !a_target.rtv.Get() ||
      !gpu_->clusters.has_value()) {
    return false;
  }
  const auto *rmaosData = DataOf(a_maps.rmaos);
  const auto *diffuseData = DataOf(a_maps.diffuse);
  const std::vector<GroupScoring> &scoring = a_grouping.scoring;
  if (!rmaosData || !rmaosData->resourceView || !diffuseData ||
      !diffuseData->resourceView || scoring.empty() ||
      scoring.size() > kMaxMaterialClusters) {
    return false;
  }
  const std::optional<GroupOdds> odds = GroupOddsOf(a_owner);
  if (!odds) {
    return false;
  }
  const PixelPipeline &pipeline = *gpu_->clusters;
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
  GroupConstants constants =
      GroupConstantsOf(a_grouping, a_draw, NormalDetailOf(a_maps.normal));
  constants.groupRmaosMip = WholeReadMip(a_maps.rmaos, a_target.size);
  constants.groupDiffuseMip = WholeReadMip(a_maps.diffuse, a_target.size);
  REX::W32::ID3D11ShaderResourceView *chartIds = nullptr;
  if (a_draw.charts) {
    if (!a_draw.charts->chartIds || !a_draw.charts->chartIds->srv.Get()) {
      return false;
    }
    chartIds = a_draw.charts->chartIds->srv.Get();
    constants.groupChartFilter = ShaderBool::kYes;
    std::ranges::copy(LitChartRows(a_draw.charts->lit),
                      constants.groupLitCharts);
  }

  pass.Context().UpdateSubresource(pipeline.constants.Get(), 0, nullptr,
                                   &constants, 0, 0);
  std::array<REX::W32::ID3D11ShaderResourceView *, kPassSrvs> srvs{};
  BindSlot(srvs, kMaterialDiffuseSlot,
           reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
               diffuseData->resourceView));
  BindSlot(srvs, kMaterialRmaosSlot,
           reinterpret_cast<REX::W32::ID3D11ShaderResourceView *>(
               rmaosData->resourceView));
  BindSlot(srvs, kMaterialNormalSlot, NormalView(a_maps.normal));
  BindSlot(srvs, kGroupChartIdsSlot, chartIds);
  BindSlot(srvs, kGroupOddsLowSlot, (*odds)[0].Get());
  BindSlot(srvs, kGroupOddsHighSlot, (*odds)[1].Get());
  const ConstantBindings cbs =
      BindingsOf({{ConstantSlot::kGroup, pipeline.constants.Get()}});
  DrawFullScreen(pass, a_target, {pipeline.shader.Get(), srvs, cbs});
  return true;
}

std::optional<TextureLab::GroupOdds> TextureLab::GroupOddsOf(
    const std::shared_ptr<const MaterialGrouping> &a_grouping) {
  std::erase_if(groupOdds_,
                [](const auto &a_entry) { return a_entry.first.expired(); });
  for (const auto &[owner, odds] : groupOdds_) {
    if (owner.lock() == a_grouping) {
      return odds;
    }
  }
  std::optional<GroupOdds> odds = CreateGroupOdds(a_grouping->smoothed);
  if (!odds) {
    return std::nullopt;
  }
  if (groupOdds_.size() >= kGroupOddsCacheSize) {
    groupOdds_.erase(groupOdds_.begin());
  }
  groupOdds_.emplace_back(a_grouping, *odds);
  return odds;
}

std::optional<TextureLab::GroupOdds>
TextureLab::CreateGroupOdds(const GroupProbabilities &a_odds) {
  const std::size_t texels =
      static_cast<std::size_t>(a_odds.side) * a_odds.side;
  if (!borrowedDevice_ || a_odds.side == 0 || a_odds.ofTexel.size() != texels) {
    return std::nullopt;
  }
  GroupOdds out;
  for (std::size_t half = 0; half < out.size(); ++half) {
    std::vector<std::uint8_t> pixels(texels * 4);
    for (std::size_t i = 0; i < texels; ++i) {
      for (std::size_t c = 0; c < 4; ++c) {
        pixels[i * 4 + c] = a_odds.ofTexel[i][half * 4 + c];
      }
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.width = a_odds.side;
    desc.height = a_odds.side;
    desc.mipLevels = 1;
    desc.arraySize = 1;
    desc.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.sampleDesc.count = 1;
    desc.usage = D3D11_USAGE_IMMUTABLE;
    desc.bindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data{pixels.data(), a_odds.side * 4, 0};
    REX::W32::ComPtr<REX::W32::ID3D11Texture2D> texture;
    if (Failed(borrowedDevice_->CreateTexture2D(&desc, &data,
                                                texture.GetAddressOf())) ||
        Failed(borrowedDevice_->CreateShaderResourceView(
            texture.Get(), nullptr, out[half].GetAddressOf()))) {
      logger::error("TextureLab: could not create the group odds textures");
      return std::nullopt;
    }
  }
  return out;
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
  if ((desc.format != DXGI_FORMAT_R32G32B32A32_FLOAT &&
       desc.format != DXGI_FORMAT_R32_FLOAT) ||
      extents.empty())
    return false;
  std::vector<ReductionLevel *> levels;
  for (const auto &extent : extents) {
    auto *level = ReductionLevelFor(extent);
    if (!level)
      return false;
    levels.push_back(level);
  }
  const PixelPipeline &pipeline = *gpu_->reduce;
  const RenderPass pass{*renderer, *borrowedContext_, CaptureBatch()};
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
        .reduceKind = static_cast<std::uint32_t>(a_kind),
        .reduceComponents = ReductionComponents(a_type),
        .reduceExtent = {sourceExtent.width, sourceExtent.height},
        .reduceReadsField = ShaderBoolOf(i == 0)};
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
    context.PSSetConstantBuffers(SlotOf(ConstantSlot::kReduce), 1, buffers);
    context.Draw(3, 0);
    UnbindTarget(pass, 1);
    source = levels[i]->srv.Get();
    sourceExtent = extents[i];
  }
  context.CopyResource(&a_staging, levels.back()->texture.Get());
  return true;
}
}
