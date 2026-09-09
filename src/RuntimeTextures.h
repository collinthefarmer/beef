#pragma once

#include "Analysis.h"
#include "Core.h"
#include "Expression.h"
#include "Mesh.h"
#include "PCH.h"
#include "TextureSize.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace BetterEnchantmentEffects
{
	class TextureLab
	{
	public:
		enum class Mode : std::uint32_t
		{
			kGlow = 0,
			kSheen = 1,
			kHeight = 2,
			kRoughness = 3,
			kChannel = 4,
			kMaskedGlow = 5,
			kLayer = 6,
			kCopy = 7,
		};

		enum class MapReading : std::uint32_t
		{
			kNone = 0,
			kDisplacementR = 1,
			kOcclusionB = 2,
			kRmaos = 3,
			kNormalSlope = 4,
			kDiffuseLuma = 5,
		};

		struct Scroll
		{
			float uOffset = 0.0f;
			float vOffset = 0.0f;
			float tileU = 1.0f;
			float tileV = 1.0f;
			bool  mirrorU = false;
			bool  mirrorV = false;
			bool  transpose = false;
			float sourceMip = 0.0f;
		};

		struct InputMap
		{
			RE::NiSourceTexture* texture = nullptr;
			MapReading           reading = MapReading::kNone;
		};

		struct HeightParams
		{
			float armorWeight = 1.0f;
			float reliefContrast = 3.0f;
			float reliefMean = 0.5f;
			float noiseWeight = 0.35f;
			float noiseMean = 0.5f;
		};

		struct RoughnessParams
		{
			float strength = 0.25f;
			float contrast = 2.0f;
		};

		struct ChannelParams
		{
			ShaderChannel channel = ShaderChannel::kRgb;
			bool          slope = false;
		};

		struct MaskParams
		{
			std::uint32_t channel = 1;
			float         threshold = 0.0f;
			float         softness = 0.1f;
			bool          invert = false;
			float         strength = 1.0f;
		};

		struct LayerInput
		{
			ShaderChannel channel = ShaderChannel::kRgb;
			bool          meshSpace = false;
			Scroll        transform;
		};

		class Lookup
		{
		public:
			~Lookup();
			REX::W32::ID3D11Texture2D*          texture = nullptr;
			REX::W32::ID3D11ShaderResourceView* srv = nullptr;
		};

		inline static constexpr std::uint32_t kProgramTextures = 8;
		inline static constexpr std::uint32_t kProgramRefs = 16;
		inline static constexpr std::uint32_t kProgramCurves = 4;
		inline static constexpr std::uint32_t kProgramStack = 32;

		struct ProgramTexture
		{
			RE::NiSourceTexture* texture = nullptr;
			LayerInput           sampling;
			float                normalize = 1.0f;
		};
		struct ProgramRef
		{
			bool          isTexture = false;
			std::uint32_t texture = 0;
			Vec3          value{};
		};
		struct ProgramPass
		{
			std::span<const Program::Node>              code;
			std::array<ProgramRef, kProgramRefs>        refs{};
			std::uint32_t                               refCount = 0;
			std::array<ProgramTexture, kProgramTextures> textures{};
			std::uint32_t                               textureCount = 0;
			std::array<const Lookup*, kProgramCurves>   curves{};
			std::uint32_t                               curveCount = 0;
			float                                       time = 0.0f;
			bool                                        vectorResult = false;
		};

		inline static constexpr std::uint32_t kRippleFirings = 8;
		struct RippleFiring
		{
			Vec3  origin;
			float age = 0.0f;
		};
		struct RipplePass
		{
			RE::NiSourceTexture*                      positions = nullptr;
			float                                     frame = 128.0f;
			std::array<RippleFiring, kRippleFirings>  firings{};
			std::uint32_t                             firingCount = 0;
			float                                     speed = 100.0f;
			float                                     width = 10.0f;
			float                                     decay = 1.0f;
			bool                                      disc = false;
		};

		struct LayerPass
		{
			RE::NiSourceTexture* previous = nullptr;
			RE::NiSourceTexture* source = nullptr;
			LayerInput           input;
			float                normalize = 1.0f;
			float                color[3]{ 1.0f, 1.0f, 1.0f };
			float                opacity = 1.0f;
			std::uint32_t        blend = 0;
			std::uint32_t        channels = 15;
			RE::NiSourceTexture* mask = nullptr;
			ShaderChannel        maskChannel = ShaderChannel::kR;
			const Lookup*        curve = nullptr;
		};

		struct LayerParams
		{
			Mode            mode = Mode::kGlow;
			Scroll          scroll;
			InputMap     map;
			HeightParams    height;
			RoughnessParams roughness;
			ChannelParams   channel;
			MaskParams      mask;
			LayerPass       layer;
		};

		class RenderTarget
		{
		public:
			~RenderTarget();
			[[nodiscard]] RE::NiSourceTexture* Texture() const noexcept { return presenter.get(); }

			RE::NiPointer<RE::NiSourceTexture>  presenter;
			RE::NiTexture::RendererData*        originalData = nullptr;
			RE::NiTexture::RendererData*        ourData = nullptr;
			REX::W32::ID3D11Texture2D*          texture = nullptr;
			REX::W32::ID3D11ShaderResourceView* srv = nullptr;
			REX::W32::ID3D11RenderTargetView*   rtv = nullptr;
			std::uint32_t                       size = 0;
		};

		[[nodiscard]] static TextureLab* GetSingleton();

		bool Init();
		[[nodiscard]] bool Available() const noexcept { return available_; }

		std::shared_ptr<RenderTarget> Acquire(TextureSize a_size);

		bool Render(RenderTarget& a_target, RE::NiSourceTexture* a_source, const LayerParams& a_params);
		bool RenderProgram(RenderTarget& a_target, const ProgramPass& a_pass);
		[[nodiscard]] bool InterpreterAvailable() const noexcept { return programPs_ != nullptr; }

		bool BakeMesh(RenderTarget& a_target, const BakeBuffers& a_bake);
		bool RenderRipple(RenderTarget& a_target, const RipplePass& a_pass);
		[[nodiscard]] bool RippleAvailable() const noexcept { return ripplePs_ != nullptr; }

		inline static constexpr std::uint32_t kSampleSide = 64;
		[[nodiscard]] std::optional<MaterialSample> SampleMaterial(RE::NiSourceTexture* a_rmaos, RE::NiSourceTexture* a_diffuse);

		bool RenderClusters(RenderTarget& a_target, RE::NiSourceTexture* a_rmaos, RE::NiSourceTexture* a_diffuse, const MaterialAnalysis& a_analysis);
		[[nodiscard]] bool ClassifyAvailable() const noexcept { return classifyPs_ != nullptr; }
		[[nodiscard]] bool BakingAvailable() const noexcept { return bakeVs_ != nullptr && bakeLayout_ != nullptr; }

		[[nodiscard]] std::vector<std::uint8_t> ReadBuffer(REX::W32::ID3D11Buffer* a_buffer, std::uint32_t a_bytes);

		std::shared_ptr<Lookup> CreateLookup(std::span<const float, 256> a_values);

		struct Extent
		{
			std::uint32_t width = 0;
			std::uint32_t height = 0;
		};
		static std::optional<Extent> ExtentOf(RE::NiSourceTexture* a_source);

		float MeanLuminance(RE::NiSourceTexture* a_source);
		float MeanChannel(RE::NiSourceTexture* a_source, ShaderChannel a_channel);

		std::shared_ptr<RenderTarget> Preview(RE::NiSourceTexture* a_source, ShaderChannel a_channel, bool a_dynamic);
		void                    RenderPreviews();
		void                    ClearPreviews();
		void                    InvalidatePreviews() noexcept { previewGeneration_.fetch_add(1, std::memory_order_relaxed); }

		[[nodiscard]] RenderTarget* Scratch(TextureSize a_size);

		void Clear();

	private:
		struct SavedState;

		bool CompileShaders();
		bool CreateTarget(RenderTarget& a_target, TextureSize a_size);
		RE::NiPointer<RE::NiSourceTexture> LoadPresenter();
		void Recycle(RenderTarget* a_target);

		bool                                            available_ = false;
		bool                                            initTried_ = false;
		REX::W32::ID3D11Device*                         device_ = nullptr;
		REX::W32::ID3D11DeviceContext*                  context_ = nullptr;
		REX::W32::ID3D11VertexShader*                   vs_ = nullptr;
		REX::W32::ID3D11PixelShader*                    ps_ = nullptr;
		REX::W32::ID3D11PixelShader*                    programPs_ = nullptr;
		REX::W32::ID3D11VertexShader*                   bakeVs_ = nullptr;
		REX::W32::ID3D11PixelShader*                    bakePs_ = nullptr;
		REX::W32::ID3D11InputLayout*                    bakeLayout_ = nullptr;
		REX::W32::ID3D11PixelShader*                    ripplePs_ = nullptr;
		REX::W32::ID3D11Buffer*                         rippleConstants_ = nullptr;
		REX::W32::ID3D11PixelShader*                    classifyPs_ = nullptr;
		REX::W32::ID3D11Buffer*                         classifyConstants_ = nullptr;
		REX::W32::ID3D11Buffer*                         constants_ = nullptr;
		REX::W32::ID3D11Buffer*                         programConstants_ = nullptr;
		REX::W32::ID3D11SamplerState*                   sampler_ = nullptr;
		REX::W32::ID3D11BlendState*                     blend_ = nullptr;
		REX::W32::ID3D11DepthStencilState*              depth_ = nullptr;
		REX::W32::ID3D11RasterizerState*                raster_ = nullptr;
		std::uint32_t                                   nextPresenter_ = 0;
		std::vector<std::unique_ptr<RenderTarget>>            pool_;
		std::map<std::uint32_t, std::shared_ptr<RenderTarget>> scratch_;
		std::unordered_map<RE::NiSourceTexture*, float> luminance_;
		std::map<std::pair<RE::NiSourceTexture*, ShaderChannel>, float> channelMeans_;
		std::unordered_set<RE::NiSourceTexture*>        sampleWarned_;

		std::optional<float> ReadBackMean(RenderTarget& a_target);
		std::vector<std::uint8_t> ReadBackPixels(RenderTarget& a_target);
		using PreviewKey = std::pair<RE::NiSourceTexture*, ShaderChannel>;
		struct PreviewEntry
		{
			std::shared_ptr<RenderTarget> target;
			std::uint64_t           generation = 0;
			bool                    dynamic = false;
			bool                    wanted = false;
		};
		std::mutex                                            previewLock_;
		std::map<PreviewKey, PreviewEntry>                    previews_;
		std::atomic<std::uint64_t>                            previewGeneration_{ 1 };
		std::uint64_t                                         previewSeen_ = 1;
		std::vector<std::pair<std::shared_ptr<RenderTarget>, std::uint64_t>> previewGraveyard_;
		std::uint64_t                                         previewTick_ = 0;
	};
}
